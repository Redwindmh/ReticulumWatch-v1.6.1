#include "hal/WatchHardware.h"

#include <LV_Helper.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include "config/BuildConfig.h"

namespace rw::hal {

namespace {
// Convert a Gregorian civil date to days since Unix epoch without relying on
// libc timezone configuration. Based on the well-known proleptic-Gregorian
// civil-date transform; suitable for the PCF8563's supported date range.
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

unsigned monthFromAbbrev(const char *mon) {
    static constexpr const char *kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (unsigned i = 0; i < 12; ++i) {
        if (std::strncmp(mon, kMonths[i], 3) == 0) return i + 1;
    }
    return 0;
}

int64_t buildEpochUtcApprox() {
#ifdef RW_BUILD_EPOCH_UTC
    return static_cast<int64_t>(RW_BUILD_EPOCH_UTC);
#else
    // Development fallback only. Release builds inject RW_BUILD_EPOCH_UTC from
    // PlatformIO so local compiler timezone cannot skew Reticulum timestamps.
    char monthText[4]{};
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(__DATE__, "%3s %d %d", monthText, &day, &year) != 3) return 0;
    if (std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) return 0;
    const unsigned month = monthFromAbbrev(monthText);
    if (month == 0 || day < 1 || day > 31 || year < 2020) return 0;
    return daysFromCivil(year, month, static_cast<unsigned>(day)) * 86400LL
         + hour * 3600LL + minute * 60LL + second;
#endif
}

volatile bool gPmuIrqPending = false;
volatile uint32_t gPmuIrqEdges = 0;
volatile bool gSensorIrqPending = false;
volatile uint32_t gSensorIrqEdges = 0;

void IRAM_ATTR onPmuIrq() {
    gPmuIrqPending = true;
    ++gPmuIrqEdges;
}

void IRAM_ATTR onSensorIrq() {
    gSensorIrqPending = true;
    ++gSensorIrqEdges;
}
}

