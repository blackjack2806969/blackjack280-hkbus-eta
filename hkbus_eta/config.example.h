#pragma once

// Copy this file to config.h, then enter your own settings.
// config.h is ignored by Git and must never be uploaded.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define STOP_LABEL "上水"
// 天文台現時沒有「上水」溫度站；打鼓嶺是北區可用測站。
#define HKO_TEMPERATURE_STATION "打鼓嶺"

// The dashboard is active only inside these daily windows.
// Outside them it shows the standby notice, turns Wi-Fi off and stops API calls.
// Set ACTIVE_WINDOW_COUNT from 1 to 6. Add the matching WINDOW_3...WINDOW_6
// definitions when you increase the count. Windows may cross midnight.
#define ACTIVE_WINDOW_COUNT 2
#define ACTIVE_WINDOW_1_START_HOUR 2
#define ACTIVE_WINDOW_1_START_MINUTE 30
#define ACTIVE_WINDOW_1_END_HOUR 4
#define ACTIVE_WINDOW_1_END_MINUTE 0
#define ACTIVE_WINDOW_2_START_HOUR 6
#define ACTIVE_WINDOW_2_START_MINUTE 0
#define ACTIVE_WINDOW_2_END_HOUR 9
#define ACTIVE_WINDOW_2_END_MINUTE 0

// Example third period (uncomment and set ACTIVE_WINDOW_COUNT to 3):
// #define ACTIVE_WINDOW_3_START_HOUR 12
// #define ACTIVE_WINDOW_3_START_MINUTE 0
// #define ACTIVE_WINDOW_3_END_HOUR 13
// #define ACTIVE_WINDOW_3_END_MINUTE 30

#define ROUTE_1 "A43"
#define STOP_ID_1 ""
#define ROUTE_2 "278A"
#define STOP_ID_2 ""
#define ROUTE_3 "277X"
#define STOP_ID_3 ""

// Automatic route schedule. Set 1 to 4 active slots.
// Each slot stays active until the next slot's start time.
#define SCHEDULE_SLOT_COUNT 1
#define SLOT_1_START_HOUR 0
#define SLOT_1_START_MINUTE 0
#define SLOT_1_ROUTE_1 ROUTE_1
#define SLOT_1_STOP_ID_1 STOP_ID_1
#define SLOT_1_ROUTE_2 ROUTE_2
#define SLOT_1_STOP_ID_2 STOP_ID_2
#define SLOT_1_ROUTE_3 ROUTE_3
#define SLOT_1_STOP_ID_3 STOP_ID_3

#define SLOT_2_START_HOUR 8
#define SLOT_2_START_MINUTE 0
#define SLOT_2_ROUTE_1 ""
#define SLOT_2_STOP_ID_1 ""
#define SLOT_2_ROUTE_2 ""
#define SLOT_2_STOP_ID_2 ""
#define SLOT_2_ROUTE_3 ""
#define SLOT_2_STOP_ID_3 ""

#define SLOT_3_START_HOUR 17
#define SLOT_3_START_MINUTE 0
#define SLOT_3_ROUTE_1 ""
#define SLOT_3_STOP_ID_1 ""
#define SLOT_3_ROUTE_2 ""
#define SLOT_3_STOP_ID_2 ""
#define SLOT_3_ROUTE_3 ""
#define SLOT_3_STOP_ID_3 ""

#define SLOT_4_START_HOUR 23
#define SLOT_4_START_MINUTE 0
#define SLOT_4_ROUTE_1 ""
#define SLOT_4_STOP_ID_1 ""
#define SLOT_4_ROUTE_2 ""
#define SLOT_4_STOP_ID_2 ""
#define SLOT_4_ROUTE_3 ""
#define SLOT_4_STOP_ID_3 ""

