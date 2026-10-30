#include "power/PowerManager.h"
#include <Arduino.h>
#include "hal/WatchHardware.h"

namespace rw::power {

void PowerManager::begin(rw::hal::WatchHardware &hardware) {
    hardware_ = &hardware;
    profile_ = rw::config::power::defaultProfile();
    state_ = PowerState::Active;
    displaySleeping_ = false;
    suppressTouchUntilRelease_ = false;
    lastActivityMs_ = millis();
    lastTouchPollMs_ = 0;
    lastTouchState_ = false;
    stateSinceMs_ = lastActivityMs_;
    accumulatedActiveMs_ = 0;
    accumulatedStandbyMs_ = 0;
    crownWakeCount_ = 0;
    liftWakeCount_ = 0;
    standbyEntryCount_ = 0;
    Serial.println("[v1.4:power] manager ready: connected standby efficiency counters active");
}

void PowerManager::configure(const rw::config::power::Profile &profile) {
    profile_ = rw::config::power::validProfile(profile) ? profile : rw::config::power::defaultProfile();
    notifyActivity();
    Serial.printf("[m12:power] display standby timeout=%s lift_to_wake=%s crown=enabled touch_wake=disabled\n",
                  rw::config::power::timeoutName(profile_.displayTimeoutMs),
                  profile_.liftToWake ? "ON" : "OFF");
}

void PowerManager::notifyActivity() {
    lastActivityMs_ = millis();
    if (displaySleeping_) displayOn();
}

bool PowerManager::liftWakeDetected(uint32_t now) {
    (void)now;
    if (!hardware_ || !profile_.liftToWake || !displaySleeping_) return false;
    return hardware_->consumeLiftWake();
}

void PowerManager::loop() {
    if (!hardware_) return;
    const uint32_t now = millis();

    // The crown is a deliberate hardware display toggle in both states.
    if (hardware_->consumeCrownPress()) {
        if (displaySleeping_) {
            ++crownWakeCount_;
            displayOn();
            Serial.println("[m12:power] wake source=crown");
        } else {
            displayOff();
            Serial.println("[m12:power] sleep source=crown");
        }
        return;
    }

    if (displaySleeping_) {
        // M12 explicitly removes tap-to-wake. Touch remains a UI/activity input
        // only while the display is already on.
        if (liftWakeDetected(now)) {
            ++liftWakeCount_;
            displayOn();
            Serial.println("[m12:power] wake source=lift");
        }
        return;
    }

    // 50 Hz is plenty for activity detection and avoids hammering the touch
    // controller from the ~2 ms main loop.
    if (now - lastTouchPollMs_ >= 20U) {
        lastTouchPollMs_ = now;
        lastTouchState_ = hardware_->touched();
    }
    if (lastTouchState_) lastActivityMs_ = now;

    if (profile_.displayTimeoutMs != 0 && now - lastActivityMs_ >= profile_.displayTimeoutMs) {
        displayOff();
    }
}


uint32_t PowerManager::activeMs() const {
    const uint32_t now = millis();
    return accumulatedActiveMs_ + (displaySleeping_ ? 0U : static_cast<uint32_t>(now - stateSinceMs_));
}

uint32_t PowerManager::standbyMs() const {
    const uint32_t now = millis();
    return accumulatedStandbyMs_ + (displaySleeping_ ? static_cast<uint32_t>(now - stateSinceMs_) : 0U);
}

const char *PowerManager::stateName() const {
    switch (state_) {
        case PowerState::Active: return "ACTIVE";
        case PowerState::ConnectedStandby: return "CONNECTED";
        case PowerState::DutyCycled: return "DUTY-CYCLED";
        case PowerState::DeepSleep: return "DEEP SLEEP";
    }
    return "UNKNOWN";
}

void PowerManager::displayOff() {
    if (!hardware_ || displaySleeping_) return;

    // Arm the sensor before blanking the display. ReticulumWatch deliberately
    // stays in CONNECTED-IDLE here: the ESP32 and LoRa endpoint keep running,
    // while wake decisions come from hardware interrupt lines rather than
    // application polling. This is the state-aware boundary needed if a future
    // mode adds true ESP light sleep.
    const uint32_t now = millis();
    accumulatedActiveMs_ += static_cast<uint32_t>(now - stateSinceMs_);
    stateSinceMs_ = now;
    ++standbyEntryCount_;
    const bool liftArmed = hardware_->armLiftWake(profile_.liftToWake);
    hardware_->sleepDisplay();
    displaySleeping_ = true;
    state_ = PowerState::ConnectedStandby;
    displayOffMs_ = now;
    Serial.println("[m4:power] connected standby: display asleep; Reticulum RX remains active");
    Serial.printf("[m12:power] standby mode=CONNECTED-IDLE cpu=running poll_motion=no sensor_irq=%s\n",
                  liftArmed ? "armed" : (profile_.liftToWake ? "FAILED" : "disabled"));
}

void PowerManager::displayOn() {
    if (!hardware_) return;
    const uint32_t now = millis();
    if (displaySleeping_) {
        accumulatedStandbyMs_ += static_cast<uint32_t>(now - stateSinceMs_);
        stateSinceMs_ = now;
        hardware_->wakeDisplay();
    }
    displaySleeping_ = false;
    state_ = PowerState::Active;
    lastActivityMs_ = now;
    (void)hardware_->armLiftWake(false);
    Serial.println("[m4:power] display awake; active mode restored");
}

} // namespace rw::power