void WatchHardware::begin() {
    // LilyGoLib's default begin() also starts its FFat-backed USB MSC service
    // and auto-initializes peripherals that Reticulum Watch will own itself.
    // Our custom FAT partition is intentionally labelled "reticulum", not
    // LilyGoLib's expected "ffat", so the default MSC path would retry
    // forever before the PMU/display are initialized.
    //
    // Reticulum Watch therefore asks LilyGoLib to bring up only the board services we
    // need for hardware validation: PMU, display, touch, accelerometer, RTC,
    // and haptics. Storage, LoRa, GPS, and audio are initialized later by
    // their dedicated Reticulum Watch services.
    constexpr uint32_t kDeferredHardware =
        NO_INIT_FATFS |
        NO_HW_LORA |
        NO_HW_GPS |
        NO_HW_MIC;

    Serial.println("[hal] starting LilyGo board init (MSC/LoRa/GPS/audio deferred)");
    const uint32_t probe = instance.begin(kDeferredHardware);
    Serial.printf("[hal] LilyGo board init complete; probe=0x%08lx\n",
                  static_cast<unsigned long>(probe));

    beginLvglHelper(instance);
    currentBrightness_ = rw::config::kInitialBrightness;
    instance.setBrightness(currentBrightness_);

    // The T-Watch S3 crown is the AXP2101 power key, not a spare GPIO.
    // M12 incorrectly required PMU_INT to still be LOW when the main loop
    // happened to poll it. The reference LilyGo driver instead latches the
    // falling edge and reads the PMIC status later. Mirror that model here.
    // Replacing only the PMU pin ISR is safe for ReticulumWatch because the
    // library PMU event loop is not otherwise consumed by this application.
    pinMode(PMU_INT, INPUT_PULLUP);
    instance.pmu.enableInterrupt(XPOWERS_PWR_BTN_CLICK_INT);
    instance.pmu.clearIrqStatus();
    detachInterrupt(PMU_INT);
    gPmuIrqPending = false;
    gPmuIrqEdges = 0;
    attachInterrupt(PMU_INT, onPmuIrq, FALLING);
    Serial.printf("[m12:power] crown IRQ armed pin=%d level=%d edge-latched + 250ms status fallback\n",
                  PMU_INT, digitalRead(PMU_INT));

    // LilyGoLib already configures the BMA423 feature engine for TILT and
    // WAKEUP and maps it to SENSOR_INT. ReticulumWatch does not call
    // LilyGoWatchS3::loop(), so take ownership of the physical GPIO14 edge
    // here instead of relying on LilyGoLib's internal event group. This is the
    // same state-aware architecture used by the hardware sleep path: the
    // sensor, not application polling, decides when motion is interesting.
    pinMode(SENSOR_INT, INPUT_PULLDOWN);
    (void)instance.sensor.readIrqStatus(); // discard boot-time stale feature bits
    detachInterrupt(SENSOR_INT);
    gSensorIrqPending = false;
    gSensorIrqEdges = 0;
    attachInterrupt(SENSOR_INT, onSensorIrq, RISING);
    Serial.printf("[m12:motion] BMA423 IRQ armed gpio=%d level=%d tilt_bit=0x%02x wakeup_bit=0x%02x\n",
                  SENSOR_INT, digitalRead(SENSOR_INT),
                  SensorBMA423::INT_TILT, SensorBMA423::INT_WAKEUP);
    begun_ = true;

    // The watch is currently deployed in Japan. Keep Unix/protocol time in UTC,
    // but format system-clock fallback values for the local watch face in JST.
    // This can become a user-selectable timezone in a later UI milestone.
    setenv("TZ", "JST-9", 1);
    tzset();

    // Seed protocol/system time before Reticulum starts. The PCF8563 stores
    // local wall time, while ESP/Reticulum uses Unix UTC. Convert RTC values
    // through mktime() under JST instead of treating the RTC fields as UTC.
    // A freshly compiled firmware timestamp is a useful lower bound: if the
    // RTC is materially older, advance both clocks rather than carrying a
    // stale battery-backed value indefinitely.
    struct tm rtcNow{};
    instance.rtc.getDateTime(&rtcNow);
    const int rtcYear = rtcNow.tm_year + 1900;
    const unsigned rtcMonth = static_cast<unsigned>(rtcNow.tm_mon + 1);
    const unsigned rtcDay = static_cast<unsigned>(rtcNow.tm_mday);
    const int64_t buildEpoch = buildEpochUtcApprox();
    const bool rtcDateValid = rtcYear >= 2020 && rtcMonth >= 1 && rtcMonth <= 12 && rtcDay >= 1 && rtcDay <= 31
                           && rtcNow.tm_hour >= 0 && rtcNow.tm_hour <= 23
                           && rtcNow.tm_min >= 0 && rtcNow.tm_min <= 59
                           && rtcNow.tm_sec >= 0 && rtcNow.tm_sec <= 60;
    int64_t rtcEpoch = 0;
    if (rtcDateValid) {
        struct tm rtcLocal = rtcNow;
        rtcLocal.tm_isdst = -1;
        const time_t converted = mktime(&rtcLocal);
        if (converted >= 1577836800) rtcEpoch = static_cast<int64_t>(converted);
    }

    constexpr int64_t kMaxRtcBuildSkew = 366LL * 86400LL;
    constexpr int64_t kBuildNewerThreshold = 60LL;
    const bool rtcPlausible = rtcEpoch > 0 &&
        (buildEpoch <= 0 || (rtcEpoch >= buildEpoch - kMaxRtcBuildSkew && rtcEpoch <= buildEpoch + kMaxRtcBuildSkew));
    const bool buildIsMateriallyNewer = buildEpoch > 0 && (!rtcPlausible || rtcEpoch + kBuildNewerThreshold < buildEpoch);
    const int64_t seedEpoch = buildIsMateriallyNewer ? buildEpoch : (rtcPlausible ? rtcEpoch : buildEpoch);

    if (seedEpoch > 0) {
        timeval tv{};
        tv.tv_sec = static_cast<time_t>(seedEpoch);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        systemClockDisciplined_ = true;

        if (buildIsMateriallyNewer) {
            struct tm local{};
            const time_t seed = static_cast<time_t>(seedEpoch);
            localtime_r(&seed, &local);
            instance.rtc.setDateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                     local.tm_hour, local.tm_min, local.tm_sec);
            Serial.printf("[time] RTC behind build by %llds; advanced system + PCF8563 to build epoch=%lld\n",
                          static_cast<long long>(rtcEpoch > 0 ? buildEpoch - rtcEpoch : 0),
                          static_cast<long long>(seedEpoch));
        } else if (rtcPlausible) {
            Serial.printf("[time] system clock seeded from RTC local=%04d-%02u-%02u %02d:%02d:%02d epoch=%lld\n",
                          rtcYear, rtcMonth, rtcDay, rtcNow.tm_hour, rtcNow.tm_min, rtcNow.tm_sec,
                          static_cast<long long>(seedEpoch));
        } else {
            Serial.printf("[time] RTC invalid; system clock seeded from firmware build epoch=%lld\n",
                          static_cast<long long>(seedEpoch));
        }
    } else {
        Serial.println("[time] ERROR: unable to establish wall clock; announce freshness may be rejected by peers");
    }

    const MemoryInfo mem = memoryInfo();
    Serial.println();
    Serial.println("=== Reticulum Watch hardware bring-up ===");
    Serial.printf("Flash:      %lu bytes (%lu MiB)\n",
                  static_cast<unsigned long>(mem.flashBytes),
                  static_cast<unsigned long>(mem.flashBytes / (1024UL * 1024UL)));
    Serial.printf("PSRAM:      %lu bytes (%lu MiB)\n",
                  static_cast<unsigned long>(mem.psramBytes),
                  static_cast<unsigned long>(mem.psramBytes / (1024UL * 1024UL)));
    Serial.printf("Free heap:  %lu bytes\n", static_cast<unsigned long>(mem.freeHeapBytes));
    Serial.printf("Free PSRAM: %lu bytes\n", static_cast<unsigned long>(mem.freePsramBytes));
    Serial.printf("Build flash target: %lu MiB [%s]\n",
                  static_cast<unsigned long>(rw::config::kExpectedFlashMB),
                  flashMatchesBuild() ? "MATCH" : "MISMATCH");
    Serial.printf("SX1262 pins: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d BUSY=%d IRQ=%d\n",
                  LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_BUSY, LORA_IRQ);
    Serial.println("=========================================");
}

