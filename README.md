# blackjack280-hkbus-eta

一個俾 4.2 吋橫向屏幕使用嘅香港巴士到站顯示屏。ESP32 會提供本機網頁，
讀取九巴／龍運 ETA 同香港天文台天氣資料；畫面固定顯示 `A43`、`278A`、
`277X` 三條路線。

## 已完成

- 三條路線、每條最多三班車倒數分鐘
- 日期 `DD/MM`、星期、香港時間
- 上水氣溫、濕度及天氣圖示
- 黃／紅／黑雨與 1／3／8／9／10 號風球
- 警告只更換天氣圖示，其他版面完全不變
- 首次開機設定 Wi‑Fi、巴士站名稱及各路線 stop ID
- 30 秒自動更新、斷線時保留上一筆資料
- 全屏 4.2 吋橫向 UI，亦支援窄屏設定
- 可用 `?demo=1` 預覽示範資料

## 重要：顯示方式

ESP32 負責提供網頁及抓取資料。屏幕端需要可以開網頁（例如內置 Chromium kiosk、
Android WebView、Raspberry Pi browser，或另一部連到同一網絡嘅裝置）。

如果你塊 4.2 吋屏幕係「直接用 SPI／RGB 接 ESP32」而唔係可以開網頁，請提供屏幕
型號或購買連結；顯示 driver 需要按實際控制器另加，現時不能安全地估 pin 位。

## Arduino IDE 安裝

### 1. 安裝 ESP32 board package

在 Arduino IDE `File > Preferences > Additional Boards Manager URLs` 加入：

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

然後到 Boards Manager 搜尋並安裝 **esp32 by Espressif Systems**。在
`Tools > Board` 選擇你實際使用嘅 ESP32／ESP32-S3 型號。

### 2. 安裝 ArduinoJson

到 `Tools > Manage Libraries`，搜尋 **ArduinoJson by Benoit Blanchon**，
安裝 7.x 版本。

ESP32 core 已包含其餘用到嘅 libraries：

- WiFi
- WiFiClientSecure
- HTTPClient
- WebServer
- Preferences
- LittleFS

### 3. 安裝 LittleFS 上載工具

網頁檔案位於 [`hkbus_eta/data`](hkbus_eta/data)，需要同 sketch 一齊燒入 flash。
Arduino IDE 2.2.1 或以上可使用
[arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)。

按該工具 README 安裝後：

1. 開啟 [`hkbus_eta/hkbus_eta.ino`](hkbus_eta/hkbus_eta.ino)。
2. 選好 Board、Port，先按一般 **Upload** 上載程式。
3. 按 `Ctrl+Shift+P`（macOS 為 `⌘+Shift+P`）。
4. 執行 **Upload LittleFS to Pico/ESP8266/ESP32**。
5. 開 Serial Monitor，baud rate 設為 `115200`。

> 如果 Arduino IDE 上載 filesystem 時報空間不足，將 Partition Scheme 改成一個
> 有 LittleFS／SPIFFS 空間嘅選項，例如 `Default 4MB with spiffs`。LittleFS 可以
> 使用該 data partition。

## 首次開機

1. ESP32 會開一個 Wi‑Fi：
   - 名稱：`HKBus-ETA-Setup`
   - 密碼：`hkbuseta`
2. 連接後打開 `http://192.168.4.1/`。
3. 輸入屋企 Wi‑Fi、顯示名稱，以及三條路線各自嘅巴士站 ID。
4. 儲存後 ESP32 會重啟，再連接屋企 Wi‑Fi。
5. Serial Monitor 會顯示新嘅本機網址，例如 `http://192.168.1.123/`。
6. 顯示屏用全屏／kiosk 模式打開該網址。

設定保存在 ESP32 NVS；設定 API 不會把已儲存密碼傳回瀏覽器。日後在畫面右下角按
半透明齒輪可修改設定。

## 如何找巴士站 ID

每條路線要填該路線喺你想顯示嘅車站所對應嘅 KMB／LWB stop ID。官方資料：

- [九巴／龍運實時到站資料集](https://data.gov.hk/tc-data/dataset/hk-td-tis_21-etakmb)
- Stop list：`https://data.etabus.gov.hk/v1/transport/kmb/stop`
- Route-stop：`https://data.etabus.gov.hk/v1/transport/kmb/route-stop/{route}/{inbound|outbound}/{service_type}`

`service_type` 目前預設為 `1`。如果某條特別班次使用其他 service type，可在
[`hkbus_eta.ino`](hkbus_eta/hkbus_eta.ino) 入面修改 ETA URL。

## 本機預覽 UI

未燒 ESP32 前，可以在 repo 根目錄執行：

```powershell
python -m http.server 8080 --directory hkbus_eta/data
```

再開 `http://localhost:8080/?demo=1`。

## 資料來源

- 九巴／龍運 ETA：香港運輸署 DATA.GOV.HK 開放數據
- 天氣及警告：香港天文台 Open Data API

ETA 約每分鐘由資料供應方更新，本專案每 30 秒輪詢一次。臨時改道請以營辦商公佈為準。

## 私隱與安全

- Wi‑Fi 密碼只儲存在 ESP32 NVS，不會提交到 GitHub。
- 設定頁只應在你信任嘅本機網絡使用。
- 韌體只對公開、唯讀嘅政府 API 發出 GET 請求。
- HTTPS 使用 `setInsecure()`，因微控制器未同步時鐘前未必能驗證 CA。此設計適合讀取
  公開資料，但不應照搬到帳戶、付款或其他敏感 API。

## License

[MIT](LICENSE)

