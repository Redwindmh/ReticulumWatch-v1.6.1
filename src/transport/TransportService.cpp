#include "transport/TransportService.h"

#include "storage/StorageService.h"
#include "transport/WatchTcpInterface.h"

namespace rw::transport {

void TransportService::begin(rw::storage::StorageService &storage) {
    profile_ = rw::config::network::defaultProfile();
    if (storage.loadNetworkProfile(profile_)) {
        Serial.printf("[m5:net] loaded network profile mode=%s enabled=%s\n",
                      rw::config::network::modeName(profile_.mode), profile_.enabled ? "yes" : "no");
    }

    if (!rw::config::network::readyToEnable(profile_)) {
        WiFi.mode(WIFI_OFF);
        status_ = profile_.enabled ? "Wi-Fi/TCP config incomplete" : "Wi-Fi/TCP disabled";
        started_ = true;
        Serial.printf("[m5:net] %s; LoRa remains the active Reticulum interface\n", status_);
        return;
    }

    if (!startWifi()) {
        status_ = "Wi-Fi start failed";
        started_ = true;
        return;
    }
    if (!registerTcpInterface()) {
        status_ = "TCP interface allocation failed";
        started_ = true;
        return;
    }
    started_ = true;
}

bool TransportService::startWifi() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (profile_.mode == rw::config::network::WifiMode::StationTcpClient) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(profile_.ssid, profile_.password);
        status_ = "Wi-Fi STA connecting";
        Serial.printf("[m5:net] Wi-Fi STA connecting to SSID '%s'; TCP target=%s:%u\n",
                      profile_.ssid, profile_.targetHost, static_cast<unsigned>(profile_.port));
        return true;
    }
    if (profile_.mode == rw::config::network::WifiMode::AccessPointTcpServer) {
        WiFi.mode(WIFI_AP);
        const bool ok = profile_.password[0]
            ? WiFi.softAP(profile_.ssid, profile_.password)
            : WiFi.softAP(profile_.ssid);
        if (!ok) {
            Serial.println("[m5:net] ERROR: Wi-Fi AP start failed");
            return false;
        }
        status_ = "Wi-Fi AP listening";
        Serial.printf("[m5:net] Wi-Fi AP '%s' started at %s; TCP server port=%u\n",
                      profile_.ssid, WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(profile_.port));
        return true;
    }
    return false;
}

bool TransportService::registerTcpInterface() {
    WatchTcpInterface::Role role = profile_.mode == rw::config::network::WifiMode::AccessPointTcpServer
        ? WatchTcpInterface::Role::Server : WatchTcpInterface::Role::Client;
    try {
        tcpImpl_ = new WatchTcpInterface(role, profile_.targetHost, profile_.port);
    } catch (...) {
        tcpImpl_ = nullptr;
    }
    if (!tcpImpl_) return false;
    tcpInterface_ = tcpImpl_;
    RNS::Transport::register_interface(tcpInterface_);
    status_ = role == WatchTcpInterface::Role::Server ? "TCP server registered" : "TCP client registered";
    Serial.println("[m5:net] registered dynamic Wi-Fi/TCP Reticulum interface (HDLC framing, MODE_FULL)");
    return true;
}

void TransportService::loop() {
    if (!started_ || !tcpImpl_) return;
    tcpImpl_->loop();
    if (profile_.mode == rw::config::network::WifiMode::StationTcpClient) {
        if (WiFi.status() == WL_CONNECTED && tcpImpl_->connected()) status_ = "Wi-Fi/TCP online";
        else if (WiFi.status() == WL_CONNECTED) status_ = tcpImpl_->status();
        else status_ = "Wi-Fi reconnecting";
    } else {
        status_ = tcpImpl_->connected() ? "AP/TCP client online" : "AP/TCP listening";
    }
}

bool TransportService::wifiConnected() const {
    if (!wifiEnabled()) return false;
    if (profile_.mode == rw::config::network::WifiMode::StationTcpClient) return WiFi.status() == WL_CONNECTED;
    return profile_.mode == rw::config::network::WifiMode::AccessPointTcpServer;
}

bool TransportService::tcpConnected() const { return tcpImpl_ && tcpImpl_->connected(); }
uint32_t TransportService::tcpRxFrames() const { return tcpImpl_ ? tcpImpl_->rxFrames() : 0; }
uint32_t TransportService::tcpTxFrames() const { return tcpImpl_ ? tcpImpl_->txFrames() : 0; }

} // namespace rw::transport