void WatchHardware::loop() {
    const uint32_t now = millis();

    // M12-build2 crown handling. The ISR latches the PMU_INT falling edge so
    // a short pulse cannot be missed by the ~2 ms application loop. A slower
    // unconditional status read is retained as a fallback because the AXP2101
    // interrupt status itself is latched until cleared.
    bool irqPending = false;
    uint32_t irqEdges = 0;
    noInterrupts();
    irqPending = gPmuIrqPending;
    irqEdges = gPmuIrqEdges;
    if (irqPending) gPmuIrqPending = false;
    interrupts();

    const bool fallbackPoll = (now - lastPmuStatusPollMs_ >= 250U);
    if (irqPending || fallbackPoll) {
        lastPmuStatusPollMs_ = now;
        (void)instance.pmu.getIrqStatus();
        const bool shortPress = instance.pmu.isPekeyShortPressIrq();
        const bool longPress = instance.pmu.isPekeyLongPressIrq();

        if (irqPending || shortPress || longPress) {
            Serial.printf("[m12:crown] irq_edges=%lu pin=%d short=%s long=%s source=%s\n",
                          static_cast<unsigned long>(irqEdges), digitalRead(PMU_INT),
                          shortPress ? "YES" : "no", longPress ? "YES" : "no",
                          irqPending ? "edge" : "status-poll");
        }
        if (shortPress) {
            crownPressLatched_ = true;
            Serial.println("[m12:power] crown short press latched");
        }
        instance.pmu.clearIrqStatus();
    }

    // M12-build4 motion path: GPIO14 is driven by the BMA423 feature engine.
    // The ISR only latches an edge; I2C/status reads remain in normal task
    // context. This remains valid in CONNECTED-IDLE and maps directly to an
    // ESP external wake source if a future power mode enters true light sleep.
    bool sensorPending = false;
    uint32_t sensorEdges = 0;
    noInterrupts();
    sensorPending = gSensorIrqPending;
    sensorEdges = gSensorIrqEdges;
    if (sensorPending) gSensorIrqPending = false;
    interrupts();

    if (sensorPending) {
        const uint16_t status = instance.sensor.readIrqStatus();
        const bool tilt = (status & SensorBMA423::INT_TILT) != 0;
        const bool wakeup = (status & SensorBMA423::INT_WAKEUP) != 0;
        const bool relevant = tilt || wakeup;
        if (relevant || lastSensorIrqLogMs_ == 0 || now - lastSensorIrqLogMs_ >= 15000U) {
            lastSensorIrqLogMs_ = now;
            Serial.printf("[m12:motion] irq_edges=%lu gpio=%d level=%d status=0x%04x tilt=%s wakeup=%s armed=%s\n",
                          static_cast<unsigned long>(sensorEdges), SENSOR_INT, digitalRead(SENSOR_INT),
                          static_cast<unsigned>(status), tilt ? "YES" : "no", wakeup ? "YES" : "no",
                          liftWakeArmed_ ? "yes" : "no");
        }
        if (liftWakeArmed_ && relevant) {
            liftWakeLatched_ = true;
            Serial.printf("[m12:motion] hardware lift event latched status=0x%04x\n",
                          static_cast<unsigned>(status));
        }
    }


    // v1.4.0 connected-standby efficiency: lift wake is owned entirely by the
    // BMA423 feature engine + GPIO14 interrupt path, so release standby no longer
    // performs a diagnostic accelerometer I2C read every second. Keep only a
    // sparse CPU/IRQ heartbeat for field diagnostics. This preserves wake behavior
    // while eliminating continuous sensor traffic and most standby serial churn.
    if (liftWakeArmed_ && (lastSensorTelemetryMs_ == 0 || now - lastSensorTelemetryMs_ >= 60000U)) {
        lastSensorTelemetryMs_ = now;
        Serial.printf("[v1.4:power] standby heartbeat cpu=running gpio=%d irq_edges=%lu sensor_irq=armed\n",
                      digitalRead(SENSOR_INT), static_cast<unsigned long>(sensorEdges));
    }

    // Non-blocking strong notification pattern. Each DRV2605 effect runs in
    // hardware; loop() only schedules the next pulse.
    if (messageHapticPulsesRemaining_ > 0) {
        if (messageHapticNextMs_ == 0 || static_cast<int32_t>(now - messageHapticNextMs_) >= 0) {
            instance.setHapticEffects(1); // Strong Click, 100%
            instance.vibrator();
            --messageHapticPulsesRemaining_;
            messageHapticNextMs_ = messageHapticPulsesRemaining_ ? now + 150U : 0U;
        }
    }
}

