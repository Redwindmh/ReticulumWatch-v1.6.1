#pragma once

#include <stdint.h>

namespace rw::config::transport {

struct Profile {
    // Transport mode is intentionally opt-in. Endpoint mode remains the safe
    // default for a battery-powered watch.
    bool transportEnabled{false};
};

inline constexpr Profile defaultProfile() { return Profile{}; }
inline bool validProfile(const Profile &) { return true; }
inline const char *modeName(const Profile &p) { return p.transportEnabled ? "TRANSPORT" : "ENDPOINT"; }

} // namespace rw::config::transport
