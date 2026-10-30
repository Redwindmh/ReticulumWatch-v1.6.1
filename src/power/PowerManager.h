#pragma once

#include <stdint.h>
#include "config/PowerConfig.h"

namespace rw::hal { class WatchHardware; }

namespace rw::power {

enum class PowerState : uint8_t { Active, ConnectedStandby, DutyCycled, DeepSleep };

class PowerManager {
public:
    void begin(rw::hal::WatchHardware &hardware);
    void configure(const rw::config::power::Profile &profile);
    void loop();

    PowerState state() const { return state_; }
    const char *stateName() const;
    const rw::config::power::Profile &profile() const { return profile_; }

    void notifyActivity();
    void displayOff();
    void displayOn();
    bool displaySleeping() const { return displaySleeping_; }
    bool uiInputBlocked() const { return displaySleeping_ || suppressTouchUntilRelease_; }
    uint32_t activeMs() const;
    uint32_t standbyMs() const;
    uint32_t crownWakeCount() const { return crownWakeCount_; }
    uint32_t liftWakeCount() const { return liftWakeCount_; }
    uint32_t standbyEntryCount() const { return standbyEntryCount_; }

private:
    rw::hal::WatchHardware *hardware_{nullptr};
    rw::config::power::Profile profile_{rw::config::power::defaultProfile()};
    PowerState state_{PowerState::Active};
    bool displaySleeping_{false};
    bool suppressTouchUntilRelease_{false};
    uint32_t lastActivityMs_{0};
    uint32_t lastTouchPollMs_{0};
    bool lastTouchState_{false};

    uint32_t displayOffMs_{0};
    uint32_t stateSinceMs_{0};
    uint32_t accumulatedActiveMs_{0};
    uint32_t accumulatedStandbyMs_{0};
    uint32_t crownWakeCount_{0};
    uint32_t liftWakeCount_{0};
    uint32_t standbyEntryCount_{0};

    // M12-build3 deliberately has no motion-polling detector here. The BMA423
    // feature engine owns lift detection and signals the ESP32 on SENSOR_INT.
    bool liftWakeDetected(uint32_t now);
};

} // namespace rw::power
