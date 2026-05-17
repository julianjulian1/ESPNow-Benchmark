#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

class MyESPNow {
public:
    // User callback signatures
    typedef void (*RecvCb)(const uint8_t *macAddr, const uint8_t *data, int len, int rssi);
    typedef void (*SendCb)(bool success);

    static void begin(uint8_t channel = 1, bool useLR = false, int8_t txPower = 78);
    static void setChannel(uint8_t channel);
    static void setTxPower(int8_t power);
    static bool addPeer(const uint8_t *macAddr, uint8_t channel = 0, bool encrypt = false);
    static bool send(const uint8_t *macAddr, const uint8_t *data, size_t len);

    static void setRecvCallback(RecvCb cb);
    static void setSendCallback(SendCb cb);

private:
    static RecvCb userRecvCb;
    static SendCb userSendCb;

    // ESP32 Arduino Core v3.x (IDF 5.x) callback signatures
    static void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
};

// --- IMPLEMENTATION ---
// Used inline to prevent multiple definition if included in multiple source files.

inline MyESPNow::RecvCb MyESPNow::userRecvCb = nullptr;
inline MyESPNow::SendCb MyESPNow::userSendCb = nullptr;

inline void MyESPNow::setRecvCallback(RecvCb cb) {
    userRecvCb = cb;
}

inline void MyESPNow::setSendCallback(SendCb cb) {
    userSendCb = cb;
}

inline void MyESPNow::begin(uint8_t channel, bool useLR, int8_t txPower) {
    WiFi.mode(WIFI_STA);
    
    if (useLR) {
        // Set protocol to Long Range (LR)
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
        Serial.println("ESP-NOW: LR Mode Enabled");
    }

    if (txPower > 0) {
        setTxPower(txPower);
    }

    if (channel > 0) {
        setChannel(channel);
    }
    
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
}

inline void MyESPNow::setChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
}

inline void MyESPNow::setTxPower(int8_t power) {
    // Range is typically 8 to 84 (2dBm to 20dBm)
    // 80 = 20dBm (Max)
    esp_wifi_set_max_tx_power(power);
    Serial.printf("ESP-NOW: TX Power set to %d\n", power);
}

inline bool MyESPNow::addPeer(const uint8_t *macAddr, uint8_t channel, bool encrypt) {
    if (esp_now_is_peer_exist(macAddr)) {
        return true;
    }
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddr, 6);
    peerInfo.channel = channel;  
    peerInfo.encrypt = encrypt;

    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return false;
    }
    return true;
}

inline bool MyESPNow::send(const uint8_t *macAddr, const uint8_t *data, size_t len) {
    esp_err_t result = esp_now_send(macAddr, data, len);
    return result == ESP_OK;
}

inline void MyESPNow::onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (userSendCb) {
        userSendCb(status == ESP_NOW_SEND_SUCCESS);
    }
}

inline void MyESPNow::onDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
    int rssi = 0;
    if (esp_now_info) {
        if (esp_now_info->rx_ctrl) {
            rssi = esp_now_info->rx_ctrl->rssi;
        }
    }
    
    if (userRecvCb && esp_now_info) {
        userRecvCb(esp_now_info->src_addr, data, data_len, rssi);
    }
}
