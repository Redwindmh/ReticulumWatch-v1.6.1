#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "reticulum/RnsCompat.h"

namespace rw::transport {

class WatchTcpInterface final : public RNS::InterfaceImpl {
public:
    enum class Role : uint8_t { Client, Server };

    WatchTcpInterface(Role role, const char *host, uint16_t port);
    ~WatchTcpInterface() override;

    void loop() override;
    void detach() override;
    std::string toString() const override;

    bool connected() const { return connected_; }
    uint32_t rxFrames() const { return rxFrames_; }
    uint32_t txFrames() const { return txFrames_; }
    uint32_t reconnects() const { return reconnects_; }
    const char *status() const { return status_; }

protected:
    bool send_outgoing(const RNS::Bytes &data) override;

private:
    static constexpr uint8_t kFlag = 0x7E;
    static constexpr uint8_t kEsc = 0x7D;
    static constexpr uint8_t kEscMask = 0x20;
    static constexpr size_t kMaxFrame = 620;
    static constexpr uint32_t kReconnectMs = 5000;

    void serviceClientConnect();
    void serviceServerAccept();
    void serviceRx();
    void setConnected(bool connected, const char *reason);
    bool writeFrame(const uint8_t *data, size_t length);
    void processByte(uint8_t value);

    Role role_;
    char host_[65]{};
    uint16_t port_{4242};
    WiFiClient client_;
    WiFiServer *server_{nullptr};
    bool connected_{false};
    bool inFrame_{false};
    bool escaped_{false};
    uint8_t frame_[kMaxFrame]{};
    size_t frameLength_{0};
    uint32_t lastConnectAttemptMs_{0};
    uint32_t rxFrames_{0};
    uint32_t txFrames_{0};
    uint32_t reconnects_{0};
    const char *status_{"offline"};
};

} // namespace rw::transport