MemoryInfo WatchHardware::memoryInfo() const {
    MemoryInfo info;
    info.flashBytes = ESP.getFlashChipSize();
    info.psramBytes = ESP.getPsramSize();
    info.freeHeapBytes = ESP.getFreeHeap();
    info.freePsramBytes = ESP.getFreePsram();
    return info;
}

bool WatchHardware::flashMatchesBuild() const {
    const uint32_t actualMiB = memoryInfo().flashBytes / (1024UL * 1024UL);
    return actualMiB == rw::config::kExpectedFlashMB;
}


double WatchHardware::unixTime() const {
    const time_t systemNow = time(nullptr);
    if (systemNow >= 1577836800) return static_cast<double>(systemNow);
    if (!begun_) return 0.0;

    struct tm rtcLocal{};
    instance.rtc.getDateTime(&rtcLocal);
    const int year = rtcLocal.tm_year + 1900;
    if (year < 2020 || rtcLocal.tm_mon < 0 || rtcLocal.tm_mon >= 12 ||
        rtcLocal.tm_mday < 1 || rtcLocal.tm_mday > 31) return 0.0;
    rtcLocal.tm_isdst = -1;
    const time_t converted = mktime(&rtcLocal);
    return converted >= 1577836800 ? static_cast<double>(converted) : 0.0;
}

