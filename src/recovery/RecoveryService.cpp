#include "recovery/RecoveryService.h"

#include <Arduino.h>
#include <FFat.h>
#include <esp_partition.h>
#include <mbedtls/base64.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config/BuildConfig.h"
#include "storage/StorageService.h"
#include "system/HealthMonitor.h"

namespace rw::recovery {
namespace {
constexpr const char *kMountPath = "/reticulum";
constexpr const char *kPartitionLabel = "reticulum";
constexpr uint32_t kTokenXor = 0xA511B00BUL;

bool hasSuffix(const char *text, const char *suffix) {
    if (!text || !suffix) return false;
    const size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

uint64_t directoryLogicalBytes(const char *path, uint32_t &files, uint32_t depth = 0) {
    if (!path || depth > 8) return 0;
    File dir = FFat.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    uint64_t bytes = 0;
    File entry = dir.openNextFile();
    while (entry) {
        const bool isDir = entry.isDirectory();
        const size_t size = static_cast<size_t>(entry.size());
        char child[160]{};
        const char *name = entry.name();
        if (name && name[0] == '/') snprintf(child, sizeof(child), "%s", name);
        else snprintf(child, sizeof(child), "%s/%s", path, name ? name : "");
        entry.close();
        if (isDir) bytes += directoryLogicalBytes(child, files, depth + 1);
        else { bytes += size; ++files; }
        entry = dir.openNextFile();
    }
    dir.close();
    return bytes;
}
} // namespace

bool RecoveryService::begin(rw::storage::StorageService &storage, rw::system::HealthMonitor &health) {
    storage_ = &storage;
    health_ = &health;
    ready_ = true;
    status_ = "USB recovery console ready";
    Serial.println("[m11:recovery] USB recovery console ready; type 'M11 HELP' in the serial monitor");
    printStatus();
    return true;
}

void RecoveryService::loop() {
    while (Serial.available() > 0) {
        const int raw = Serial.read();
        if (raw < 0) break;
        const char c = static_cast<char>(raw);
        if (c == '\r') continue;
        if (c == '\n') {
            if (lineLength_ > 0) {
                line_[lineLength_] = '\0';
                processLine(line_);
                lineLength_ = 0;
            }
            continue;
        }
        if (lineLength_ + 1 < sizeof(line_)) line_[lineLength_++] = c;
        else {
            lineLength_ = 0;
            Serial.println("[m11:recovery] ERROR command line too long; discarded");
        }
    }
}

void RecoveryService::printHelp() const {
    Serial.println("[m11:recovery] commands:");
    Serial.println("  M11 STATUS                - filesystem/partition accounting");
    Serial.println("  M11 BACKUP                - stream verified RWB1 backup over USB serial");
    Serial.println("  M11 FORMAT <token>        - format reticulum FAT only after same-session backup");
    Serial.println("  M11 RESTORE BEGIN <token> - begin restore after FORMAT");
    Serial.println("  M11 RESTORE BEGIN FORCE   - explicit interrupted-recovery restore mode");
    Serial.println("  M11 PUT <path> <size> <crc32hex>");
    Serial.println("  M11 DATA <base64>");
    Serial.println("  M11 DONE");
    Serial.println("  M11 RESTORE END");
    Serial.println("[m11:recovery] recommended host workflow: tools/rw_recovery.py repair --port <port> --out <dir>");
}

void RecoveryService::printStatus() {
    if (storage_) storage_->refreshSpaceStats();
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, kPartitionLabel);
    uint32_t files = 0;
    const uint64_t logical = storage_ && storage_->ready() ? directoryLogicalBytes("/", files) : 0;
    const uint64_t total = storage_ ? storage_->totalBytes() : 0;
    const uint64_t free = storage_ ? storage_->freeBytes() : 0;
    const uint64_t used = total >= free ? total - free : 0;
    const uint64_t unaccounted = used > logical ? used - logical : 0;
    Serial.printf("[m11:recovery] status partition_offset=0x%lX raw_bytes=%lu fs_total=%llu fs_free=%llu fs_used=%llu logical_files=%lu logical_bytes=%llu unaccounted=%llu\n",
                  partition ? static_cast<unsigned long>(partition->address) : 0UL,
                  partition ? static_cast<unsigned long>(partition->size) : 0UL,
                  static_cast<unsigned long long>(total),
                  static_cast<unsigned long long>(free),
                  static_cast<unsigned long long>(used),
                  static_cast<unsigned long>(files),
                  static_cast<unsigned long long>(logical),
                  static_cast<unsigned long long>(unaccounted));
    if (unaccounted > 1024ULL * 1024ULL)
        Serial.println("[m11:recovery] WARNING: FAT allocation greatly exceeds traversable file data; backup/rebuild is recommended");
}

bool RecoveryService::shouldSkipBackupPath(const char *path) const {
    if (!path || path[0] != '/') return true;
    // Rebuildable accelerators and interrupted temporary writes do not belong
    // in an authoritative recovery bundle.
    if (hasSuffix(path, ".idx") || hasSuffix(path, ".idx.tmp") || hasSuffix(path, ".tmp")) return true;
    return false;
}

uint32_t RecoveryService::crc32Update(uint32_t state, const uint8_t *data, size_t length) {
    uint32_t crc = state;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
    return crc;
}

uint32_t RecoveryService::crc32File(const char *path, uint32_t &sizeOut) const {
    sizeOut = 0;
    File in = FFat.open(path, FILE_READ);
    if (!in || in.isDirectory()) { if (in) in.close(); return 0; }
    uint8_t buffer[kRawChunk]{};
    uint32_t state = 0xFFFFFFFFUL;
    while (in.available()) {
        const size_t got = in.read(buffer, sizeof(buffer));
        if (got == 0) break;
        state = crc32Update(state, buffer, got);
        sizeOut += static_cast<uint32_t>(got);
    }
    in.close();
    return crc32Finalize(state);
}

bool RecoveryService::streamFile(const char *path, uint32_t &fileCrc, uint32_t &manifestState) {
    uint32_t size = 0;
    fileCrc = crc32File(path, size);
    File in = FFat.open(path, FILE_READ);
    if (!in || in.isDirectory()) { if (in) in.close(); return false; }

    Serial.printf("RWB1 FILE %s %lu %08lX\n", path,
                  static_cast<unsigned long>(size), static_cast<unsigned long>(fileCrc));
    manifestState = crc32Update(manifestState, reinterpret_cast<const uint8_t *>(path), strlen(path));
    manifestState = crc32Update(manifestState, reinterpret_cast<const uint8_t *>(&size), sizeof(size));
    manifestState = crc32Update(manifestState, reinterpret_cast<const uint8_t *>(&fileCrc), sizeof(fileCrc));

    uint8_t raw[kRawChunk]{};
    unsigned char encoded[4 * ((kRawChunk + 2) / 3) + 4]{};
    while (in.available()) {
        const size_t got = in.read(raw, sizeof(raw));
        if (got == 0) break;
        size_t encodedBytes = 0;
        if (mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &encodedBytes, raw, got) != 0) {
            in.close();
            return false;
        }
        encoded[encodedBytes] = '\0';
        Serial.printf("RWB1 DATA %s\n", reinterpret_cast<const char *>(encoded));
    }
    in.close();
    Serial.printf("RWB1 EOF %s\n", path);
    return true;
}

