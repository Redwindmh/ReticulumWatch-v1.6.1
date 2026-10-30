#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace rw::config::user {

enum class AnnouncePolicy : uint8_t {
    Manual = 0,
    Startup = 1,
    SixHours = 2,
};

struct Profile {
    char displayName[33]{"Reticulum Watch"};
    char quickReply1[25]{""};
    char quickReply2[25]{""};
    uint8_t brightness{150};
    AnnouncePolicy announcePolicy{AnnouncePolicy::Manual};
    bool notificationsEnabled{true};
    bool hapticsEnabled{true};
    bool peerDiscoveryVisible{true};
};

inline Profile defaultProfile() { return Profile{}; }

inline const char *announcePolicyName(AnnouncePolicy policy) {
    switch (policy) {
        case AnnouncePolicy::Startup: return "ON START";
        case AnnouncePolicy::SixHours: return "EVERY 6H";
        default: return "MANUAL";
    }
}

inline bool validProfile(const Profile &profile) {
    if (profile.displayName[0] == '\0') return false;
    if (profile.brightness < 20) return false;
    const uint8_t p = static_cast<uint8_t>(profile.announcePolicy);
    return p <= static_cast<uint8_t>(AnnouncePolicy::SixHours);
}

} // namespace rw::config::user
