#include <Arduino.h>
#include "myESPNow.h"

// -------------------------------------------------------------
// Configurable Target MAC Address
// -------------------------------------------------------------
// To send to a specific configured MAC address, uncomment the 
// #define line below and set the MAC address accordingly.
// Otherwise, it defaults to the Broadcast address.

// Define multiple target MAC addresses
uint8_t targetMacAddresses[][6] = {
    {0xA4, 0xF0, 0x0F, 0x60, 0x1D, 0x10},
    {0xA8, 0x46, 0x74, 0x46, 0x06, 0x44}
};
const int numTargets = sizeof(targetMacAddresses) / sizeof(targetMacAddresses[0]);

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

    // Add peers to send to
    for (int i = 0; i < numTargets; i++) {
        MyESPNow::addPeer(targetMacAddresses[i], 1, false);
    }

    Serial.println("ESP-NOW Ready");
    Serial.print("Device MAC Address: ");
    Serial.println(WiFi.macAddress());
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

        // Send to targets
        for (int i = 0; i < numTargets; i++) {
            MyESPNow::send(targetMacAddresses[i], (const uint8_t *)msg, strlen(msg));
        }
    }
}
