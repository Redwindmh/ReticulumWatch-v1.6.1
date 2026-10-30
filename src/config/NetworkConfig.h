#pragma once

#include <stdint.h>
#include <string.h>

namespace rw::config::network {

enum class WifiMode : uint8_t {
    Off = 0,
    StationTcpClient = 1,
    AccessPointTcpServer = 2,
};

struct Profile {
    uint32_t magic{0x35545752UL}; // RWT5 little-endian marker
    uint16_t version{1};
    WifiMode mode{WifiMode::Off};
    bool enabled{false};
    uint16_t port{4242};
    char ssid[33]{};
    char password[65]{};
    char targetHost[65]{};
};

inline Profile defaultProfile() {
    Profile p{};
    p.magic = 0x35545752UL;
    p.version = 1;
    p.mode = WifiMode::Off;
    p.enabled = false;
    p.port = 4242;
    p.ssid[0] = '\0';
    p.password[0] = '\0';
    strcpy(p.targetHost, "192.168.1.1");
    return p;
}

inline bool validProfile(const Profile &p) {
    if (p.magic != 0x35545752UL || p.version != 1) return false;
    if (p.mode != WifiMode::Off && p.mode != WifiMode::StationTcpClient && p.mode != WifiMode::AccessPointTcpServer) return false;
    if (p.port == 0) return false;
    return true;
}

inline bool readyToEnable(const Profile &p) {
    if (!validProfile(p) || !p.enabled || p.mode == WifiMode::Off) return false;
    if (p.ssid[0] == '\0') return false;
    if (p.mode == WifiMode::StationTcpClient && p.targetHost[0] == '\0') return false;
    return true;
}

inline const char *modeName(WifiMode mode) {
    switch (mode) {
        case WifiMode::StationTcpClient: return "STA/TCP CLIENT";
        case WifiMode::AccessPointTcpServer: return "AP/TCP SERVER";
        default: return "OFF";
    }
}

} // namespace rw::config::network
