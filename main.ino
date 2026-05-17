#include <Arduino.h>
#include "myESPNow.h"

// -------------------------------------------------------------
// Configurable Target MAC Address
// -------------------------------------------------------------
// To send to a specific configured MAC address, uncomment the 
// #define line below and set the MAC address accordingly.
// Otherwise, it defaults to the Broadcast address.

// #define USE_SPECIFIC_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}

#ifdef USE_SPECIFIC_MAC
uint8_t targetMacAddress[] = USE_SPECIFIC_MAC;
#else
uint8_t targetMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast MAC
#endif

// Set to true to enable Long Range (LR) mode, or false for standard mode.
#define USE_LR_MODE true

// TX Power selection (Range: 8 to 84. 80 is 20dBm which is max)
#define TX_POWER 80

// -------------------------------------------------------------


// Timing variables for sending data
unsigned long lastSendTime = 0;
const int sendInterval = 1000;
uint32_t messageCount = 0;

// To track hardware RTT
volatile unsigned long packetSendMicros = 0;


// Callback when data is sent
void onDataSent(bool success) {
    unsigned long rtt = micros() - packetSendMicros;
    float rttMs = rtt / 1000.0;

    Serial.print("Delivery Status: ");
    if (success) {
        Serial.printf("Success (Hardware ACK received) | RTT: %.2f ms\n", rttMs);
    } else {
        Serial.println("Fail (No ACK)");
    }
}

// Callback when data is received
void onDataRecv(const uint8_t *macAddr, const uint8_t *data, int len, int rssi) {
    Serial.print("Received ");
    Serial.print(len);
    Serial.print(" bytes from ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", macAddr[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.printf(" | RSSI: %d dBm | Data: ", rssi);

    // Assuming we receive a null-terminated string or we manually terminate it
    char str[len + 1];
    memcpy(str, data, len);
    str[len] = '\0';
    Serial.println(str);
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Give serial monitor time to connect

    Serial.println("\nESP-NOW Setup...");

    // Initialize ESP-NOW on channel 1, with LR mode and TX Power
    MyESPNow::begin(1, USE_LR_MODE, TX_POWER);

    // Register callbacks
    MyESPNow::setSendCallback(onDataSent);
    MyESPNow::setRecvCallback(onDataRecv);

    // Add peer to send to
    MyESPNow::addPeer(targetMacAddress, 1, false);

    Serial.println("ESP-NOW Ready");
}

void loop() {
    if (millis() - lastSendTime >= sendInterval) {
        lastSendTime = millis();
        messageCount++;

        char msg[64];
        snprintf(msg, sizeof(msg), "Hello ESP-NOW #%u", messageCount);

        Serial.print("Sending: ");
        Serial.println(msg);

        // Record time right before sending
        packetSendMicros = micros();

        // Send to target (Broadcast or specific MAC)
        MyESPNow::send(targetMacAddress, (const uint8_t *)msg, strlen(msg));
    }
}
