#pragma once

#include <stdint.h>

namespace rw::config::power {

struct Profile {
    // 0 disables automatic display standby. Connected standby only turns the
    // display off; the CPU, SX1262 and Reticulum endpoint continue running.
    uint32_t displayTimeoutMs{30000};

    // M12: optional wrist-lift display wake. Disabled by default until the
    // wearer explicitly enables it. Touch never wakes a sleeping display.
    bool liftToWake{false};
};

inline constexpr Profile defaultProfile() { return Profile{}; }

inline bool validProfile(const Profile &p) {
    return p.displayTimeoutMs == 0 || p.displayTimeoutMs == 15000 ||
           p.displayTimeoutMs == 30000 || p.displayTimeoutMs == 60000;
}

inline const char *timeoutName(uint32_t timeoutMs) {
    switch (timeoutMs) {
        case 0: return "NEVER";
        case 15000: return "15 SEC";
        case 30000: return "30 SEC";
        case 60000: return "60 SEC";
        default: return "CUSTOM";
    }
}

} // namespace rw::config::power
