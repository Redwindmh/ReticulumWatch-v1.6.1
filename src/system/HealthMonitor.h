#pragma once

#include <stdint.h>

namespace rw::storage { class StorageService; }

namespace rw::system {

class HealthMonitor {
public:
    // Start only after normal subsystem/UI initialisation has completed.
    // The supervisor is intentionally independent of Arduino loopTask so a
    // wedged main loop can still be detected and recovered.
    bool begin(rw::storage::StorageService &storage);

    // Call once at the beginning of every Arduino loop iteration.
    void heartbeat();

    // Non-blocking periodic diagnostics. Filesystem accounting is refreshed
    // from loopTask, never from the supervisor task, to avoid concurrent FFat
    // access with journal writes.
    void loop();

    bool ready() const { return ready_; }
    const char *status() const { return status_; }
    uint32_t maxLoopGapMs() const { return maxLoopGapMs_; }
    uint32_t loopStackHighWaterWords() const { return loopStackHighWaterWords_; }
    uint32_t supervisorRecoveries() const { return supervisorRecoveries_; }
    void setMaintenanceMode(bool enabled);
    bool maintenanceMode() const { return maintenanceMode_; }

private:
    static constexpr uint32_t kStartupGraceMs = 30000UL;
    static constexpr uint32_t kLoopStallTimeoutMs = 30000UL;
    static constexpr uint32_t kReportIntervalMs = 60000UL;
    static constexpr uint32_t kSupervisorPollMs = 1000UL;
    static constexpr uint32_t kSupervisorStackBytes = 6144UL;

    static void supervisorEntry(void *context);
    void supervisorLoop();
    void printReport(uint32_t now);

    rw::storage::StorageService *storage_{nullptr};
    volatile uint32_t lastHeartbeatMs_{0};
    volatile uint32_t maxLoopGapMs_{0};
    volatile uint32_t loopStackHighWaterWords_{0};
    uint32_t startedAtMs_{0};
    uint32_t lastReportMs_{0};
    uint32_t lastStackSampleMs_{0};
    uint32_t supervisorRecoveries_{0};
    volatile bool maintenanceMode_{false};
    bool ready_{false};
    const char *status_{"not started"};
};

} // namespace rw::system
