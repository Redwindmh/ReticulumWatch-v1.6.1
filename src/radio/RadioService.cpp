#include "radio/RadioService.h"

#include <Arduino.h>
#include <LilyGoLib.h>
#include <pins_arduino.h>
#include <cstring>
#include <esp_system.h>

#include "hal/WatchHardware.h"

namespace {
volatile bool gPacketReceived = false;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onPacketReceived() {
    // ISR context: only set a flag. SPI, allocation and logging remain in loop().
    gPacketReceived = true;
}

bool checkRadioResult(const char *operation, int16_t result) {
    if (result == RADIOLIB_ERR_NONE) return true;
    Serial.printf("[radio] ERROR: %s failed, RadioLib code=%d\n", operation, result);
    return false;
}
}

namespace rw::radio {

void RadioService::begin(rw::hal::WatchHardware &hardware, const rw::config::radio::Profile &profile) {
    profile_ = rw::config::radio::validProfile(profile) ? profile : rw::config::radio::defaultProfile();
    Serial.printf("[radio] taking ownership of SX1262 CS=%d IRQ=%d BUSY=%d RST=%d\n",
                  LORA_CS, LORA_IRQ, LORA_BUSY, LORA_RST);

    if (!hardware.initRadioStandby()) {
        status_ = "SX1262 init FAILED";
        Serial.println("[radio] ERROR: LilyGo/RadioLib SX1262 initialization failed");
        return;
    }

    if (!applyProfile()) {
        status_ = "RF profile FAILED";
        ::radio.standby();
        return;
    }

    if (!startReceive()) {
        status_ = "SX1262 RX start FAILED";
        ::radio.standby();
        return;
    }

    ready_ = true;
    status_ = "SX1262 Reticulum RX/TX active";

    Serial.printf("[radio] profile %.3f MHz BW=%lu kHz SF=%u CR=4/%u sync=0x%02X preamble=%u TX=%d dBm\n",
                  static_cast<float>(profile_.frequencyHz) / 1000000.0f,
                  static_cast<unsigned long>(profile_.bandwidthHz / 1000UL),
                  profile_.spreadingFactor,
                  profile_.codingRate,
                  profile_.syncWord,
                  profile_.preambleSymbols,
                  profile_.txPowerDbm);
    Serial.printf("[radio] RNode-compatible on-air framing: 1-byte header, Reticulum MTU=%u, split RX/TX\n",
                  static_cast<unsigned>(rw::config::radio::kReticulumMtu));
    Serial.println("[radio] RX state explicitly re-armed after every packet and TX");
}

bool RadioService::applyProfile() {
    if (!checkRadioResult("setFrequency", ::radio.setFrequency(static_cast<float>(profile_.frequencyHz) / 1000000.0f))) return false;
    if (!checkRadioResult("setBandwidth", ::radio.setBandwidth(static_cast<float>(profile_.bandwidthHz) / 1000.0f))) return false;
    if (!checkRadioResult("setSpreadingFactor", ::radio.setSpreadingFactor(profile_.spreadingFactor))) return false;
    if (!checkRadioResult("setCodingRate", ::radio.setCodingRate(profile_.codingRate))) return false;
    if (!checkRadioResult("setSyncWord", ::radio.setSyncWord(profile_.syncWord))) return false;
    if (!checkRadioResult("setPreambleLength", ::radio.setPreambleLength(profile_.preambleSymbols))) return false;
    if (!checkRadioResult("explicitHeader", ::radio.explicitHeader())) return false;
    if (!checkRadioResult("setCRC", ::radio.setCRC(2))) return false;
    if (!checkRadioResult("setOutputPower", ::radio.setOutputPower(profile_.txPowerDbm))) return false;
    return true;
}

bool RadioService::startReceive() {
    gPacketReceived = false;
    ::radio.clearPacketReceivedAction();
    ::radio.setPacketReceivedAction(onPacketReceived);
    const int16_t result = ::radio.startReceive();
    if (!checkRadioResult("startReceive", result)) {
        ::radio.clearPacketReceivedAction();
        return false;
    }
    return true;
}

void RadioService::loop() {
    if (!ready_ || txBusy_) return;

    if (splitSequence_ != rw::config::radio::kSequenceUnset &&
        static_cast<uint32_t>(millis() - splitStartedMs_) > rw::config::radio::kSplitTimeoutMs) {
        ++stats_.splitTimeouts;
        Serial.printf("[radio:rx] split timeout seq=%u buffered=%u; discarding\n",
                      splitSequence_, static_cast<unsigned>(splitLength_));
        resetSplit();
    }

    if (!gPacketReceived) return;
    gPacketReceived = false;

    const size_t frameLength = ::radio.getPacketLength();
    if (frameLength < 2 || frameLength > rw::config::radio::kPhysicalMtu) {
        ++stats_.droppedFrames;
        Serial.printf("[radio:rx] dropped physical frame length=%u (expected 2..255)\n",
                      static_cast<unsigned>(frameLength));
        if (!startReceive()) {
            ready_ = false;
            status_ = "RX re-arm FAILED";
        }
        return;
    }

    uint8_t frame[rw::config::radio::kPhysicalMtu]{};
    const int16_t result = ::radio.readData(frame, frameLength);

    if (result == RADIOLIB_ERR_CRC_MISMATCH) {
        ++stats_.crcErrors;
        Serial.println("[radio:rx] CRC mismatch");
    } else if (result != RADIOLIB_ERR_NONE) {
        ++stats_.readErrors;
        Serial.printf("[radio:rx] readData failed, RadioLib code=%d\n", result);
    } else {
        ++stats_.physicalFrames;
        const float rssi = ::radio.getRSSI();
        const float snr = ::radio.getSNR();
        stats_.lastRssi = rssi;
        stats_.lastSnr = snr;
        stats_.haveSignalMetrics = true;
        processPhysicalFrame(frame, frameLength, rssi, snr);
    }

    // RadioLib's receive operation must be explicitly restarted after reading a
    // packet. The old M1.0.1 omitted this and could silently stop listening after one
    // inbound PHY frame. This is now part of the normal RX lifecycle.
    if (!startReceive()) {
        ready_ = false;
        status_ = "RX re-arm FAILED";
        Serial.println("[radio:rx] ERROR: could not re-arm continuous receive");
    }
}

bool RadioService::transmitPhysical(const uint8_t *frame, size_t length, uint8_t sequence, bool split, uint8_t part) {
    const int16_t result = ::radio.transmit(frame, length);
    if (result != RADIOLIB_ERR_NONE) {
        ++stats_.txErrors;
        Serial.printf("[radio:tx] ERROR: PHY transmit failed code=%d len=%u seq=%u part=%u\n",
                      result, static_cast<unsigned>(length), sequence, part);
        return false;
    }

    ++stats_.txPhysicalFrames;
    Serial.printf("[radio:tx] PHY #%lu len=%u seq=%u split=%s part=%u OK\n",
                  static_cast<unsigned long>(stats_.txPhysicalFrames),
                  static_cast<unsigned>(length), sequence, split ? "yes" : "no", part);
    return true;
}

bool RadioService::transmitReticulum(const uint8_t *data, size_t length) {
    ++stats_.txRequests;

    if (!ready_ || !data || length == 0 || length > rw::config::radio::kReticulumMtu || txBusy_) {
        ++stats_.txErrors;
        Serial.printf("[radio:tx] ERROR: rejected Reticulum TX len=%u ready=%s busy=%s\n",
                      static_cast<unsigned>(length), ready_ ? "yes" : "no", txBusy_ ? "yes" : "no");
        return false;
    }

    txBusy_ = true;
    gPacketReceived = false;
    ::radio.clearPacketReceivedAction();

    const uint8_t sequence = static_cast<uint8_t>(esp_random() & 0x0F);
    const bool split = length > rw::config::radio::kSinglePayloadMtu;
    const uint8_t header = static_cast<uint8_t>((sequence << 4) | (split ? rw::config::radio::kSplitFlag : 0));

    Serial.printf("[radio:tx] Reticulum request #%lu len=%u seq=%u split=%s\n",
                  static_cast<unsigned long>(stats_.txRequests),
                  static_cast<unsigned>(length), sequence, split ? "yes" : "no");

    bool success = true;
    uint8_t frame[rw::config::radio::kPhysicalMtu]{};

    if (!split) {
        frame[0] = header;
        memcpy(frame + 1, data, length);
        success = transmitPhysical(frame, length + 1, sequence, false, 1);
    } else {
        frame[0] = header;
        memcpy(frame + 1, data, rw::config::radio::kSinglePayloadMtu);
        success = transmitPhysical(frame, rw::config::radio::kPhysicalMtu, sequence, true, 1);

        if (success) {
            const size_t remaining = length - rw::config::radio::kSinglePayloadMtu;
            frame[0] = header;
            memcpy(frame + 1, data + rw::config::radio::kSinglePayloadMtu, remaining);
            success = transmitPhysical(frame, remaining + 1, sequence, true, 2);
        }
    }

    if (success) {
        ++stats_.txReticulumPackets;
        stats_.lastTxReticulumBytes = length;
        Serial.printf("[radio:tx] complete Reticulum payload #%lu: %u bytes\n",
                      static_cast<unsigned long>(stats_.txReticulumPackets),
                      static_cast<unsigned>(length));
    }

    const bool rxRestored = startReceive();
    if (rxRestored) {
        ++stats_.txRxRestarts;
        Serial.println("[radio:tx] SX1262 returned to interrupt RX mode");
    } else {
        ready_ = false;
        status_ = "RX restart FAILED after TX";
        ++stats_.txErrors;
        Serial.println("[radio:tx] ERROR: could not restore RX after transmission");
        success = false;
    }

    txBusy_ = false;
    return success;
}

void RadioService::processPhysicalFrame(const uint8_t *frame, size_t length, float rssi, float snr) {
    const uint8_t header = frame[0];
    const uint8_t sequence = static_cast<uint8_t>(header >> 4);
    const bool split = (header & rw::config::radio::kSplitFlag) != 0;
    const uint8_t reservedFlags = static_cast<uint8_t>(header & 0x0E);
    const uint8_t *payload = frame + rw::config::radio::kRNodeHeaderBytes;
    const size_t payloadLength = length - rw::config::radio::kRNodeHeaderBytes;

    Serial.printf("[radio:rx] PHY #%lu len=%u payload=%u seq=%u split=%s RSSI=%.1f SNR=%.1f\n",
                  static_cast<unsigned long>(stats_.physicalFrames),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(payloadLength),
                  sequence,
                  split ? "yes" : "no",
                  rssi, snr);

    if (reservedFlags != 0) {
        ++stats_.droppedFrames;
        Serial.printf("[radio:rx] unsupported RNode header flags 0x%02X; dropped\n", reservedFlags);
        return;
    }

    if (!split) {
        // Match the proven RatSpeak RNode framing state machine: a non-split
        // packet while awaiting split frame 2 invalidates the partial assembly.
        if (splitSequence_ != rw::config::radio::kSequenceUnset) {
            ++stats_.splitRestarts;
            Serial.println("[radio:rx] non-split frame interrupted pending split; discarding partial");
            resetSplit();
        }
        queueComplete(payload, payloadLength, rssi, snr);
        return;
    }

    if (splitSequence_ == rw::config::radio::kSequenceUnset || splitSequence_ != sequence) {
        if (splitSequence_ != rw::config::radio::kSequenceUnset) {
            ++stats_.splitRestarts;
            Serial.printf("[radio:rx] split seq changed %u -> %u; replacing incomplete packet\n",
                          splitSequence_, sequence);
        }

        memcpy(splitBuffer_, payload, payloadLength);
        splitLength_ = payloadLength;
        splitSequence_ = sequence;
        splitStartedMs_ = millis();
        ++stats_.splitStarts;
        Serial.printf("[radio:rx] split start seq=%u buffered=%u\n",
                      sequence, static_cast<unsigned>(splitLength_));
        return;
    }

    if (splitLength_ + payloadLength > sizeof(splitBuffer_)) {
        ++stats_.droppedFrames;
        Serial.printf("[radio:rx] split overflow seq=%u total=%u; dropped\n",
                      sequence, static_cast<unsigned>(splitLength_ + payloadLength));
        resetSplit();
        return;
    }

    memcpy(splitBuffer_ + splitLength_, payload, payloadLength);
    splitLength_ += payloadLength;

    Serial.printf("[radio:rx] split complete seq=%u Reticulum=%u bytes\n",
                  sequence, static_cast<unsigned>(splitLength_));
    queueComplete(splitBuffer_, splitLength_, rssi, snr);
    resetSplit();
}

void RadioService::queueComplete(const uint8_t *payload, size_t length, float rssi, float snr) {
    // M1.1 diagnostic: decode only the outer Reticulum header before handing
    // the raw packet to microReticulum. This does not decrypt or alter data.
    // HEADER_1: flags,hops,dest(16),context -> 19-byte header
    // HEADER_2: flags,hops,transport(16),dest(16),context -> 35-byte header
    if (payload && length >= 19) {
        const uint8_t flags = payload[0];
        const bool header2 = (flags & 0x40U) != 0;
        const uint8_t packetType = static_cast<uint8_t>(flags & 0x03U);
        const uint8_t destinationType = static_cast<uint8_t>((flags >> 2) & 0x03U);
        const uint8_t transportType = static_cast<uint8_t>((flags >> 4) & 0x01U);
        const size_t destOffset = header2 ? 18U : 2U;
        const size_t contextOffset = header2 ? 34U : 18U;
        if (length > contextOffset && destOffset + 16U <= length) {
            char destHex[33]{};
            static const char kHex[] = "0123456789abcdef";
            for (size_t i = 0; i < 16; ++i) {
                const uint8_t b = payload[destOffset + i];
                destHex[i * 2] = kHex[(b >> 4) & 0x0F];
                destHex[i * 2 + 1] = kHex[b & 0x0F];
            }
            Serial.printf("[rns:wire] len=%u H%u type=%u dest_type=%u transport=%u dest=%s ctx=0x%02X hops=%u\n",
                          static_cast<unsigned>(length), header2 ? 2U : 1U,
                          packetType, destinationType, transportType, destHex,
                          payload[contextOffset], payload[1]);
        } else {
            Serial.printf("[rns:wire] malformed H%u packet len=%u\n",
                          header2 ? 2U : 1U, static_cast<unsigned>(length));
        }
    }

    if (length == 0 || length > sizeof(pendingPacket_.data)) {
        ++stats_.droppedPackets;
        Serial.printf("[radio:rx] invalid reassembled payload length=%u; dropped\n",
                      static_cast<unsigned>(length));
        return;
    }

    if (pendingPacketReady_) {
        ++stats_.droppedPackets;
        Serial.println("[radio:rx] completed-packet slot busy; dropped newest packet");
        return;
    }

    memcpy(pendingPacket_.data, payload, length);
    pendingPacket_.length = length;
    pendingPacket_.rssi = rssi;
    pendingPacket_.snr = snr;
    pendingPacketReady_ = true;
    ++stats_.assembledPackets;

    Serial.printf("[radio:rx] complete Reticulum payload #%lu: %u bytes RSSI=%.1f SNR=%.1f\n",
                  static_cast<unsigned long>(stats_.assembledPackets),
                  static_cast<unsigned>(length), rssi, snr);
}

bool RadioService::takePacket(ReceivedPacket &packet) {
    if (!pendingPacketReady_) return false;
    packet = pendingPacket_;
    pendingPacketReady_ = false;
    return true;
}

void RadioService::resetSplit() {
    splitLength_ = 0;
    splitSequence_ = rw::config::radio::kSequenceUnset;
    splitStartedMs_ = 0;
}

} // namespace rw::radio