bool RecoveryService::streamDirectory(const char *path, uint32_t &files, uint64_t &bytes, uint32_t &manifestState) {
    File dir = FFat.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }
    File entry = dir.openNextFile();
    while (entry) {
        const bool isDir = entry.isDirectory();
        const uint32_t entrySize = static_cast<uint32_t>(entry.size());
        char child[kPathCapacity]{};
        const char *name = entry.name();
        if (name && name[0] == '/') snprintf(child, sizeof(child), "%s", name);
        else if (strcmp(path, "/") == 0) snprintf(child, sizeof(child), "/%s", name ? name : "");
        else snprintf(child, sizeof(child), "%s/%s", path, name ? name : "");
        entry.close();

        if (isDir) {
            if (!streamDirectory(child, files, bytes, manifestState)) return false;
        } else if (!shouldSkipBackupPath(child)) {
            uint32_t crc = 0;
            if (!streamFile(child, crc, manifestState)) return false;
            ++files;
            bytes += entrySize;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return true;
}

void RecoveryService::streamBackup() {
    if (!storage_ || !storage_->ready()) {
        Serial.println("[m11:recovery] ERROR backup unavailable: persistent volume is not mounted");
        return;
    }
    if (health_) health_->setMaintenanceMode(true);
    Serial.printf("RWB1 BEGIN %lu %s\n", static_cast<unsigned long>(kProtocolVersion), rw::config::kVersion);
    uint32_t files = 0;
    uint64_t bytes = 0;
    uint32_t manifestState = 0xFFFFFFFFUL;
    const bool ok = streamDirectory("/", files, bytes, manifestState);
    backupManifest_ = crc32Finalize(manifestState);
    backupToken_ = backupManifest_ ^ kTokenXor;
    backupCompleted_ = ok;
    if (ok) {
        Serial.printf("RWB1 END %lu %llu %08lX %08lX\n",
                      static_cast<unsigned long>(files),
                      static_cast<unsigned long long>(bytes),
                      static_cast<unsigned long>(backupManifest_),
                      static_cast<unsigned long>(backupToken_));
        Serial.printf("[m11:recovery] backup complete files=%lu bytes=%llu manifest=%08lX FORMAT token=%08lX\n",
                      static_cast<unsigned long>(files), static_cast<unsigned long long>(bytes),
                      static_cast<unsigned long>(backupManifest_), static_cast<unsigned long>(backupToken_));
    } else {
        Serial.println("RWB1 ERROR stream_failed");
        Serial.println("[m11:recovery] ERROR backup stream failed; FORMAT remains locked");
    }
    Serial.flush();
    if (health_) health_->setMaintenanceMode(false);
}

bool RecoveryService::parseHex32(const char *text, uint32_t &value) {
    if (!text || strlen(text) != 8) return false;
    value = 0;
    for (size_t i = 0; i < 8; ++i) {
        const char c = text[i];
        uint8_t n = 0;
        if (c >= '0' && c <= '9') n = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') n = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') n = static_cast<uint8_t>(c - 'A' + 10);
        else return false;
        value = (value << 4U) | n;
    }
    return true;
}

