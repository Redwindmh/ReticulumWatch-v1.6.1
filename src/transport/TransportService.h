#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config/NetworkConfig.h"
#include "reticulum/RnsCompat.h"

namespace rw::storage { class StorageService; }
namespace rw::transport { class WatchTcpInterface; }

namespace rw::transport {

class TransportService {
public:
    void begin(rw::storage::StorageService &storage);
    void loop();

    bool wifiEnabled() const { return profile_.enabled && profile_.mode != rw::config::network::WifiMode::Off; }
    bool wifiConnected() const;
    bool tcpConnected() const;
    const char *status() const { return status_; }
    const rw::config::network::Profile &profile() const { return profile_; }
    uint32_t tcpRxFrames() const;
    uint32_t tcpTxFrames() const;

private:
    bool startWifi();
    bool registerTcpInterface();

    rw::config::network::Profile profile_{rw::config::network::defaultProfile()};
    RNS::Interface tcpInterface_{RNS::Type::NONE};
    WatchTcpInterface *tcpImpl_{nullptr};
    const char *status_{"disabled"};
    bool started_{false};
};

} // namespace rw::transport
