/**
 * espnow_lr_node.ino
 * ──────────────────────────────────────────────────────────────────────────────
 * ESP-NOW Long Range symmetric node — same firmware for both devices.
 *
 * Flow:
 *   • Each node broadcasts "hello there [#N]" after a random delay.
 *   • Receiver prints the message and sends a unicast ACK back.
 *   • Sender on ACK: prints latency stats, bumps seq, schedules next send.
 *   • If no ACK within RETRY_INTERVAL_MS: retry up to MAX_RETRIES times.
 *   • Duplicate rx-seq detected → prints warning + re-sends ACK.
 *
 * Tested with Arduino-ESP32 core ≥ 3.x (IDF 5.x) — uses new recv_cb signature.
 * ──────────────────────────────────────────────────────────────────────────────
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

// ═══════════════════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════════════════
constexpr uint8_t  ESPNOW_CHANNEL    = 1;
constexpr uint32_t DELAY_MIN_MS      = 2000;   // random delay lower bound
constexpr uint32_t DELAY_MAX_MS      = 6000;   // random delay upper bound
constexpr uint32_t RETRY_INTERVAL_MS = 500;    // wait before retry
constexpr uint8_t  MAX_RETRIES       = 3;

// ═══════════════════════════════════════════════════════════════════════════════
// Broadcast peer address
// ═══════════════════════════════════════════════════════════════════════════════
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ═══════════════════════════════════════════════════════════════════════════════
// Message structure (shared between both devices)
// ═══════════════════════════════════════════════════════════════════════════════
struct Message {
    bool     isAck;
    uint32_t seqNum;
    char     text[64];
};

// ═══════════════════════════════════════════════════════════════════════════════
// TX state
// ═══════════════════════════════════════════════════════════════════════════════
static uint32_t txSeq           = 0;
static bool     waitingForAck   = false;
static uint8_t  retryCount      = 0;

static uint32_t firstSendMs     = 0;   // timestamp of original send (seq N)
static uint32_t lastAttemptMs   = 0;   // timestamp of most recent attempt

// ═══════════════════════════════════════════════════════════════════════════════
// RX state
// ═══════════════════════════════════════════════════════════════════════════════
static uint32_t lastRxSeq = UINT32_MAX;   // sentinel: nothing received yet

// ═══════════════════════════════════════════════════════════════════════════════
// Async random delay state
// ═══════════════════════════════════════════════════════════════════════════════
static uint32_t asyncDelayStart  = 0;
static uint32_t asyncDelayTarget = 0;
static bool     asyncDelayArmed  = false;   // false = delay already consumed

// ── Start a new one-shot random delay ─────────────────────────────────────────
void startRandomDelay(uint32_t minMs = DELAY_MIN_MS, uint32_t maxMs = DELAY_MAX_MS) {
    asyncDelayStart  = millis();
    asyncDelayTarget = minMs + (esp_random() % (maxMs - minMs + 1));
    asyncDelayArmed  = true;
    Serial.printf("[DELAY] Next send in %u ms\n", asyncDelayTarget);
}

/**
 * Returns true exactly once when the armed delay has elapsed.
 * Must call startRandomDelay() again to re-arm.
 */