bool WatchHardware::localDateTime(struct tm &out) const {
    out = {};
    const time_t now = time(nullptr);
    if (now >= 1577836800) {
        localtime_r(&now, &out);
        return true;
    }
    if (!begun_) return false;
    instance.rtc.getDateTime(&out);
    const int year = out.tm_year + 1900;
    return year >= 2020 && out.tm_mon >= 0 && out.tm_mon < 12 && out.tm_mday >= 1 && out.tm_mday <= 31;
}

bool WatchHardware::disciplineTime(double unixEpoch, const char *source, bool updateRtc) {
    if (unixEpoch < 1577836800.0 || unixEpoch > 4102444800.0) return false; // 2020..2100
    const time_t candidate = static_cast<time_t>(unixEpoch + 0.5);
    const time_t current = time(nullptr);
    const int64_t skew = current >= 1577836800
        ? static_cast<int64_t>(candidate) - static_cast<int64_t>(current)
        : 0;

    // Message timestamps are useful clock discipline, not an unrestricted
    // network time protocol. Limit corrections to 30 minutes once a sane
    // local clock exists, preventing arbitrary remote clock jumps.
    if (current >= 1577836800 && llabs(skew) > 1800LL) {
        Serial.printf("[time] discipline rejected source=%s skew=%llds exceeds 1800s guard\n",
                      source ? source : "unknown", static_cast<long long>(skew));
        return false;
    }
    if (current >= 1577836800 && llabs(skew) < 2LL) return true;

    timeval tv{};
    tv.tv_sec = candidate;
    settimeofday(&tv, nullptr);
    systemClockDisciplined_ = true;
    if (updateRtc && begun_) {
        struct tm local{};
        localtime_r(&candidate, &local);
        instance.rtc.setDateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                 local.tm_hour, local.tm_min, local.tm_sec);
    }
    Serial.printf("[time] disciplined source=%s skew=%llds epoch=%lld rtc=%s\n",
                  source ? source : "unknown", static_cast<long long>(skew),
                  static_cast<long long>(candidate), updateRtc ? "updated" : "unchanged");
    return true;
}

int WatchHardware::batteryPercent() const {
    if (!begun_ || !instance.pmu.isBatteryConnect()) return -1;
    return static_cast<int>(instance.pmu.getBatteryPercent());
}

bool WatchHardware::charging() const {
    return begun_ && instance.pmu.isCharging();
}

bool WatchHardware::touched() const {
    if (!begun_) return false;
    int16_t x = 0, y = 0;
    return instance.getPoint(&x, &y, 1) > 0;
}

bool WatchHardware::consumeCrownPress() {
    if (!crownPressLatched_) return false;
    crownPressLatched_ = false;
    return true;
}