bool RecoveryService::formatVolume(uint32_t token) {
    if (!backupCompleted_ || token != backupToken_) {
        Serial.println("[m11:recovery] FORMAT REFUSED: a verified backup must complete in this boot and the token must match");
        return false;
    }
    if (health_) health_->setMaintenanceMode(true);
    if (storage_) storage_->enterRecoveryMaintenance();
    FFat.end();
    char label[] = "reticulum";
    Serial.println("[m11:recovery] FORMAT armed by verified same-session backup; formatting only partition 'reticulum'");
    if (!FFat.format(false, label)) {
        Serial.println("RWR1 FORMAT FAIL");
        Serial.println("[m11:recovery] ERROR FFat format failed; recovery remains in maintenance mode");
        return false;
    }
    if (!FFat.begin(false, kMountPath, 10, kPartitionLabel)) {
        Serial.println("RWR1 FORMAT FAIL remount");
        Serial.println("[m11:recovery] ERROR formatted FFat could not be remounted");
        return false;
    }
    formattedThisSession_ = true;
    restoreActive_ = false;
    restoredFiles_ = 0;
    restoredBytes_ = 0;
    Serial.printf("RWR1 FORMAT OK %08lX\n", static_cast<unsigned long>(backupToken_));
    Serial.println("[m11:recovery] empty volume mounted in maintenance mode; begin RESTORE before rebooting");
    return true;
}

bool RecoveryService::validateRestorePath(const char *path) const {
    if (!path || path[0] != '/' || strlen(path) >= kPathCapacity) return false;
    if (strstr(path, "..") != nullptr || strstr(path, "//") != nullptr) return false;
    if (strcmp(path, "/") == 0) return false;
    return true;
}

