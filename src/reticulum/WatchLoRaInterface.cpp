#include "reticulum/WatchLoRaInterface.h"

#include <Arduino.h>
#include "config/RadioConfig.h"
#include "radio/RadioService.h"

namespace rw::reticulum {

WatchLoRaInterface::WatchLoRaInterface(rw::radio::RadioService &radio, uint32_t bitrate)
    : RNS::InterfaceImpl("TWatchSX1262"), radio_(radio) {
    _IN = true;
    _OUT = true;
    _HW_MTU = rw::config::radio::kReticulumMtu;
    _bitrate = bitrate;
}

void WatchLoRaInterface::ingest(const uint8_t *data, size_t length) {
    if (!data || length == 0 || length > _HW_MTU) {
        Serial.printf("[rns:if] RX reject len=%u\n", static_cast<unsigned>(length));
        return;
    }

    const RNS::Bytes packet(data, length);
    RNS::InterfaceImpl::handle_incoming(packet);
}

bool WatchLoRaInterface::enqueue(const RNS::Bytes &data) {
    if (txQueue_.size() >= kTxQueueDepth) {
        Serial.println("[rns:if] TX queue full; rejecting packet");
        return false;
    }
    txQueue_.push_back(data);
    Serial.printf("[rns:if] queued TX len=%u depth=%u\n",
                  static_cast<unsigned>(data.size()),
                  static_cast<unsigned>(txQueue_.size()));
    return true;
}

bool WatchLoRaInterface::send_outgoing(const RNS::Bytes &data) {
    if (!_OUT || !radio_.ready() || data.size() == 0 || data.size() > _HW_MTU) {
        Serial.printf("[rns:if] TX reject len=%u ready=%s out=%s\n",
                      static_cast<unsigned>(data.size()),
                      radio_.ready() ? "yes" : "no", _OUT ? "yes" : "no");
        return false;
    }

    // SX1262 is half-duplex. Preserve an in-progress split receive and queue
    // Reticulum protocol replies/proofs until the physical radio is available.
    if (radio_.txBusy() || radio_.splitRxPending()) {
        if (!enqueue(data)) {
            Serial.println("[rns:if] ERROR: deferred TX queue rejected packet");
            return false;
        }
        return true;
    }

    if (!transmitNow(data)) {
        Serial.println("[rns:if] ERROR: immediate physical LoRa TX failed");
        return false;
    }
    return true;
}

bool WatchLoRaInterface::transmitNow(const RNS::Bytes &data) {
    Serial.printf("[rns:if] handing %u Reticulum bytes to SX1262\n",
                  static_cast<unsigned>(data.size()));

    if (!radio_.transmitReticulum(data.data(), data.size())) {
        Serial.println("[rns:if] ERROR: physical LoRa TX failed");
        return false;
    }

    RNS::InterfaceImpl::handle_outgoing(data);
    return true;
}

void WatchLoRaInterface::loop() {
    if (txQueue_.empty()) return;
    if (!radio_.ready() || radio_.txBusy() || radio_.splitRxPending()) return;

    RNS::Bytes packet = txQueue_.front();
    txQueue_.pop_front();
    if (!transmitNow(packet)) {
        Serial.println("[rns:if] deferred TX failed");
    }
}

} // namespace rw::reticulum