bool WatchHardware::ensureAccelerometerLive() {
    // SensorLib 0.3.3 separates accelerometer configuration from powering the
    // measurement engine. M12-build3 armed the feature/IRQ layer without ever
    // proving the accelerometer itself was enabled; hardware then returned a
    // successful read containing (0,0,0) forever and GPIO14 never asserted.
    // Configure a feature-safe ODR, explicitly enable acquisition, then demand
    // a real gravity vector before lift-to-wake can be armed.
    const bool configOk = instance.sensor.configAccelerometer(
        SensorBMA423::RANGE_4G,
        SensorBMA423::ODR_100HZ,
        SensorBMA423::BW_NORMAL_AVG4,
        SensorBMA423::PERF_CONTINUOUS_MODE);
    const bool enableOk = instance.sensor.enableAccelerometer();

    delay(20); // allow fresh acceleration samples after power/config change

    bool anyRead = false;
    bool nonZero = false;
    int16_t lastX = 0, lastY = 0, lastZ = 0;
    uint8_t goodSamples = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        int16_t x = 0, y = 0, z = 0;
        const bool ok = instance.sensor.getAccelerometer(x, y, z);
        if (ok) {
            anyRead = true;
            lastX = x;
            lastY = y;
            lastZ = z;
            // A stationary watch must still measure gravity on at least one
            // axis. Tolerate tiny zero-centred noise, but never accept the
            // persistent exact-zero vector observed on M12-build3 as healthy.
            const int32_t activity = abs(static_cast<int32_t>(x))
                                   + abs(static_cast<int32_t>(y))
                                   + abs(static_cast<int32_t>(z));
            if (activity > 8) {
                nonZero = true;
                ++goodSamples;
            }
        }
        delay(10);
    }

    const bool live = configOk && enableOk && anyRead && nonZero;
    Serial.printf("[m12:motion] accel bring-up config=%s enable=%s read=%s live=%s good=%u/6 last=%d,%d,%d\n",
                  configOk ? "OK" : "FAIL", enableOk ? "OK" : "FAIL",
                  anyRead ? "OK" : "FAIL", live ? "YES" : "NO",
                  static_cast<unsigned>(goodSamples), lastX, lastY, lastZ);
    if (!live) {
        Serial.println("[m12:motion] ERROR accelerometer acquisition is not live; lift wake will NOT arm");
    }
    return live;
}

bool WatchHardware::armLiftWake(bool enabled) {
    if (!begun_) return false;

    if (!enabled) {
        liftWakeArmed_ = false;
        liftWakeLatched_ = false;
        lastSensorTelemetryMs_ = 0;
        Serial.println("[m12:motion] lift wake disarmed; sensor feature engine left intact");
        return true;
    }

    // Never claim the wake path is armed until acquisition is demonstrably
    // live. This is a hardware liveness gate, not a compile/API-success gate.
    const bool accelLive = ensureAccelerometerLive();
    const bool featureOk = accelLive && instance.sensor.enableFeature(
        SensorBMA423::FEATURE_TILT | SensorBMA423::FEATURE_WAKEUP, true);
    const bool tiltIrqOk = featureOk && instance.sensor.enableTiltIRQ();
    const bool wakeIrqOk = featureOk && instance.sensor.enableWakeupIRQ();

    // Clear only stale pre-standby status before arming our consumer. Once
    // armed, status is consumed only after a physical SENSOR_INT edge.
    const uint16_t stale = accelLive ? instance.sensor.readIrqStatus() : 0;
    noInterrupts();
    gSensorIrqPending = false;
    interrupts();
    liftWakeLatched_ = false;
    lastSensorTelemetryMs_ = 0;
    liftWakeArmed_ = accelLive && featureOk && tiltIrqOk && wakeIrqOk;

    Serial.printf("[m12:motion] standby IRQ arm accel=%s feature=%s tilt_irq=%s wake_irq=%s gpio=%d level=%d stale=0x%04x mode=CONNECTED-IDLE\n",
                  accelLive ? "LIVE" : "DEAD", featureOk ? "OK" : "FAIL",
                  tiltIrqOk ? "OK" : "FAIL", wakeIrqOk ? "OK" : "FAIL",
                  SENSOR_INT, digitalRead(SENSOR_INT), static_cast<unsigned>(stale));
    return liftWakeArmed_;
}