bool RecoveryService::ensureParentDirectories(const char *path) {
    if (!validateRestorePath(path)) return false;
    char work[kPathCapacity]{};
    strncpy(work, path, sizeof(work) - 1);
    char *slash = strrchr(work, '/');
    if (!slash || slash == work) return true;
    *slash = '\0';
    for (char *p = work + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (!FFat.exists(work) && !FFat.mkdir(work)) return false;
        *p = '/';
    }
    return FFat.exists(work) || FFat.mkdir(work);
}

bool RecoveryService::beginRestore(const char *tokenText) {
    uint32_t token = 0;
    const bool force = tokenText && strcmp(tokenText, "FORCE") == 0;
    const bool tokenOk = tokenText && parseHex32(tokenText, token) && token == backupToken_;
    if (!force && (!formattedThisSession_ || !tokenOk)) {
        Serial.println("[m11:recovery] RESTORE REFUSED: use matching same-session token after FORMAT, or explicit FORCE after interrupted recovery");
        return false;
    }
    if (force) {
        if (health_) health_->setMaintenanceMode(true);
        if (storage_) storage_->enterRecoveryMaintenance();
    }
    restoreActive_ = true;
    restoredFiles_ = 0;
    restoredBytes_ = 0;
    if (restoreFile_) restoreFile_.close();
    restorePath_[0] = '\0';
    Serial.println("RWR1 RESTORE READY");
    return true;
}

bool RecoveryService::beginRestoreFile(const char *path, uint32_t size, uint32_t crc) {
    if (!restoreActive_ || !validateRestorePath(path) || restoreFile_) return false;
    if (!ensureParentDirectories(path)) return false;
    if (FFat.exists(path) && !FFat.remove(path)) return false;
    restoreFile_ = FFat.open(path, FILE_WRITE);
    if (!restoreFile_) return false;
    strncpy(restorePath_, path, sizeof(restorePath_) - 1);
    restoreExpectedSize_ = size;
    restoreExpectedCrc_ = crc;
    restoreWritten_ = 0;
    restoreCrcState_ = 0xFFFFFFFFUL;
    Serial.printf("RWR1 PUT READY %s\n", restorePath_);
    return true;
}

bool RecoveryService::restoreData(const char *encoded) {
    if (!restoreActive_ || !restoreFile_ || !encoded) return false;
    uint8_t decoded[kRawChunk + 8]{};
    size_t decodedBytes = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &decodedBytes,
                              reinterpret_cast<const unsigned char *>(encoded), strlen(encoded)) != 0) return false;
    if (restoreWritten_ + decodedBytes > restoreExpectedSize_) return false;
    const size_t wrote = restoreFile_.write(decoded, decodedBytes);
    if (wrote != decodedBytes) return false;
    restoreCrcState_ = crc32Update(restoreCrcState_, decoded, decodedBytes);
    restoreWritten_ += static_cast<uint32_t>(decodedBytes);
    return true;
}

bool RecoveryService::finishRestoreFile() {
    if (!restoreFile_) return false;
    restoreFile_.flush();
    restoreFile_.close();
    const uint32_t actualCrc = crc32Finalize(restoreCrcState_);
    const bool ok = restoreWritten_ == restoreExpectedSize_ && actualCrc == restoreExpectedCrc_;
    if (!ok) {
        Serial.printf("RWR1 FILE FAIL %s size=%lu/%lu crc=%08lX/%08lX\n", restorePath_,
                      static_cast<unsigned long>(restoreWritten_), static_cast<unsigned long>(restoreExpectedSize_),
                      static_cast<unsigned long>(actualCrc), static_cast<unsigned long>(restoreExpectedCrc_));
        FFat.remove(restorePath_);
        restorePath_[0] = '\0';
        return false;
    }
    ++restoredFiles_;
    restoredBytes_ += restoreWritten_;
    Serial.printf("RWR1 FILE OK %s\n", restorePath_);
    restorePath_[0] = '\0';
    return true;
}