bool asyncDelayFired() {
    if (asyncDelayArmed && (millis() - asyncDelayStart >= asyncDelayTarget)) {
        asyncDelayArmed = false;
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Stats
// ═══════════════════════════════════════════════════════════════════════════════
struct Stats {
    uint32_t sent        = 0;
    uint32_t acked       = 0;
    uint32_t lost        = 0;
    uint32_t retries     = 0;
    uint32_t sumTotalMs  = 0;   // sum of (firstSend → ACK) latencies
    uint32_t sumRetryMs  = 0;   // sum of (lastAttempt → ACK) latencies
} stats;

static void printStats() {
    Serial.println("  ┌─ Cumulative Stats ─────────────────────────────");
    Serial.printf ("  │ Sent: %u  ACKed: %u  Lost: %u  Retries: %u\n",
                   stats.sent, stats.acked, stats.lost, stats.retries);
    if (stats.acked > 0) {
        Serial.printf("  │ Avg total latency   : %u ms\n",
                      stats.sumTotalMs / stats.acked);
        Serial.printf("  │ Avg attempt latency : %u ms\n",
                      stats.sumRetryMs / stats.acked);
    }
    Serial.println("  └────────────────────────────────────────────────");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Dynamic unicast peer helper
// ═══════════════════════════════════════════════════════════════════════════════
static void ensurePeer(const uint8_t *mac) {
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, mac, 6);
        p.channel = ESPNOW_CHANNEL;
        p.ifidx   = WIFI_IF_STA;
        p.encrypt = false;
        if (esp_now_add_peer(&p) != ESP_OK) {
            Serial.println("[PEER] Failed to add unicast peer");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Send
// ═══════════════════════════════════════════════════════════════════════════════
static void sendHello(bool isRetry) {
    Message msg = {};
    msg.isAck   = false;
    msg.seqNum  = txSeq;
    snprintf(msg.text, sizeof(msg.text), "hello there [#%u]", txSeq);

    if (!isRetry) {
        firstSendMs  = millis();
        retryCount   = 0;
        stats.sent++;
    } else {
        retryCount++;
        stats.retries++;
    }
    lastAttemptMs = millis();

    esp_err_t err = esp_now_send(BROADCAST, (uint8_t *)&msg, sizeof(msg));
    Serial.printf("[TX] %s seq #%u (attempt %u/%u) → %s\n",
                  isRetry ? "RETRY" : "SEND",
                  txSeq,
                  retryCount + 1,
                  MAX_RETRIES + 1,
                  err == ESP_OK ? "OK" : esp_err_to_name(err));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ESP-NOW callbacks
// ═══════════════════════════════════════════════════════════════════════════════

// MAC-layer delivery confirmation (not app-level ACK)
void onSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[CB ] MAC-layer send FAILED (seq #%u)\n", txSeq);
    }
}

// New-style callback (IDF 5.x / Arduino-ESP32 ≥ 3.0)
void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data, int len)
{
    if (len < (int)sizeof(Message)) return;

    Message msg = {};
    memcpy(&msg, data, sizeof(Message));

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             info->src_addr[0], info->src_addr[1], info->src_addr[2],
             info->src_addr[3], info->src_addr[4], info->src_addr[5]);

    // ── Received an ACK ───────────────────────────────────────────────────────
    if (msg.isAck) {
        if (!waitingForAck) {
            Serial.printf("[RX ] Unexpected ACK for seq #%u — ignoring\n", msg.seqNum);
            return;
        }
        if (msg.seqNum != txSeq) {
            Serial.printf("[RX ] Stale ACK seq #%u (expect #%u) — ignoring\n",
                          msg.seqNum, txSeq);
            return;
        }

        uint32_t now        = millis();
        uint32_t totalLat   = now - firstSendMs;
        uint32_t attemptLat = now - lastAttemptMs;

        waitingForAck = false;
        stats.acked++;
        stats.sumTotalMs += totalLat;
        stats.sumRetryMs += attemptLat;

        Serial.println("┌─ ACK ─────────────────────────────────────────");
        Serial.printf ("│ From      : %s\n",  macStr);
        Serial.printf ("│ Seq       : #%u\n", msg.seqNum);
        Serial.printf ("│ Attempts  : %u\n",  retryCount + 1);
        Serial.printf ("│ Total lat : %u ms  (first send → ACK)\n",  totalLat);
        Serial.printf ("│ Attempt lat:%u ms  (last attempt → ACK)\n", attemptLat);
        Serial.println("└───────────────────────────────────────────────");
        printStats();

        txSeq++;              // advance seq only after confirmed ACK
        startRandomDelay();   // schedule next hello

    // ── Received a data message → send ACK ───────────────────────────────────
    } else {
        if (msg.seqNum == lastRxSeq) {
            Serial.printf("[RX ] Duplicate seq #%u from %s — last ACK failed, re-ACKing\n",
                          msg.seqNum, macStr);
        } else {
            lastRxSeq = msg.seqNum;
            Serial.printf("[RX ] From %s | seq #%u | \"%s\"\n",
                          macStr, msg.seqNum, msg.text);
        }

        // Register sender as unicast peer so we can ACK them
        ensurePeer(info->src_addr);

        Message ack = {};
        ack.isAck  = true;
        ack.seqNum = msg.seqNum;
        snprintf(ack.text, sizeof(ack.text), "ACK");

        esp_err_t err = esp_now_send(info->src_addr,
                                     (uint8_t *)&ack, sizeof(ack));
        Serial.printf("[TX ] ACK → %s seq #%u %s\n",
                      macStr, msg.seqNum,
                      err == ESP_OK ? "sent" : esp_err_to_name(err));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  ESP-NOW Long Range Node  — booting  ║");
    Serial.println("╚══════════════════════════════════════╝");

    // 1. STA mode (no AP, no connection needed)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // 2. Enable Long Range PHY (Espressif proprietary 802.11 LR)
    //    Both devices MUST have LR enabled, otherwise frames are silently dropped.
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
        WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR
    ));

    // 3. Lock to channel 1
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    Serial.printf("[WiFi] Mode=STA | LR=ON | CH=%d | MAC=%s\n",
                  ESPNOW_CHANNEL, WiFi.macAddress().c_str());

    // 4. Init ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] esp_now_init() failed — halting");
        while (true) delay(1000);
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    // 5. Register broadcast peer
    {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, BROADCAST, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        if (esp_now_add_peer(&peer) != ESP_OK) {
            Serial.println("[FATAL] Failed to add broadcast peer — halting");
            while (true) delay(1000);
        }
    }

    Serial.println("[ESPNOW] Ready");
    startRandomDelay();   // kick off first random delay
}

// ═══════════════════════════════════════════════════════════════════════════════
// Loop
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    bool fired = asyncDelayFired();

    if (!waitingForAck) {
        // ── Idle: send when the random delay fires ────────────────────────────
        if (fired) {
            sendHello(false);
            waitingForAck = true;
        }
    } else {
        // ── Waiting for ACK ───────────────────────────────────────────────────
        uint32_t elapsed = millis() - lastAttemptMs;

        if (elapsed >= RETRY_INTERVAL_MS) {
            if (retryCount < MAX_RETRIES) {
                Serial.printf("[WAIT] No ACK after %u ms — retrying (%u/%u)\n",
                              elapsed, retryCount + 1, MAX_RETRIES);
                sendHello(true);
            } else {
                // All retries exhausted — count as lost and move on
                stats.lost++;
                Serial.printf("[FAIL] seq #%u lost after %u attempts — skipping\n",
                              txSeq, MAX_RETRIES + 1);
                printStats();
                txSeq++;
                waitingForAck = false;
                startRandomDelay();
            }
        }
    }
}
