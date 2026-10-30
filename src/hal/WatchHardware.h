#pragma once

#include <Arduino.h>
#include <LilyGoLib.h>
#include <time.h>
#include "config/BuildConfig.h"

namespace rw::hal {

struct MemoryInfo {
    uint32_t flashBytes{0};
    uint32_t psramBytes{0};
    uint32_t freeHeapBytes{0};
    uint32_t freePsramBytes{0};
};

class WatchHardware {
public:
    void begin();
    void loop();

    MemoryInfo memoryInfo() const;
    bool flashMatchesBuild() const;
    double unixTime() const;
    bool localDateTime(struct tm &out) const;
    bool disciplineTime(double unixEpoch, const char *source, bool updateRtc = true);
    int batteryPercent() const;
    bool charging() const;
    bool touched() const;
    bool consumeCrownPress();
    // M12-build3: lift-to-wake is interrupt-driven from the BMA423 SENSOR_INT
    // line. Polling raw acceleration is deliberately not part of the wake path.
    bool armLiftWake(bool enabled);
    bool consumeLiftWake();

    void vibrate();
    void tapHaptic();
    void keyHaptic();
    void messageHaptic(bool incoming);
    bool initRadioStandby();
    void setBrightness(uint8_t level);
    void sleepDisplay();
    void wakeDisplay();
    [[noreturn]] void restart();

private:
    bool ensureAccelerometerLive();
    bool begun_{false};
    bool systemClockDisciplined_{false};
    uint8_t messageHapticPulsesRemaining_{0};
    uint32_t messageHapticNextMs_{0};
    bool crownPressLatched_{false};
    bool liftWakeArmed_{false};
    bool liftWakeLatched_{false};
    uint32_t lastSensorIrqLogMs_{0};
    uint32_t lastSensorTelemetryMs_{0};
    uint32_t lastPmuStatusPollMs_{0};
    uint8_t currentBrightness_{rw::config::kInitialBrightness};
};

} // namespace rw::hal
