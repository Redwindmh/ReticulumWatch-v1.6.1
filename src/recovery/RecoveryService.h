#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

namespace rw::storage { class StorageService; }
namespace rw::system { class HealthMonitor; }

namespace rw::recovery {

class RecoveryService {
public:
    bool begin(rw::storage::StorageService &storage, rw::system::HealthMonitor &health);
    void loop();

    bool ready() const { return ready_; }
    const char *status() const { return status_; }
    bool backupCompleted() const { return backupCompleted_; }
    uint32_t backupToken() const { return backupToken_; }

private:
    static constexpr size_t kLineCapacity = 320;
    static constexpr size_t kPathCapacity = 160;
    static constexpr size_t kRawChunk = 96;
    static constexpr uint32_t kProtocolVersion = 1;

    void processLine(char *line);
    void printHelp() const;
    void printStatus();
    void streamBackup();
    bool streamDirectory(const char *path, uint32_t &files, uint64_t &bytes, uint32_t &manifestState);
    bool streamFile(const char *path, uint32_t &fileCrc, uint32_t &manifestState);
    bool shouldSkipBackupPath(const char *path) const;

    bool formatVolume(uint32_t token);
    bool beginRestore(const char *tokenText);
    bool beginRestoreFile(const char *path, uint32_t size, uint32_t crc);
    bool restoreData(const char *encoded);
    bool finishRestoreFile();
    bool finishRestore();
    void abortRestore(const char *reason);
    bool ensureParentDirectories(const char *path);
    bool validateRestorePath(const char *path) const;

    uint32_t crc32File(const char *path, uint32_t &sizeOut) const;
    static uint32_t crc32Update(uint32_t state, const uint8_t *data, size_t length);
    static uint32_t crc32Finalize(uint32_t state) { return state ^ 0xFFFFFFFFUL; }
    static bool parseHex32(const char *text, uint32_t &value);

    rw::storage::StorageService *storage_{nullptr};
    rw::system::HealthMonitor *health_{nullptr};
    bool ready_{false};
    const char *status_{"not started"};

    char line_[kLineCapacity]{};
    size_t lineLength_{0};

    bool backupCompleted_{false};
    uint32_t backupToken_{0};
    uint32_t backupManifest_{0};

    bool formattedThisSession_{false};
    bool restoreActive_{false};
    File restoreFile_{};
    char restorePath_[kPathCapacity]{};
    uint32_t restoreExpectedSize_{0};
    uint32_t restoreExpectedCrc_{0};
    uint32_t restoreWritten_{0};
    uint32_t restoreCrcState_{0xFFFFFFFFUL};
    uint32_t restoredFiles_{0};
    uint64_t restoredBytes_{0};
};

} // namespace rw::recovery
