#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <sys/time.h>
#include "ST7305_U8g2.h"
#include "adc_bsp.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

constexpr uint8_t SHTC3_ADDR=0x70, RTC_ADDR=0x51;
constexpr uint32_t DATA_REFRESH_MS=60000;
ST7305_U8g2 display(11,12,5,40,41);
U8G2 *gfx=nullptr;
struct RouteData { const char *route; const char *stopId; int eta[3]={-1,-1,-1}; };
enum WarningState { NORMAL,RAIN_AMBER,RAIN_RED,RAIN_BLACK,TC1,TC3,TC8,TC9,TC10 };
RouteData routes[3]={{ROUTE_1,STOP_ID_1},{ROUTE_2,STOP_ID_2},{ROUTE_3,STOP_ID_3}};
float localTemp=NAN,localHumidity=NAN;
WarningState warningState=NORMAL;
uint32_t lastDataRefresh=0; int lastDrawnMinute=-1;
uint8_t bcdToDec(uint8_t v){return(v>>4)*10+(v&15);} uint8_t decToBcd(uint8_t v){return((v/10)<<4)|(v%10);}

bool readShtc3(float &temperature,float &humidity){
  Wire.beginTransmission(SHTC3_ADDR);Wire.write(0x35);Wire.write(0x17);if(Wire.endTransmission()!=0)return false;delay(1);
  Wire.beginTransmission(SHTC3_ADDR);Wire.write(0x78);Wire.write(0x66);if(Wire.endTransmission()!=0)return false;delay(15);
  if(Wire.requestFrom(SHTC3_ADDR,(uint8_t)6)!=6)return false;
  uint16_t rawT=(Wire.read()<<8)|Wire.read();Wire.read();uint16_t rawH=(Wire.read()<<8)|Wire.read();Wire.read();
  temperature=-45.0f+175.0f*rawT/65535.0f;humidity=100.0f*rawH/65535.0f;
  Wire.beginTransmission(SHTC3_ADDR);Wire.write(0xB0);Wire.write(0x98);Wire.endTransmission();return true;
}
bool readRtc(tm &v){
  Wire.beginTransmission(RTC_ADDR);Wire.write(0x04);if(Wire.endTransmission(false)!=0||Wire.requestFrom(RTC_ADDR,(uint8_t)7)!=7)return false;v={};
  v.tm_sec=bcdToDec(Wire.read()&0x7F);v.tm_min=bcdToDec(Wire.read()&0x7F);v.tm_hour=bcdToDec(Wire.read()&0x3F);
  v.tm_mday=bcdToDec(Wire.read()&0x3F);v.tm_wday=bcdToDec(Wire.read()&7);v.tm_mon=bcdToDec(Wire.read()&0x1F)-1;v.tm_year=bcdToDec(Wire.read())+100;return v.tm_year>=120;
}
void writeRtc(const tm &v){
  Wire.beginTransmission(RTC_ADDR);Wire.write(0x04);Wire.write(decToBcd(v.tm_sec));Wire.write(decToBcd(v.tm_min));Wire.write(decToBcd(v.tm_hour));
  Wire.write(decToBcd(v.tm_mday));Wire.write(decToBcd(v.tm_wday));Wire.write(decToBcd(v.tm_mon+1));Wire.write(decToBcd((v.tm_year+1900)%100));Wire.endTransmission();
}
bool getJson(const String &url,JsonDocument &json){
  WiFiClientSecure client;client.setInsecure();HTTPClient http;http.setConnectTimeout(7000);http.setTimeout(9000);if(!http.begin(client,url))return false;
  http.addHeader("Accept","application/json");http.addHeader("User-Agent","blackjack280-hkbus-eta-native/0.2");int code=http.GET();
  if(code!=HTTP_CODE_OK){Serial.printf("GET -> %d\n",code);http.end();return false;}DeserializationError error=deserializeJson(json,http.getStream());http.end();return !error;
}
time_t parseEta(const char *iso){
  if(!iso||strlen(iso)<19)return 0;tm v={};if(sscanf(iso,"%d-%d-%dT%d:%d:%d",&v.tm_year,&v.tm_mon,&v.tm_mday,&v.tm_hour,&v.tm_min,&v.tm_sec)!=6)return 0;
  v.tm_year-=1900;v.tm_mon--;return timegm(&v)-8*3600;
}
void fetchRoute(RouteData &route){
  for(int &v:route.eta)v=-1;if(!strlen(route.stopId)||WiFi.status()!=WL_CONNECTED)return;JsonDocument json;
  String url="https://data.etabus.gov.hk/v1/transport/kmb/eta/"+String(route.stopId)+"/"+route.route+"/1";if(!getJson(url,json))return;
  int index=0;time_t now=time(nullptr);for(JsonObjectConst item:json["data"].as<JsonArrayConst>()){if(index==3)break;time_t arrival=parseEta(item["eta"]);long seconds=arrival-now;if(arrival&&seconds>=-30)route.eta[index++]=max(0L,(seconds+30)/60);}
}
void fetchWarnings(){
  warningState=NORMAL;JsonDocument json;if(WiFi.status()!=WL_CONNECTED||!getJson("https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=tc",json))return;
  String rain=json["WRAIN"]["code"]|"";if(rain=="WRAINB"){warningState=RAIN_BLACK;return;}if(rain=="WRAINR"){warningState=RAIN_RED;return;}if(rain=="WRAINA"){warningState=RAIN_AMBER;return;}
  String tc=json["WTCSGNL"]["code"]|"";if(tc=="TC10")warningState=TC10;else if(tc=="TC9")warningState=TC9;else if(tc.startsWith("TC8"))warningState=TC8;else if(tc=="TC3")warningState=TC3;else if(tc=="TC1")warningState=TC1;
}
void centered(const char *text,int x,int width,int y){gfx->drawUTF8(x+max(0,(width-gfx->getUTF8Width(text))/2),y,text);}
void drawWeatherIcon(int cx,int cy){
  if(warningState!=NORMAL){gfx->drawCircle(cx,cy,27);gfx->drawCircle(cx,cy,26);const char *label=warningState==RAIN_AMBER?"黃雨":warningState==RAIN_RED?"紅雨":warningState==RAIN_BLACK?"黑雨":warningState==TC1?"T1":warningState==TC3?"T3":warningState==TC8?"T8":warningState==TC9?"T9":"T10";gfx->setFont(u8g2_font_unifont_t_chinese2);centered(label,cx-27,54,cy+5);return;}
  gfx->drawDisc(cx-10,cy-9,12);for(int a=0;a<360;a+=45){float r=a*PI/180.0f;gfx->drawLine(cx-10+cos(r)*17,cy-9+sin(r)*17,cx-10+cos(r)*22,cy-9+sin(r)*22);}
  gfx->setDrawColor(0);gfx->drawDisc(cx+7,cy+8,18);gfx->drawDisc(cx-9,cy+12,13);gfx->setDrawColor(1);gfx->drawCircle(cx+7,cy+8,18);gfx->drawCircle(cx-9,cy+12,13);gfx->drawHLine(cx-22,cy+24,43);
}
void drawDashboard(){
  tm now={};getLocalTime(&now,50);gfx->clearBuffer();gfx->setDrawColor(1);gfx->drawFrame(0,0,400,300);gfx->drawVLine(300,0,300);gfx->drawHLine(0,40,300);gfx->drawHLine(0,126,300);gfx->drawHLine(0,212,300);gfx->drawVLine(90,40,260);gfx->drawVLine(160,40,260);gfx->drawVLine(230,40,260);
  gfx->setFont(u8g2_font_unifont_t_chinese2);gfx->drawUTF8(10,27,"路 線");gfx->setFont(u8g2_font_helvB14_tf);centered("1st",90,70,27);centered("2nd",160,70,27);centered("3rd",230,70,27);
  int rowTop[3]={40,126,212};for(int r=0;r<3;++r){int y=rowTop[r];gfx->drawRBox(6,y+17,78,52,5);gfx->setDrawColor(0);gfx->setFont(u8g2_font_helvB18_tf);centered(routes[r].route,6,78,y+51);gfx->setDrawColor(1);
    for(int i=0;i<3;++i){char text[6];routes[r].eta[i]<0?strcpy(text,"--"):snprintf(text,sizeof(text),"%d",routes[r].eta[i]);gfx->setFont(u8g2_font_logisoso28_tn);centered(text,90+i*70,54,y+55);if(routes[r].eta[i]>=0){gfx->setFont(u8g2_font_unifont_t_chinese2);gfx->drawUTF8(143+i*70,y+55,"分");}}}
  uint8_t battery=Adc_GetBatteryLevel();gfx->drawFrame(331,12,26,12);gfx->drawBox(357,15,3,6);gfx->drawBox(334,15,map(battery,0,100,0,20),6);char text[28];gfx->setFont(u8g2_font_7x14B_tf);snprintf(text,sizeof(text),"%u%%",battery);gfx->drawStr(365,23,text);
  gfx->setFont(u8g2_font_logisoso24_tn);snprintf(text,sizeof(text),"%02d/%02d",now.tm_mday,now.tm_mon+1);centered(text,300,100,66);static const char *week[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
  gfx->drawRBox(319,76,63,26,4);gfx->setDrawColor(0);gfx->setFont(u8g2_font_unifont_t_chinese2);centered(week[now.tm_wday],319,63,95);gfx->setDrawColor(1);gfx->setFont(u8g2_font_logisoso24_tn);snprintf(text,sizeof(text),"%02d:%02d",now.tm_hour,now.tm_min);centered(text,300,100,139);gfx->drawHLine(307,151,86);
  drawWeatherIcon(330,215);gfx->setFont(u8g2_font_unifont_t_chinese2);isnan(localTemp)?strcpy(text,"上水 --°C"):snprintf(text,sizeof(text),"上水 %.0f°C",localTemp);gfx->drawUTF8(353,197,text);isnan(localHumidity)?strcpy(text,"濕度 --%"):snprintf(text,sizeof(text),"濕度 %.0f%%",localHumidity);gfx->drawUTF8(353,221,text);gfx->drawUTF8(353,246,warningState==NORMAL?"本地天氣":"天氣警告");if(!strlen(WIFI_SSID)){gfx->setFont(u8g2_font_5x7_tf);gfx->drawStr(306,292,"EDIT config.h");}gfx->sendBuffer();
}
void syncClock(){
  configTime(8*3600,0,"time.cloudflare.com","pool.ntp.org","time.google.com");tm v={};if(getLocalTime(&v,8000)){writeRtc(v);return;}if(readRtc(v)){time_t utc=timegm(&v)-8*3600;timeval tv={utc,0};settimeofday(&tv,nullptr);}
}
void refreshData(){readShtc3(localTemp,localHumidity);if(WiFi.status()==WL_CONNECTED){for(RouteData &route:routes)fetchRoute(route);fetchWarnings();}lastDataRefresh=millis();drawDashboard();}
void setup(){
  Serial.begin(115200);Wire.begin(14,13);Adc_PortInit();display.begin(U8G2_R1);gfx=display.getU8g2();if(strlen(WIFI_SSID)){WiFi.mode(WIFI_STA);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);uint32_t started=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-started<20000)delay(250);}syncClock();refreshData();
}
void loop(){tm now={};if(getLocalTime(&now,10)&&now.tm_min!=lastDrawnMinute){lastDrawnMinute=now.tm_min;drawDashboard();}if(millis()-lastDataRefresh>=DATA_REFRESH_MS)refreshData();delay(250);}
