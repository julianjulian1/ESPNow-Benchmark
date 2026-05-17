#include <Arduino.h>
#include "myESPNow.h"

// -------------------------------------------------------------
// Benchmark Configuration
// -------------------------------------------------------------
// WARNING: If using BROADCAST (FF:FF:FF:FF:FF:FF), the Success Rate 
// will ALWAYS be 100% because Broadcast has NO Hardware ACK.
// For a real Success Rate / Packet Loss benchmark, you MUST use 
// the specific MAC address of the receiver.

// #define USE_SPECIFIC_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}

#ifdef USE_SPECIFIC_MAC
uint8_t targetMacAddress[] = USE_SPECIFIC_MAC;
#else
uint8_t targetMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Default: Broadcast
#endif

#define USE_LR_MODE true
#define TX_POWER 80      // Max Power
#define STATS_INTERVAL 5 // Print stats every 5 seconds
#define PAYLOAD_SIZE 250 // Configurable payload size (up to 250 bytes)

// Stats variables
uint32_t totalSent = 0;
uint64_t totalBytesSent = 0;
uint32_t totalSuccess = 0;
uint32_t totalFail = 0;
unsigned long startTime = 0;
unsigned long lastStatsTime = 0;

// Flow control & RTT tracking
volatile bool readyToSend = true;
volatile unsigned long lastSendTimeMicros = 0;
volatile uint32_t sumRTTMicros = 0;
volatile uint32_t rttCount = 0;

void onDataSent(bool success) {
    unsigned long rtt = micros() - lastSendTimeMicros;
    
    if (success) {
        totalSuccess++;
        sumRTTMicros += rtt;
        rttCount++;
    } else {
        totalFail++;
    }
    // Packet processed by hardware, ready for next one
    readyToSend = true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n--- ESP-NOW SEND BENCHMARK ---");
    MyESPNow::begin(1, USE_LR_MODE, TX_POWER);
    MyESPNow::setSendCallback(onDataSent);
    MyESPNow::addPeer(targetMacAddress, 1, false);

    startTime = millis();
    lastStatsTime = startTime;
    Serial.println("Benchmarking started... Sending as fast as possible.");
}

void loop() {
    // Only send if the previous packet has been processed (Flow Control)
    if (readyToSend) {
        static uint8_t payload[PAYLOAD_SIZE];
        
        // Mark as busy and record timestamp
        readyToSend = false;
        lastSendTimeMicros = micros();

        if (MyESPNow::send(targetMacAddress, payload, PAYLOAD_SIZE)) {
            totalSent++;
            totalBytesSent += PAYLOAD_SIZE;
        } else {
            // If it failed to even queue, allow immediate retry
            readyToSend = true;
        }
    }

    // Calculate and display stats every X seconds
    unsigned long now = millis();
    if (now - lastStatsTime >= (STATS_INTERVAL * 1000)) {
        unsigned long elapsedMs = now - lastStatsTime;
        float elapsedSec = (float)elapsedMs / 1000.0;
        
        float pps = (float)totalSent / elapsedSec;
        float kbps = ((float)totalBytesSent * 8.0) / (elapsedSec * 1024.0); // kilobits per second
        float successRate = (totalSent > 0) ? ((float)totalSuccess / (totalSuccess + totalFail) * 100.0) : 0;
        float avgRTT = (rttCount > 0) ? ((float)sumRTTMicros / rttCount / 1000.0) : 0; // Convert micros to ms

        Serial.println("\n--- BENCHMARK STATS ---");
        Serial.printf("Payload Size: %d bytes\n", PAYLOAD_SIZE);
        Serial.printf("Elapsed: %lu seconds\n", (now - startTime) / 1000);
        Serial.printf("Packets Sent (Interval): %u\n", totalSent);
        Serial.printf("Packets Per Second: %.2f\n", pps);
        Serial.printf("Throughput: %.2f kbps\n", kbps);
        Serial.printf("Success (ACK) Rate: %.2f%%\n", successRate);
        Serial.printf("Hardware RTT: %.2f ms\n", avgRTT);
        Serial.println("------------------------");

        // Reset interval counters
        totalSent = 0;
        totalBytesSent = 0;
        totalSuccess = 0;
        totalFail = 0;
        sumRTTMicros = 0;
        rttCount = 0;
        lastStatsTime = now;
    }
}
