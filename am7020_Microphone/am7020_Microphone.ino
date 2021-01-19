#include "config.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

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
PubSubClient  mqttClient(MQTT_BROKER, MQTT_PORT, tcpClient);

// 定義麥克風輸入腳位
#define MIC_PIN A0

void mqttConnect(void);
void nbConnect(void);

void setup()
{
    SerialMon.begin(MONITOR_BAUDRATE);
    SerialAT.begin(AM7020_BAUDRATE);

    pinMode(MIC_PIN, INPUT);
    // AM7020 NBIOT 連線基地台
    nbConnect();
    // 設定 MQTT KeepAlive 為270秒
    mqttClient.setKeepAlive(270);
}

void loop()
{
    static unsigned long timer = 0;
    // 檢查 MQTT Client 連線狀態
    if (!mqttClient.connected()) {
        // 檢查 NBIOT 連線狀態
        if (!modem.isNetworkConnected()) {
            nbConnect();
        }
        SerialMon.println(F("=== MQTT NOT CONNECTED ==="));
        mqttConnect();
    }

    if (millis() >= timer) {
        timer     = millis() + UPLOAD_INTERVAL;
        // 讀取麥克風原始類比數據
        int   adc = analogRead(MIC_PIN);
        // 線性回歸
        float dB  = (adc + 291.269) / 11.550;
        SerialMon.print(F("dB="));
        SerialMon.println((int)dB);
        // 發布資料
        mqttClient.publish(MQTT_TOPIC, String((int)dB).c_str());
    }
    // mqtt handle
    mqttClient.loop();
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