bool WatchHardware::consumeLiftWake() {
    if (!liftWakeLatched_) return false;
    liftWakeLatched_ = false;
    return true;
}

void WatchHardware::vibrate() {
    if (!begun_) return;

    // Notification/system haptic. Keep this deliberately separate from the
    // lighter UI tap feedback so rapid keyboard input is not throttled by the
    // notification debounce policy.
    static uint32_t lastHapticMs = 0;
    const uint32_t now = millis();
    if (lastHapticMs != 0 && (now - lastHapticMs) < 1000) return;
    lastHapticMs = now;

    // DRV2605 library effect 1: Strong Click, 100%.
    instance.setHapticEffects(1);
    instance.vibrator();
}

void WatchHardware::tapHaptic() {
    if (!begun_ || messageHapticPulsesRemaining_ > 0) return;

    // RC1: stronger general UI feedback. Effect 1 is the DRV2605 Strong Click
    // at full strength; keep a short debounce for duplicate LVGL press events.
    static uint32_t lastTapMs = 0;
    const uint32_t now = millis();
    if (lastTapMs != 0 && (now - lastTapMs) < 45) return;
    lastTapMs = now;

    instance.setHapticEffects(1);
    instance.vibrator();
}

void WatchHardware::keyHaptic() {
    if (!begun_ || messageHapticPulsesRemaining_ > 0) return;

    // Keyboard feedback gets its own debounce so a preceding UI tap does not
    // swallow the first typed character's tactile confirmation.
    static uint32_t lastKeyMs = 0;
    const uint32_t now = millis();
    if (lastKeyMs != 0 && (now - lastKeyMs) < 28) return;
    lastKeyMs = now;

    instance.setHapticEffects(1);
    instance.vibrator();
}

void WatchHardware::messageHaptic(bool incoming) {
    if (!begun_) return;
    messageHapticPulsesRemaining_ = incoming ? 3 : 2;
    messageHapticNextMs_ = 0;
    Serial.printf("[m10:notify] strong haptic queued type=%s pulses=%u\n",
                  incoming ? "RX" : "TX", static_cast<unsigned>(messageHapticPulsesRemaining_));
}

bool WatchHardware::initRadioStandby() {
    if (!begun_) return false;

    // The board begin() deliberately deferred LoRa. RadioService owns it.
    instance.powerControl(POWER_RADIO, true);
    delay(20);

    if (!instance.initLoRa()) {
        return false;
    }

    // M1.1 lets RadioService configure SX1262 RX/TX after this known-good standby bring-up. This hardware-init method itself never starts RX and never transmits.
    const int16_t standbyResult = radio.standby();
    if (standbyResult != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] standby failed, RadioLib code=%d\n", standbyResult);
        return false;
    }

    return true;
}

void WatchHardware::setBrightness(uint8_t level) {
    currentBrightness_ = level;
    if (begun_) instance.setBrightness(level);
}

void WatchHardware::sleepDisplay() {
    if (begun_) {
        // LilyGoWatchS3::sleepDisplay() is currently a no-op. The library's
        // own light-sleep path gates this exact PMU channel to blank the watch.
        instance.powerControl(POWER_DISPLAY_BACKLIGHT, false);
    }
}

void WatchHardware::wakeDisplay() {
    if (begun_) {
        instance.powerControl(POWER_DISPLAY_BACKLIGHT, true);
        instance.setBrightness(currentBrightness_);
    }
}

[[noreturn]] void WatchHardware::restart() {
    Serial.println("[hal] software restart requested from UI");
    Serial.flush();
    delay(80);
    ESP.restart();
    while (true) { delay(1000); }
}

} // namespace rw::hal
