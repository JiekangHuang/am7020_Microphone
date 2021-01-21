#include "config.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

#define DEBUG false

#ifdef DEBUG_DUMP_AT_COMMAND
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm        modem(debugger, AM7020_RESET);
#else
// 建立 AM7020 modem（設定 Serial 及 EN Pin）
TinyGsm modem(SerialAT, AM7020_RESET);
#endif

// 在 modem 架構上建立 Tcp Client
TinyGsmClient tcpClient(modem);
// 在 Tcp Client 架構上建立 MQTT Client
PubSubClient mqttClient(MQTT_BROKER, MQTT_PORT, tcpClient);

// 定義麥克風輸入腳位
#define MIC_PIN A0
#define INTERCEPT 200.9
#define SLOPE 10.711

int   year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
float timezone = 0.0;
typedef enum
{
    LIGHT_RED,
    LIGHT_GREEN,
} LIGHT_E;

LIGHT_E pre_dB_light = LIGHT_RED, pre_rule_light = LIGHT_RED, dB_light = LIGHT_RED, rule_light = LIGHT_RED;

int dB_max = 0;

/* 噪音標準 第三類 規則 */
// 日間
#define RULE1(h, dB) (h >= 7 && h < 20 && dB > 65)
// 晚間
#define RULE2(h, dB) (h >= 20 && h < 23 && dB > 60)
// 夜間
#define RULE3(h, dB) ((h >= 23 || h < 7) && dB > 55)
// 施工時段 規則
#define RULE4(w, h) (((w != 0 && w != 6) && (h >= 22 || h < 6)) || (!(w != 0 && w != 6) && ((h >= 18 || h < 8) || (h >= 12 && h < 13))))

void mqttConnect(void);
void nbConnect(void);
int  calcWeek(int year, int month, int day);

void setup()
{
    SerialMon.begin(MONITOR_BAUDRATE);
    SerialAT.begin(AM7020_BAUDRATE);

    pinMode(MIC_PIN, INPUT);
#if DEBUG
#else
    // AM7020 NBIOT 連線基地台
    nbConnect();
    // 設定 MQTT KeepAlive 為270秒
    mqttClient.setKeepAlive(270);
#endif
}

void loop()
{
#if DEBUG
    int adc = analogRead(MIC_PIN);
    SerialMon.print(F("adc="));
    SerialMon.print(adc);
    // 線性回歸
    float dB = (adc + INTERCEPT) / SLOPE;
    SerialMon.print(F(",dB="));
    SerialMon.println((int)dB);
#else
    static unsigned long timer = 0, timer1 = 0;
    // 檢查 MQTT Client 連線狀態
    if (!mqttClient.connected()) {
        // 檢查 NBIOT 連線狀態
        if (!modem.isNetworkConnected()) {
            nbConnect();
        }
        SerialMon.println(F("=== MQTT NOT CONNECTED ==="));
        mqttConnect();
    }

    if (millis() > timer1) {
        timer1 = millis() + 100;
        // 讀取麥克風原始類比數據
        int adc = analogRead(MIC_PIN);
        // 線性回歸
        float dB = (adc + INTERCEPT) / SLOPE;
        if (dB > dB_max) {
            dB_max = (int)dB;
        }
        SerialMon.print(F("dB="));
        SerialMon.print(dB);
        SerialMon.print(F(",dB_max="));
        SerialMon.println(dB_max);
    }

    if (millis() >= timer) {
        timer = millis() + UPLOAD_INTERVAL;
        /* dB data */
        SerialMon.print(F("dB_max="));
        SerialMon.println(dB_max);
        mqttClient.publish(MQTT_TOPIC_MIC, String(dB_max).c_str());

        if (modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second, &timezone)) {
            /* dB light */
            int h    = ((hour + (int)timezone) % 24);
            dB_light = (RULE1(h, dB_max) || RULE2(h, dB_max) || RULE3(h, dB_max)) ? LIGHT_RED : LIGHT_GREEN;
            if (pre_dB_light != dB_light) {
                pre_dB_light = dB_light;
                mqttClient.publish(MQTT_TOPIC_DB_COLOR, String(dB_light).c_str());
            }

            /* rule light */
            rule_light = (RULE4(calcWeek(year, month, day), h)) ? LIGHT_RED : LIGHT_GREEN;
            if (pre_rule_light != rule_light) {
                pre_rule_light = rule_light;
                mqttClient.publish(MQTT_TOPIC_RULE_COLOR, String(rule_light).c_str());
            }
        } else {
            SerialMon.println("get time error !");
        }
        dB_max = 0;
    }
    // mqtt handle
    mqttClient.loop();
#endif
}

/**
 * MQTT Client 連線
 */
void mqttConnect(void)
{
    SerialMon.print(F("Connecting to "));
    SerialMon.print(MQTT_BROKER);
    SerialMon.print(F("..."));

    // Connect to MQTT Broker
    while (!mqttClient.connect("AM7020_MQTTID_MIC_20210119", MQTT_USERNAME, MQTT_PASSWORD)) {
        SerialMon.println(F(" fail"));
    }
    SerialMon.println(F(" success"));
}

/**
 * AM7020 NBIOT 連線基地台
 */
void nbConnect(void)
{
    SerialMon.println(F("Initializing modem..."));
    while (!modem.init() || !modem.nbiotConnect(APN, BAND)) {
        SerialMon.print(F("."));
    };

    SerialMon.print(F("Waiting for network..."));
    while (!modem.waitForNetwork()) {
        SerialMon.print(F("."));
    }
    SerialMon.println(F(" success"));
}

/**
 * 計算星期
 *
 * @param year 西元年
 * @param month 月
 * @param day 日
 * @return 星期
 */
int calcWeek(int year, int month, int day)
{
    // https://ronaldchik.blogspot.com/2014/01/how-to-calculate-weekday.html
    /* (日號＋月值＋年值＋過4＋世紀值－潤) ÷7）*/
    // 月份代表數值
    const int m_d[] = {6, 2, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    // 世紀代表數值
    const int c_d[] = {0, 5, 3, 1};
    // 取年前兩碼
    int yf = atoi(String(year).substring(0, 2).c_str());
    // 取年後兩碼
    int yb = atoi(String(year).substring(2).c_str());
    // 計算閏年
    bool leap = ((!(year % 4) && (year % 100)) || !(year % 400));
    // 計算星期
    int week = ((day + m_d[month - 1] + yb + (int)(yb / 4) + c_d[((yf - 16) % 4)] - ((int)leap)) % 7);
    return week;
}