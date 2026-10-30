#pragma once

#include <deque>
#include "reticulum/RnsCompat.h"

namespace rw::radio { class RadioService; }

namespace rw::reticulum {

class WatchLoRaInterface : public RNS::InterfaceImpl {
public:
    WatchLoRaInterface(rw::radio::RadioService &radio, uint32_t bitrate);

    void ingest(const uint8_t *data, size_t length);
    void loop();

protected:
    // microReticulum 0.5.0 InterfaceImpl contract: return true only when the
    // packet was accepted for immediate or deferred physical transmission.
    bool send_outgoing(const RNS::Bytes &data) override;

private:
    static constexpr size_t kTxQueueDepth = 6;

    bool enqueue(const RNS::Bytes &data);
    bool transmitNow(const RNS::Bytes &data);

    rw::radio::RadioService &radio_;
    std::deque<RNS::Bytes> txQueue_;
};

} // namespace rw::reticulum
