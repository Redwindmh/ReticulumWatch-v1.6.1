#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/RadioConfig.h"

namespace rw::hal { class WatchHardware; }

namespace rw::radio {

struct ReceivedPacket {
    uint8_t data[rw::config::radio::kReticulumMtu]{};
    size_t length{0};
    float rssi{0.0f};
    float snr{0.0f};
};

struct RadioStats {
    uint32_t physicalFrames{0};
    uint32_t assembledPackets{0};
    uint32_t crcErrors{0};
    uint32_t readErrors{0};
    uint32_t droppedFrames{0};
    uint32_t droppedPackets{0};
    uint32_t splitStarts{0};
    uint32_t splitRestarts{0};
    uint32_t splitTimeouts{0};

    uint32_t txRequests{0};
    uint32_t txReticulumPackets{0};
    uint32_t txPhysicalFrames{0};
    uint32_t txErrors{0};
    uint32_t txRxRestarts{0};
    size_t lastTxReticulumBytes{0};

    float lastRssi{0.0f};
    float lastSnr{0.0f};
    bool haveSignalMetrics{false};
};

class RadioService {
public:
    void begin(rw::hal::WatchHardware &hardware, const rw::config::radio::Profile &profile);
    void loop();

    bool takePacket(ReceivedPacket &packet);
    bool transmitReticulum(const uint8_t *data, size_t length);

    const char *status() const { return status_; }
    bool ready() const { return ready_; }
    bool txBusy() const { return txBusy_; }
    bool splitRxPending() const { return splitSequence_ != rw::config::radio::kSequenceUnset; }
    const RadioStats &stats() const { return stats_; }

    uint32_t bitrate() const { return rw::config::radio::nominalBitrate(profile_); }
    uint32_t frequencyHz() const { return profile_.frequencyHz; }
    uint32_t bandwidthHz() const { return profile_.bandwidthHz; }
    uint8_t spreadingFactor() const { return profile_.spreadingFactor; }
    uint8_t codingRate() const { return profile_.codingRate; }
    uint8_t syncWord() const { return profile_.syncWord; }
    uint16_t preambleSymbols() const { return profile_.preambleSymbols; }
    int8_t txPowerDbm() const { return profile_.txPowerDbm; }
    const rw::config::radio::Profile &profile() const { return profile_; }

private:
    bool applyProfile();
    bool startReceive();
    bool transmitPhysical(const uint8_t *frame, size_t length, uint8_t sequence, bool split, uint8_t part);
    void processPhysicalFrame(const uint8_t *frame, size_t length, float rssi, float snr);
    void queueComplete(const uint8_t *payload, size_t length, float rssi, float snr);
    void resetSplit();

    bool ready_{false};
    bool txBusy_{false};
    const char *status_{"not initialized"};
    RadioStats stats_{};
    rw::config::radio::Profile profile_{rw::config::radio::defaultProfile()};

    uint8_t splitBuffer_[rw::config::radio::kReticulumMtu]{};
    size_t splitLength_{0};
    uint8_t splitSequence_{rw::config::radio::kSequenceUnset};
    uint32_t splitStartedMs_{0};

    ReceivedPacket pendingPacket_{};
    bool pendingPacketReady_{false};
};

} // namespace rw::radio
