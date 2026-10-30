#include "transport/WatchTcpInterface.h"

#include <cstring>

namespace rw::transport {

WatchTcpInterface::WatchTcpInterface(Role role, const char *host, uint16_t port)
    : RNS::InterfaceImpl(role == Role::Client ? "TWatchWiFiTCPClient" : "TWatchWiFiTCPServer"),
      role_(role), port_(port) {
    if (host) {
        strncpy(host_, host, sizeof(host_) - 1);
        host_[sizeof(host_) - 1] = '\0';
    }
    _IN = true;
    _OUT = true;
    _online = false;
    _mode = RNS::Type::Interface::MODE_FULL;
    _HW_MTU = 500;
    _FIXED_MTU = true;
    _AUTOCONFIGURE_MTU = false;
    _bitrate = 10'000'000;

    if (role_ == Role::Server) {
        server_ = new WiFiServer(port_);
        if (server_) {
            server_->begin();
            server_->setNoDelay(true);
            status_ = "listening";
            Serial.printf("[m5:tcp] AP TCP server listening on port %u\n", static_cast<unsigned>(port_));
        } else {
            status_ = "server allocation failed";
        }
    }
}

WatchTcpInterface::~WatchTcpInterface() {
    detach();
    delete server_;
    server_ = nullptr;
}

std::string WatchTcpInterface::toString() const {
    if (role_ == Role::Client) {
        return std::string("Interface[TWatchWiFiTCPClient/") + host_ + ":" + std::to_string(port_) + "]";
    }
    return std::string("Interface[TWatchWiFiTCPServer/0.0.0.0:") + std::to_string(port_) + "]";
}

void WatchTcpInterface::setConnected(bool connected, const char *reason) {
    if (connected_ == connected) {
        if (reason) status_ = reason;
        return;
    }
    connected_ = connected;
    _online = connected;
    status_ = reason ? reason : (connected ? "connected" : "offline");
    if (connected) {
        ++reconnects_;
        Serial.printf("[m5:tcp] %s connected; synthesizing Reticulum tunnel\n",
                      role_ == Role::Client ? "client" : "server peer");
        // Standard Reticulum TCPClient/TCPServer interfaces use HDLC framing
        // and synthesize a tunnel on connection/reconnection so path state can
        // recover cleanly across intermittent IP links.
        try {
            auto handle = shared_from_this();
            RNS::Interface self(handle);
            RNS::Transport::synthesize_tunnel(self);
        } catch (...) {
            Serial.println("[m5:tcp] WARNING: tunnel synthesis threw; interface remains online");
        }
    } else {
        inFrame_ = false;
        escaped_ = false;
        frameLength_ = 0;
        Serial.printf("[m5:tcp] disconnected: %s\n", status_);
    }
}

void WatchTcpInterface::serviceClientConnect() {
    if (role_ != Role::Client || connected_) return;
    if (WiFi.status() != WL_CONNECTED) {
        status_ = "waiting for Wi-Fi";
        return;
    }
    const uint32_t now = millis();
    if (lastConnectAttemptMs_ && now - lastConnectAttemptMs_ < kReconnectMs) return;
    lastConnectAttemptMs_ = now;
    Serial.printf("[m5:tcp] connecting to %s:%u...\n", host_, static_cast<unsigned>(port_));
    client_.stop();
    client_.setNoDelay(true);
    if (client_.connect(host_, port_, 3000)) {
        client_.setNoDelay(true);
        setConnected(true, "connected");
    } else {
        status_ = "TCP connect failed";
        Serial.println("[m5:tcp] TCP connect failed; retry scheduled");
    }
}

void WatchTcpInterface::serviceServerAccept() {
    if (role_ != Role::Server || !server_) return;
    if (connected_) return;
    WiFiClient candidate = server_->available();
    if (candidate) {
        client_ = candidate;
        client_.setNoDelay(true);
        setConnected(true, "client connected");
    }
}

void WatchTcpInterface::processByte(uint8_t value) {
    if (value == kFlag) {
        if (inFrame_ && frameLength_ > 0) {
            const RNS::Bytes packet(frame_, frameLength_);
            ++rxFrames_;
            RNS::InterfaceImpl::handle_incoming(packet);
            Serial.printf("[m5:tcp] RX frame #%lu len=%u\n",
                          static_cast<unsigned long>(rxFrames_),
                          static_cast<unsigned>(frameLength_));
        }
        inFrame_ = true;
        escaped_ = false;
        frameLength_ = 0;
        return;
    }
    if (!inFrame_) return;
    if (escaped_) {
        value ^= kEscMask;
        escaped_ = false;
    } else if (value == kEsc) {
        escaped_ = true;
        return;
    }
    if (frameLength_ >= sizeof(frame_)) {
        Serial.println("[m5:tcp] oversized HDLC frame dropped");
        inFrame_ = false;
        escaped_ = false;
        frameLength_ = 0;
        return;
    }
    frame_[frameLength_++] = value;
}

void WatchTcpInterface::serviceRx() {
    if (!connected_) return;
    if (!client_.connected()) {
        client_.stop();
        setConnected(false, "socket closed");
        return;
    }
    while (client_.available() > 0) {
        const int v = client_.read();
        if (v < 0) break;
        processByte(static_cast<uint8_t>(v));
    }
}

bool WatchTcpInterface::writeFrame(const uint8_t *data, size_t length) {
    if (!connected_ || !client_.connected() || !data || length == 0 || length > _HW_MTU) return false;
    uint8_t encoded[kMaxFrame * 2 + 2]{};
    size_t out = 0;
    encoded[out++] = kFlag;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t v = data[i];
        if (v == kFlag || v == kEsc) {
            encoded[out++] = kEsc;
            encoded[out++] = v ^ kEscMask;
        } else {
            encoded[out++] = v;
        }
    }
    encoded[out++] = kFlag;
    const size_t written = client_.write(encoded, out);
    if (written != out) {
        client_.stop();
        setConnected(false, "short TCP write");
        return false;
    }
    ++txFrames_;
    Serial.printf("[m5:tcp] TX frame #%lu len=%u wire=%u\n",
                  static_cast<unsigned long>(txFrames_),
                  static_cast<unsigned>(length), static_cast<unsigned>(out));
    return true;
}

bool WatchTcpInterface::send_outgoing(const RNS::Bytes &data) {
    if (!_OUT || !_online || data.size() == 0 || data.size() > _HW_MTU) return false;
    if (!writeFrame(data.data(), data.size())) return false;
    RNS::InterfaceImpl::handle_outgoing(data);
    return true;
}

void WatchTcpInterface::loop() {
    if (role_ == Role::Client) serviceClientConnect();
    else serviceServerAccept();
    serviceRx();
}

void WatchTcpInterface::detach() {
    client_.stop();
    if (server_) server_->stop();
    connected_ = false;
    _online = false;
    status_ = "detached";
}

} // namespace rw::transport
