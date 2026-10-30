#include "system/HealthMonitor.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "storage/StorageService.h"

namespace rw::system {
namespace {
constexpr uint32_t kRecoveryMagic = 0x4D313052UL; // "M10R"
RTC_DATA_ATTR uint32_t gRecoveryMagic = 0;
RTC_DATA_ATTR uint32_t gRecoveryCount = 0;

const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}
} // namespace

bool HealthMonitor::begin(rw::storage::StorageService &storage) {
    storage_ = &storage;
    startedAtMs_ = millis();
    lastHeartbeatMs_ = startedAtMs_;
    lastReportMs_ = startedAtMs_;
    lastStackSampleMs_ = startedAtMs_;

    if (gRecoveryMagic == kRecoveryMagic) {
        ++gRecoveryCount;
        gRecoveryMagic = 0;
        Serial.printf("[m10:health] previous boot recovered by loop supervisor; recoveries=%lu\n",
                      static_cast<unsigned long>(gRecoveryCount));
    }
    supervisorRecoveries_ = gRecoveryCount;

    const esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[m10:health] reset_reason=%s(%d) supervisor_recoveries=%lu\n",
                  resetReasonName(reason), static_cast<int>(reason),
                  static_cast<unsigned long>(supervisorRecoveries_));

    BaseType_t created = xTaskCreatePinnedToCore(
        &HealthMonitor::supervisorEntry, "rw-health", kSupervisorStackBytes,
        this, 1, nullptr, 0);
    if (created != pdPASS) {
        status_ = "supervisor task failed";
        Serial.println("[m10:health] WARNING: supervisor task creation failed; communicator continues without autonomous loop recovery");
        return false;
    }

    ready_ = true;
    status_ = "supervisor active";
    Serial.printf("[m10:health] supervisor ready timeout=%lums grace=%lums stack=%lu core=0\n",
                  static_cast<unsigned long>(kLoopStallTimeoutMs),
                  static_cast<unsigned long>(kStartupGraceMs),
                  static_cast<unsigned long>(kSupervisorStackBytes));
    return true;
}

void HealthMonitor::setMaintenanceMode(bool enabled) {
    maintenanceMode_ = enabled;
    lastHeartbeatMs_ = millis();
    Serial.printf("[m11:recovery] loop supervisor maintenance=%s\n", enabled ? "on" : "off");
}

void HealthMonitor::heartbeat() {
    const uint32_t now = millis();
    const uint32_t previous = lastHeartbeatMs_;
    if (previous != 0) {
        const uint32_t gap = now - previous;
        if (gap > maxLoopGapMs_) maxLoopGapMs_ = gap;
    }
    lastHeartbeatMs_ = now;

    if (now - lastStackSampleMs_ >= 1000UL) {
        loopStackHighWaterWords_ = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
        lastStackSampleMs_ = now;
    }
}

void HealthMonitor::loop() {
    const uint32_t now = millis();
    if (now - lastReportMs_ < kReportIntervalMs) return;
    lastReportMs_ = now;
    if (storage_) storage_->refreshSpaceStats();
    printReport(now);
}

void HealthMonitor::printReport(uint32_t now) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t minHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t freePsram = ESP.getFreePsram();
    const uint64_t storageFree = storage_ ? storage_->freeBytes() : 0;
    Serial.printf("[m10:health] uptime=%lus heap_free=%lu heap_min=%lu psram_free=%lu storage_free=%llu loop_gap_max=%lums loop_stack_hwm=%lu words\n",
                  static_cast<unsigned long>(now / 1000UL),
                  static_cast<unsigned long>(freeHeap),
                  static_cast<unsigned long>(minHeap),
                  static_cast<unsigned long>(freePsram),
                  static_cast<unsigned long long>(storageFree),
                  static_cast<unsigned long>(maxLoopGapMs_),
                  static_cast<unsigned long>(loopStackHighWaterWords_));
}

void HealthMonitor::supervisorEntry(void *context) {
    auto *self = static_cast<HealthMonitor *>(context);
    if (self) self->supervisorLoop();
    vTaskDelete(nullptr);
}

void HealthMonitor::supervisorLoop() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kSupervisorPollMs));
        const uint32_t now = millis();
        if (maintenanceMode_) continue;
        if (now - startedAtMs_ < kStartupGraceMs) continue;
        const uint32_t heartbeatAge = now - lastHeartbeatMs_;
        if (heartbeatAge <= kLoopStallTimeoutMs) continue;

        // This path is intentionally minimal: no FFat writes and no attempt to
        // repair protocol state from the supervisor task. Existing persistent
        // state remains authoritative after reboot.
        gRecoveryMagic = kRecoveryMagic;
        Serial.printf("[m10:health] RECOVERY: loop heartbeat stale=%lums > %lums; restarting without filesystem mutation\n",
                      static_cast<unsigned long>(heartbeatAge),
                      static_cast<unsigned long>(kLoopStallTimeoutMs));
        Serial.flush();
        delay(50);
        esp_restart();
    }
}

} // namespace rw::system
