# blackjack280-hkbus-eta

為 **Waveshare ESP32-S3-RLCD-4.2** 製作的香港巴士 ETA 顯示屏。

## 硬件

- ESP32-S3-WROOM-1-N16R8（16MB Flash / 8MB PSRAM）
- ST7305 4.2" 黑白全反射 RLCD，橫向 400×300
- SHTC3 溫濕度感測器
- PCF85063 RTC
- 18650 電池及板載電壓量度

## 功能

- `A43 / 278A / 277X`，每條顯示三班 ETA
- 直接驅動 ST7305 RLCD，不需要 Chrome 或另一部電腦
- 400×300 原生黑白 UI
- 香港天文台即時室外溫度及相對濕度（不是 SHTC3 室內讀數）
- PCF85063 RTC；有網絡時以 NTP 校時
- 黃／紅／黑雨及 1／3／8／9／10 號風球只更換天氣圖示
- 18650 電量顯示
- 原本的 HTML/CSS 設計稿保留在 `hkbus_eta/data`

## Arduino IDE

官方要求 **Arduino-ESP32 3.3.0 或以上**。

### Libraries

在 Arduino Library Manager 安裝：

- `ArduinoJson` 7.x
- `U8g2`

ST7305 port 已放在 sketch 內，來源為 Waveshare 官方 Arduino U8g2 example。

### Board 設定

- Board：`ESP32S3 Dev Module`
- Flash Size：`16MB`
- PSRAM：`OPI PSRAM`
- USB CDC On Boot：`Enabled`
- Upload Mode：`UART0 / Hardware CDC`

實物到手後，以 Waveshare `Tools-Configuration.png` 再核對 Arduino IDE 選項。

## 設定 Wi-Fi 及巴士站

1. 複製 `hkbus_eta/config.example.h` 為 `hkbus_eta/config.h`。
2. 填入 Wi-Fi SSID／密碼。
3. 填入三條路線各自的 KMB/LWB stop ID。
4. `HKO_TEMPERATURE_STATION` 預設為天文台「打鼓嶺」測站；可按需要改成 API 提供的其他測站。
5. `config.h` 已加入 `.gitignore`，不會上載密碼到 GitHub。

```cpp
#define WIFI_SSID "你的 Wi-Fi"
#define WIFI_PASSWORD "你的密碼"
#define ROUTE_1 "A43"
#define STOP_ID_1 "巴士站 ID"
#define HKO_TEMPERATURE_STATION "打鼓嶺"
```

## 編譯

用 Arduino IDE 開啟 `hkbus_eta/hkbus_eta.ino`，按 Verify 編譯，再用 USB-C Upload。此 native 版本不需要 LittleFS upload。

## 資料來源

- 九巴／龍運 ETA：`https://data.etabus.gov.hk`
- 香港天文台即時天氣及警告：`https://data.weather.gov.hk`
- 室外溫度：天文台 `rhrread` 的「打鼓嶺」測站（天文台現時沒有「上水」溫度站）
- 室外相對濕度：天文台 `rhrread` 提供的「香港天文台」數值
- 板載 SHTC3：只作機內感測／Serial 除錯，不會顯示成室外天氣
- 時鐘：NTP + 板載 PCF85063

## 上游硬件資料

- [Waveshare 官方產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
- [Waveshare 官方 Arduino examples](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)

ST7305 port 及 ADC 實作源自 Waveshare Apache-2.0 example；原始授權見 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

## License

本專案原創部分採用 [MIT](LICENSE)。
