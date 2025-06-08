#include <Arduino.h>
#include <WiFi.h>

#define DCSBIOS_DISABLE_SERVO
#define DCSBIOS_ESP32_UDP
#define DCSBIOS_LOG_OUTPUT Serial
#include <DcsBios.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID_HERE";
const char* WIFI_PASS = "YOUR_WIFI_PASS_HERE";

DcsBios::Switch2Pos masterCautionResetSw("MASTER_CAUTION_RESET_SW", 16);
DcsBios::LED        masterCaution(0x7408, 0x0200, 17);

void setupWiFi() {
    DCSBIOS_LOG("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        DCSBIOS_LOG(".");
        delay(100);
    }
    DCSBIOS_LOG(" connected\r\n");
}

void setup() {
#ifdef DCSBIOS_LOG_OUTPUT
    DCSBIOS_LOG_OUTPUT.begin(115200);
#endif
    setupWiFi();

    DcsBios::setup();
}

void loop() {
    DcsBios::loop();
}