bool RecoveryService::finishRestore() {
    if (!restoreActive_ || restoreFile_) return false;
    File identity = FFat.open("/rns/identity.key", FILE_READ);
    const bool identityOk = identity && !identity.isDirectory() && identity.size() == 64;
    if (identity) identity.close();
    if (!identityOk) {
        Serial.println("RWR1 RESTORE FAIL identity_missing_or_invalid");
        Serial.println("[m11:recovery] ERROR restore cannot complete without the 64-byte persistent identity");
        return false;
    }
    restoreActive_ = false;
    Serial.printf("RWR1 RESTORE OK %lu %llu\n", static_cast<unsigned long>(restoredFiles_),
                  static_cast<unsigned long long>(restoredBytes_));
    Serial.println("[m11:recovery] restore verified; restarting into restored persistent identity/history");
    Serial.flush();
    delay(250);
    ESP.restart();
    return true;
}

void RecoveryService::abortRestore(const char *reason) {
    if (restoreFile_) restoreFile_.close();
    if (restorePath_[0]) FFat.remove(restorePath_);
    restorePath_[0] = '\0';
    Serial.printf("RWR1 ERROR %s\n", reason ? reason : "restore_error");
}

void RecoveryService::processLine(char *line) {
    if (!line || strncmp(line, "M11 ", 4) != 0) return;
    char *save = nullptr;
    char *prefix = strtok_r(line, " ", &save);
    char *command = strtok_r(nullptr, " ", &save);
    (void)prefix;
    if (!command) return;

    if (strcmp(command, "HELP") == 0) { printHelp(); return; }
    if (strcmp(command, "STATUS") == 0) { printStatus(); return; }
    if (strcmp(command, "BACKUP") == 0) { streamBackup(); return; }
    if (strcmp(command, "FORMAT") == 0) {
        char *tokenText = strtok_r(nullptr, " ", &save);
        uint32_t token = 0;
        if (!parseHex32(tokenText, token) || !formatVolume(token))
            Serial.println("[m11:recovery] FORMAT usage: M11 FORMAT <8-hex-token-from-completed-backup>");
        return;
    }
    if (strcmp(command, "RESTORE") == 0) {
        char *sub = strtok_r(nullptr, " ", &save);
        if (!sub) return;
        if (strcmp(sub, "BEGIN") == 0) {
            char *tokenText = strtok_r(nullptr, " ", &save);
            if (!beginRestore(tokenText)) Serial.println("RWR1 RESTORE REFUSED");
        } else if (strcmp(sub, "END") == 0) {
            if (!finishRestore()) Serial.println("RWR1 RESTORE INCOMPLETE");
        }
        return;
    }
    if (strcmp(command, "PUT") == 0) {
        char *path = strtok_r(nullptr, " ", &save);
        char *sizeText = strtok_r(nullptr, " ", &save);
        char *crcText = strtok_r(nullptr, " ", &save);
        uint32_t crc = 0;
        const uint32_t size = sizeText ? static_cast<uint32_t>(strtoul(sizeText, nullptr, 10)) : 0;
        if (!path || !sizeText || !parseHex32(crcText, crc) || !beginRestoreFile(path, size, crc))
            abortRestore("put_failed");
        return;
    }
    if (strcmp(command, "DATA") == 0) {
        char *encoded = strtok_r(nullptr, " ", &save);
        if (!restoreData(encoded)) {
            abortRestore("data_failed");
        } else {
            // Pace host restore traffic one decoded chunk at a time. The M11
            // repair log showed USB CDC could accept a host burst faster than
            // the main loop consumed it after FORMAT, causing a restore timeout.
            Serial.printf("RWR1 DATA OK %lu\n", static_cast<unsigned long>(restoreWritten_));
        }
        return;
    }
    if (strcmp(command, "DONE") == 0) {
        if (!finishRestoreFile()) abortRestore("file_verify_failed");
        return;
    }
    Serial.println("[m11:recovery] unknown command; type M11 HELP");
}

} // namespace rw::recovery
