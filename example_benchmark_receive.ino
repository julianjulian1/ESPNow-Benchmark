#include <Arduino.h>
#include "myESPNow.h"

// -------------------------------------------------------------
// Benchmark Configuration
// -------------------------------------------------------------
#define USE_LR_MODE true
#define TX_POWER 80
#define STATS_INTERVAL 5 // Print stats every 5 seconds

// Stats variables
uint32_t totalReceived = 0;
uint64_t totalBytesReceived = 0;
int32_t lastPacketLen = 0;
int32_t sumRSSI = 0;
unsigned long startTime = 0;
unsigned long lastStatsTime = 0;
void onDataRecv(const uint8_t *macAddr, const uint8_t *data, int len, int rssi) {
    totalReceived++;
    totalBytesReceived += len;
    lastPacketLen = len;
    sumRSSI += rssi;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n--- ESP-NOW RECEIVE BENCHMARK ---");
    MyESPNow::begin(1, USE_LR_MODE, TX_POWER);
    MyESPNow::setRecvCallback(onDataRecv);

    startTime = millis();
    lastStatsTime = startTime;
    Serial.println("Benchmarking started... Receiving as many messages as possible.");
}

void loop() {
    unsigned long now = millis();
    if (now - lastStatsTime >= (STATS_INTERVAL * 1000)) {
        unsigned long elapsedMs = now - lastStatsTime;
        float elapsedSec = (float)elapsedMs / 1000.0;
        
        float pps = (float)totalReceived / elapsedSec;
        float kbps = ((float)totalBytesReceived * 8.0) / (elapsedSec * 1024.0); // kilobits per second
        float avgRSSI = (totalReceived > 0) ? ((float)sumRSSI / totalReceived) : 0;

        Serial.println("\n--- BENCHMARK STATS ---");
        Serial.printf("Last Packet Size: %d bytes\n", lastPacketLen);
        Serial.printf("Elapsed: %lu seconds\n", (now - startTime) / 1000);
        Serial.printf("Packets Received (Interval): %u\n", totalReceived);
        Serial.printf("Packets Per Second: %.2f\n", pps);
        Serial.printf("Throughput: %.2f kbps\n", kbps);
        Serial.printf("Avg RSSI: %.2f dBm\n", avgRSSI);
        Serial.println("------------------------");

        // Reset interval counters
        totalReceived = 0;
        totalBytesReceived = 0;
        sumRSSI = 0;
        lastStatsTime = now;
    }
}
