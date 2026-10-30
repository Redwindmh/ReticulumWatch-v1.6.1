#include "storage/StorageService.h"

#include <Arduino.h>
#include <FFat.h>
#include <FS.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace rw::storage {
namespace {
constexpr const char *kMountPath = "/reticulum";
constexpr const char *kPartitionLabel = "reticulum";
constexpr const char *kAppDir = "/app";
constexpr const char *kRadioProfilePath = "/app/radio.cfg";
constexpr const char *kPowerProfilePath = "/app/power.cfg";
constexpr const char *kNetworkProfilePath = "/app/network.cfg";
constexpr const char *kTransportProfilePath = "/app/transport.cfg";
constexpr const char *kUserProfilePath = "/app/user.cfg";
constexpr const char *kRnsDir = "/rns";
constexpr const char *kRnsInternalDir = "/rns/internal";
constexpr const char *kLxmfDir = "/lxmf";
constexpr const char *kLxmfInboxDir = "/lxmf/inbox";
constexpr const char *kLxmfOutboxDir = "/lxmf/outbox";
constexpr const char *kLxmfMetadataDir = "/lxmf/metadata";
constexpr const char *kLxmfUnreadDir = "/lxmf/metadata/unread";
constexpr const char *kLxmfDeliveryDir = "/lxmf/metadata/delivery";
constexpr const char *kLxmfPeersDir = "/lxmf/metadata/peers";
constexpr const char *kLxmfAudioDir = "/lxmf/metadata/audio";
constexpr const char *kLxmfCorruptDir = "/lxmf/corrupt";
constexpr const char *kBootCounterPath = "/app/boot_count.bin";
constexpr const char *kIdentityPath = "/rns/identity.key";
constexpr const char *kLxmfCountPath = "/lxmf/message_count.bin";
constexpr const char *kLxmfWriteTempPath = "/lxmf/inbox/.write.tmp";
constexpr const char *kLxmfAudioWriteTempPath = "/lxmf/metadata/audio/.write.tmp";
constexpr size_t kMaxLxmfAudioBytes = 512;
constexpr uint32_t kLxmfAudioMagicV1 = 0x31445541UL; // AUD1
constexpr uint16_t kLxmfAudioVersionV1 = 1;
// Preserve a small write reserve for the authoritative journal and settings.
// Under pressure, rebuildable message-ID accelerator markers are discarded
// and their rebuild is deferred; history-scan dedupe remains authoritative.

constexpr uint32_t kLxmfRecordMagicV1 = 0x31584D4CUL; // LMX1
constexpr uint32_t kLxmfRecordMagicV2 = 0x32584D4CUL; // LMX2
constexpr uint32_t kLxmfRecordMagicV3 = 0x33584D4CUL; // LMX3
constexpr uint16_t kLxmfRecordVersionV1 = 1;
constexpr uint16_t kLxmfRecordVersionV2 = 2;
constexpr uint16_t kLxmfRecordVersionV3 = 3;
constexpr uint16_t kFlagSourceKnown = 0x0001;
constexpr uint16_t kFlagSignatureVerified = 0x0002;
constexpr uint16_t kFlagOutgoing = 0x0004;
// LMX3 reserved-byte marker identifying records whose trust metadata was
// produced after the M1.4 signature-validation rebase. Pre-M1.4 records have
// zeroed reserved bytes and must never inherit the old fork's VERIFIED claim.
constexpr uint8_t kVerificationMetadataMarker = 0xA4;

#pragma pack(push, 1)
struct PersistedLxmfRecordV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sequence;
    double timestamp;
    uint8_t sourceHash[16];
    uint8_t messageId[32];
    char title[65];
    char content[257];
};

struct PersistedLxmfRecordV2 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sequence;
    double timestamp;
    uint8_t sourceHash[16];
    uint8_t destinationHash[16];
    uint8_t messageId[32];
    char title[65];
    char content[257];
};

struct PersistedLxmfRecordV3 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sequence;
    double timestamp;
    uint8_t sourceHash[16];
    uint8_t destinationHash[16];
    uint8_t messageId[32];
    uint8_t deliveryState;
    uint8_t reserved[3];
    char title[65];
    char content[257];
    uint32_t crc32;
};

struct PersistedDeliveryOverlayV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t state;
    uint8_t attempts;
    float rttSeconds;
    double updatedAt;
    char note[49];
    uint32_t crc32;
};

struct PersistedPeerV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t flags;
    uint8_t hops;
    uint8_t destinationHash[16];
    double lastSeen;
    char displayName[33];
    char announcedName[33];
    uint32_t crc32;
};

struct PersistedLxmfAudioHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t reserved;
    uint32_t encodedBytes;
    uint32_t payloadCrc32;
    uint32_t headerCrc32;
};

struct PersistedUserProfileV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t brightness;
    uint8_t announcePolicy;
    uint8_t flags;
    uint8_t reserved[3];
    char displayName[33];
    char quickReply1[25];
    char quickReply2[25];
    uint32_t crc32;
};
#pragma pack(pop)

void lxmfMessagePath(char *path, size_t capacity, uint32_t sequence) {
    snprintf(path, capacity, "/lxmf/inbox/%08lu.lxm",
             static_cast<unsigned long>(sequence));
}

void unreadMarkerPath(char *path, size_t capacity, uint32_t sequence) {
    snprintf(path, capacity, "/lxmf/metadata/unread/%08lu.unr",
             static_cast<unsigned long>(sequence));
}

void deliveryOverlayPath(char *path, size_t capacity, uint32_t sequence) {
    snprintf(path, capacity, "/lxmf/metadata/delivery/%08lu.dlv",
             static_cast<unsigned long>(sequence));
}

void audioSidecarPath(char *path, size_t capacity, uint32_t sequence) {
    snprintf(path, capacity, "/lxmf/metadata/audio/%08lu.aud",
             static_cast<unsigned long>(sequence));
}

void peerPath(char *path, size_t capacity, const uint8_t destinationHash[16]) {
    static const char hex[] = "0123456789abcdef";
    if (!path || capacity < 58 || !destinationHash) { if (path && capacity) path[0] = '\0'; return; }
    size_t pos = 0;
    const char *prefix = "/lxmf/metadata/peers/";
    const size_t prefixLen = strlen(prefix);
    memcpy(path, prefix, prefixLen); pos = prefixLen;
    for (size_t i = 0; i < 16; ++i) {
        path[pos++] = hex[(destinationHash[i] >> 4) & 0x0F];
        path[pos++] = hex[destinationHash[i] & 0x0F];
    }
    memcpy(path + pos, ".peer", 5); pos += 5; path[pos] = '\0';
}

bool parsePeerHash(const char *name, uint8_t destinationHash[16]) {
    if (!name || !destinationHash) return false;
    const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
    if (strlen(base) != 37 || strcmp(base + 32, ".peer") != 0) return false;
    auto nibble=[](char c)->int { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; };
    for (size_t i=0;i<16;++i) { int hi=nibble(base[i*2]), lo=nibble(base[i*2+1]); if(hi<0||lo<0) return false; destinationHash[i]=static_cast<uint8_t>((hi<<4)|lo); }
    return true;
}

bool messageIdIsZero(const uint8_t messageId[32]) {
    if (!messageId) return true;
    uint8_t aggregate = 0;
    for (size_t i = 0; i < 32; ++i) aggregate |= messageId[i];
    return aggregate == 0;
}

void messageIdPath(char *path, size_t capacity, const uint8_t messageId[32]) {
    static const char hex[] = "0123456789abcdef";
    if (!path || capacity == 0) return;
    constexpr size_t kPrefixLength = 15; // strlen("/lxmf/metadata/")
    constexpr size_t kRequired = kPrefixLength + 64 + 4 + 1; // + ".idx" + NUL
    if (!messageId || capacity < kRequired) {
        path[0] = '\0';
        return;
    }
    memcpy(path, "/lxmf/metadata/", kPrefixLength);
    size_t pos = kPrefixLength;
    for (size_t i = 0; i < 32; ++i) {
        path[pos++] = hex[(messageId[i] >> 4) & 0x0F];
        path[pos++] = hex[messageId[i] & 0x0F];
    }
    memcpy(path + pos, ".idx", 4);
    pos += 4;
    path[pos] = '\0';
}

uint32_t parseMessageSequence(const char *name) {
    if (!name) return 0;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (strlen(base) != 12 || strcmp(base + 8, ".lxm") != 0) return 0;

    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        if (base[i] < '0' || base[i] > '9') return 0;
        value = value * 10U + static_cast<uint32_t>(base[i] - '0');
    }
    return value;
}

uint32_t parseLeadingSequence(const char *name) {
    if (!name) return 0;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (strlen(base) < 8) return 0;
    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        if (base[i] < '0' || base[i] > '9') return 0;
        value = value * 10U + static_cast<uint32_t>(base[i] - '0');
    }
    return value;
}

bool hasSuffix(const char *name, const char *suffix) {
    if (!name || !suffix) return false;
    const size_t nameLen = strlen(name);
    const size_t suffixLen = strlen(suffix);
    return nameLen >= suffixLen && strcmp(name + nameLen - suffixLen, suffix) == 0;
}

uint32_t crc32Ieee(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

LxmfVerificationState sanitizeVerificationState(uint8_t raw) {
    if (raw > static_cast<uint8_t>(LxmfVerificationState::Failed)) return LxmfVerificationState::Unknown;
    return static_cast<LxmfVerificationState>(raw);
}

void normalizeVerificationFields(LxmfMessageRecord &record) {
    record.sourceKnown = record.verificationState != LxmfVerificationState::Unknown;
    record.signatureVerified = record.verificationState == LxmfVerificationState::Verified;
}

LxmfVerificationState legacyVerificationState(bool sourceKnown, bool outgoing) {
    // Pre-M1.4 signatureVerified bits were generated by the vulnerable
    // Identity::validate() lineage. Never promote those historical bits to
    // VERIFIED. A remembered identity is retained only as KNOWN.
    if (outgoing) return LxmfVerificationState::Verified;
    return sourceKnown ? LxmfVerificationState::Known : LxmfVerificationState::Unknown;
}

constexpr uint32_t kDeliveryOverlayMagic = 0x31564C44UL; // DLV1
constexpr uint16_t kDeliveryOverlayVersion = 1;
constexpr uint32_t kPeerMagic = 0x31524550UL; // PER1
constexpr uint16_t kPeerVersion = 1;
constexpr uint8_t kPeerFlagDiscovered = 0x01;
constexpr uint8_t kPeerFlagSavedContact = 0x02;
constexpr uint8_t kPeerFlagFavorite = 0x04;
constexpr uint8_t kPeerFlagVerifiedLxmf = 0x08;

LxmfDeliveryState sanitizeDeliveryState(uint8_t raw) {
    if (raw > static_cast<uint8_t>(LxmfDeliveryState::Retrying)) return LxmfDeliveryState::Unknown;
    return static_cast<LxmfDeliveryState>(raw);
}

bool nameEndsWith(const char *name, const char *suffix) {
    if (!name || !suffix) return false;
    const size_t n = strlen(name);
    const size_t m = strlen(suffix);
    return n >= m && strcmp(name + n - m, suffix) == 0;
}

void buildFullPath(const char *dirPath, const char *entryName, char *out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!dirPath || !entryName) return;
    if (entryName[0] == '/') snprintf(out, outSize, "%s", entryName);
    else snprintf(out, outSize, "%s/%s", dirPath, entryName);
}

void logDirectoryUsage(const char *label, const char *path) {
    File dir = FFat.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        Serial.printf("[storage:pressure] %-12s unavailable (%s)\n", label, path);
        return;
    }
    uint32_t files = 0;
    uint32_t dirs = 0;
    uint64_t logicalBytes = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) ++dirs;
        else { ++files; logicalBytes += static_cast<uint64_t>(entry.size()); }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    Serial.printf("[storage:pressure] %-12s files=%lu dirs=%lu logical=%llu bytes\n",
                  label, static_cast<unsigned long>(files), static_cast<unsigned long>(dirs),
                  static_cast<unsigned long long>(logicalBytes));
}

uint32_t purgeDerivedMessageIdIndexMarkers() {
    File dir = FFat.open(kLxmfMetadataDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    uint32_t removed = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            char name[128]{};
            strncpy(name, entry.name(), sizeof(name) - 1);
            const bool derivedIndex = nameEndsWith(name, ".idx") || nameEndsWith(name, ".idx.tmp");
            entry.close();
            if (derivedIndex) {
                char fullPath[160]{};
                buildFullPath(kLxmfMetadataDir, name, fullPath, sizeof(fullPath));
                if (fullPath[0] && FFat.remove(fullPath)) ++removed;
            }
        } else {
            entry.close();
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return removed;
}


uint32_t removeFilesInDirectory(const char *dirPath) {
    File dir = FFat.open(dirPath);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    uint32_t removed = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            char name[160]{};
            strncpy(name, entry.name(), sizeof(name) - 1);
            entry.close();
            char fullPath[192]{};
            buildFullPath(dirPath, name, fullPath, sizeof(fullPath));
            if (fullPath[0] && FFat.remove(fullPath)) ++removed;
        } else {
            entry.close();
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return removed;
}

void logStoragePressureSummary() {
    logDirectoryUsage("app", kAppDir);
    logDirectoryUsage("rns", kRnsDir);
    logDirectoryUsage("inbox", kLxmfInboxDir);
    logDirectoryUsage("metadata", kLxmfMetadataDir);
    logDirectoryUsage("unread", kLxmfUnreadDir);
    logDirectoryUsage("delivery", kLxmfDeliveryDir);
    logDirectoryUsage("peers", kLxmfPeersDir);
    logDirectoryUsage("audio", kLxmfAudioDir);
    logDirectoryUsage("corrupt", kLxmfCorruptDir);
    logDirectoryUsage("outbox", kLxmfOutboxDir);
}

} // namespace

const char *lxmfDeliveryStateName(LxmfDeliveryState state) {
    switch (state) {
        case LxmfDeliveryState::Received: return "RECEIVED";
        case LxmfDeliveryState::Submitted: return "SUBMITTED";
        case LxmfDeliveryState::Delivered: return "DELIVERED";
        case LxmfDeliveryState::Failed: return "FAILED";
        case LxmfDeliveryState::Retrying: return "RETRYING";
        default: return "UNKNOWN";
    }
}

const char *lxmfVerificationStateName(LxmfVerificationState state) {
    switch (state) {
        case LxmfVerificationState::Unknown: return "UNKNOWN";
        case LxmfVerificationState::Known: return "KNOWN";
        case LxmfVerificationState::Verified: return "VERIFIED";
        case LxmfVerificationState::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

void StorageService::begin() {
    Serial.println("[storage] mounting dedicated Reticulum FAT partition...");
    if (!FFat.begin(false, kMountPath, 10, kPartitionLabel)) {
        status_ = "mount failed (preserved)";
        Serial.println("[m10:storage] ERROR: could not mount partition 'reticulum'; automatic formatting is disabled to preserve identity/history");
        Serial.println("[m10:storage] recovery requires explicit user-directed repair or format; no destructive action taken");
        return;
    }

    totalBytes_ = FFat.totalBytes();
    freeBytes_ = FFat.freeBytes();
    if (!ensureDirectories()) { status_ = "directory init failed"; return; }
    // A reset during an atomic history write can leave only the temp file.
    // It is never authoritative and is safe to discard before journal scan.
    if (FFat.exists(kLxmfWriteTempPath)) {
        FFat.remove(kLxmfWriteTempPath);
        Serial.println("[storage] removed stale interrupted-write temp file");
    }
    // A full but readable filesystem must not disable the communicator. The
    // boot counter is diagnostic metadata, so never consume the protected
    // reserve just to increment it.
    if (freeBytes_ >= kCriticalWriteReserveBytes + 4096ULL) {
        if (!updateBootCounter()) {
            Serial.println("[m10:storage] WARNING: boot counter update failed; continuing with read-only/RAM fallback");
        }
    } else {
        Serial.printf("[m10:storage] boot counter write skipped at free=%llu; preserving emergency reserve\n",
                      static_cast<unsigned long long>(freeBytes_));
    }
    if (!loadLxmfMessageCount()) { status_ = "LXMF index recovery failed"; return; }

    ready_ = true;

    // M10 preserves the M9-build4 recovery: a hardware log showed the FFat volume at exactly
    // zero free bytes after hundreds of development boots. Message-ID .idx
    // files are rebuildable accelerators, not source-of-truth data. Under
    // storage pressure, remove only those derived markers and keep dedupe on
    // the existing journal-scan fallback. Never erase identity, journal,
    // contacts, unread/delivery overlays, or audio sidecars automatically.
    freeBytes_ = FFat.freeBytes();
    if (freeBytes_ < kStoragePressureThresholdBytes) {
        Serial.printf("[storage:pressure] WARNING free=%llu < %llu bytes; preserving authoritative/user data\n",
                      static_cast<unsigned long long>(freeBytes_),
                      static_cast<unsigned long long>(kStoragePressureThresholdBytes));
        logStoragePressureSummary();
        const uint32_t removed = purgeDerivedMessageIdIndexMarkers();
        freeBytes_ = FFat.freeBytes();
        lxmfMessageIdIndexDeferred_ = true;
        lxmfMessageIdIndexComplete_ = false;
        Serial.printf("[storage:pressure] removed %lu rebuildable message-ID marker(s); free=%llu bytes\n",
                      static_cast<unsigned long>(removed),
                      static_cast<unsigned long long>(freeBytes_));
        Serial.println("[storage:pressure] message-ID index rebuild deferred; history scan fallback active");
    } else {
        lxmfMessageIdIndexComplete_ = rebuildLxmfMessageIdIndex();
        if (!lxmfMessageIdIndexComplete_) {
            Serial.println("[storage] WARNING: message-ID metadata index rebuild incomplete; history scan fallback remains active");
        }
    }
    applyRetentionPolicy();
    unreadLxmfCount_ = rebuildUnreadState();
    trimCorruptQuarantine();

    freeBytes_ = FFat.freeBytes();
    status_ = "mounted + journal integrity OK";

    Serial.printf("[storage] mounted %s (label=%s)\n", kMountPath, kPartitionLabel);
    Serial.printf("[storage] total=%llu bytes free=%llu bytes boot_count=%lu lxmf_high_sequence=%lu\n",
                  static_cast<unsigned long long>(totalBytes_),
                  static_cast<unsigned long long>(freeBytes_),
                  static_cast<unsigned long>(bootCount_),
                  static_cast<unsigned long>(lxmfMessageCount_));
    Serial.printf("[storage] unread incoming messages=%lu\n", static_cast<unsigned long>(unreadLxmfCount_));
    Serial.println("[storage] /app /rns /lxmf/inbox + unread/delivery/peers metadata ready; LMX1+LMX2+LMX3 reader active");
    Serial.println("[m2:store] M2.6 metadata overlays ready; immutable message journal preserved");
}

void StorageService::refreshSpaceStats() {
    if (!ready_) return;
    totalBytes_ = FFat.totalBytes();
    freeBytes_ = FFat.freeBytes();
}

bool StorageService::ensureWriteHeadroom(size_t bytesNeeded, const char *operation) {
    if (!ready_) return false;
    refreshSpaceStats();
    const uint64_t required = static_cast<uint64_t>(bytesNeeded) + kCriticalWriteReserveBytes;
    if (freeBytes_ >= required) return true;
    Serial.printf("[m10:storage] WRITE BLOCKED op=%s need=%llu free=%llu reserve=%llu; existing data preserved\n",
                  operation ? operation : "unknown",
                  static_cast<unsigned long long>(bytesNeeded),
                  static_cast<unsigned long long>(freeBytes_),
                  static_cast<unsigned long long>(kCriticalWriteReserveBytes));
    return false;
}

bool StorageService::ensureDirectories() {
    if (!FFat.exists(kAppDir) && !FFat.mkdir(kAppDir)) return false;
    if (!FFat.exists(kRnsDir) && !FFat.mkdir(kRnsDir)) return false;
    if (!FFat.exists(kRnsInternalDir) && !FFat.mkdir(kRnsInternalDir)) return false;
    if (!FFat.exists(kLxmfDir) && !FFat.mkdir(kLxmfDir)) return false;
    if (!FFat.exists(kLxmfInboxDir) && !FFat.mkdir(kLxmfInboxDir)) return false;
    if (!FFat.exists(kLxmfOutboxDir) && !FFat.mkdir(kLxmfOutboxDir)) return false;
    if (!FFat.exists(kLxmfMetadataDir) && !FFat.mkdir(kLxmfMetadataDir)) return false;
    if (!FFat.exists(kLxmfUnreadDir) && !FFat.mkdir(kLxmfUnreadDir)) return false;
    if (!FFat.exists(kLxmfDeliveryDir) && !FFat.mkdir(kLxmfDeliveryDir)) return false;
    if (!FFat.exists(kLxmfPeersDir) && !FFat.mkdir(kLxmfPeersDir)) return false;
    if (!FFat.exists(kLxmfAudioDir) && !FFat.mkdir(kLxmfAudioDir)) return false;
    if (!FFat.exists(kLxmfCorruptDir) && !FFat.mkdir(kLxmfCorruptDir)) return false;
    return true;
}

bool StorageService::updateBootCounter() {
    uint32_t previous = 0;
    if (FFat.exists(kBootCounterPath)) {
        File in = FFat.open(kBootCounterPath, FILE_READ);
        if (!in) return false;
        if (in.size() == sizeof(previous)) {
            if (in.read(reinterpret_cast<uint8_t *>(&previous), sizeof(previous)) != sizeof(previous)) {
                in.close(); return false;
            }
        }
        in.close();
    }
    bootCount_ = previous + 1;
    File out = FFat.open(kBootCounterPath, FILE_WRITE);
    if (!out) return false;
    const size_t wrote = out.write(reinterpret_cast<const uint8_t *>(&bootCount_), sizeof(bootCount_));
    out.flush(); out.close();
    return wrote == sizeof(bootCount_);
}

bool StorageService::loadLxmfMessageCount() {
    uint32_t storedSequence = 0;
    bool storedSequenceValid = false;

    if (FFat.exists(kLxmfCountPath)) {
        File in = FFat.open(kLxmfCountPath, FILE_READ);
        if (in) {
            if (in.size() == sizeof(storedSequence) &&
                in.read(reinterpret_cast<uint8_t *>(&storedSequence), sizeof(storedSequence)) == sizeof(storedSequence)) {
                storedSequenceValid = true;
            }
            in.close();
        }
    }

    uint32_t discoveredMax = 0;
    uint32_t discoveredFiles = 0;
    File dir = FFat.open(kLxmfInboxDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const uint32_t sequence = parseMessageSequence(entry.name());
            if (sequence != 0) {
                ++discoveredFiles;
                if (sequence > discoveredMax) discoveredMax = sequence;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    // Message files remain authoritative. A valid stored high-water mark may be
    // slightly ahead only when the newest record was quarantined after a prior
    // boot. Never trust a wildly higher counter that could be corrupted.
    uint32_t selectedSequence = discoveredMax;
    bool acceptedStoredHighWater = false;
    if (storedSequenceValid && storedSequence >= discoveredMax) {
        const uint32_t lead = storedSequence - discoveredMax;
        const bool plausibleLead = (lead == 0) ||
            (discoveredMax > 0 && lead <= kMaxQuarantinedRecords) ||
            (discoveredMax == 0 && storedSequence <= kMaxQuarantinedRecords);
        if (plausibleLead) {
            selectedSequence = storedSequence;
            acceptedStoredHighWater = storedSequence > discoveredMax;
        }
    }

    lxmfMessageCount_ = selectedSequence;
    if (!storedSequenceValid || storedSequence != selectedSequence) {
        Serial.printf("[storage] LXMF sequence index recovered: stored=%s%lu discovered_max=%lu files=%lu selected=%lu\n",
                      storedSequenceValid ? "" : "invalid/",
                      static_cast<unsigned long>(storedSequence),
                      static_cast<unsigned long>(discoveredMax),
                      static_cast<unsigned long>(discoveredFiles),
                      static_cast<unsigned long>(selectedSequence));
        if (!saveLxmfMessageCount()) return false;
    } else if (acceptedStoredHighWater) {
        Serial.printf("[storage] LXMF sequence high-water preserved across quarantine: stored=%lu discovered_max=%lu\n",
                      static_cast<unsigned long>(storedSequence),
                      static_cast<unsigned long>(discoveredMax));
    }
    return true;
}

bool StorageService::saveLxmfMessageCount() const {
    File out = FFat.open(kLxmfCountPath, FILE_WRITE);
    if (!out) return false;
    const size_t wrote = out.write(reinterpret_cast<const uint8_t *>(&lxmfMessageCount_), sizeof(lxmfMessageCount_));
    out.flush(); out.close();
    return wrote == sizeof(lxmfMessageCount_);
}


void StorageService::enterRecoveryMaintenance() {
    ready_ = false;
    status_ = "recovery maintenance";
    Serial.println("[m11:recovery] StorageService entered maintenance mode; normal persistent writes disabled until reboot");
}

bool StorageService::wipeChatHistory() {
    if (!ready_) return false;

    // Chat history deliberately excludes identity/configuration and peer/contact
    // metadata. It includes the immutable journal and all overlays that only
    // make sense when those message records exist.
    const uint32_t inbox = removeFilesInDirectory(kLxmfInboxDir);
    const uint32_t outbox = removeFilesInDirectory(kLxmfOutboxDir);
    const uint32_t unread = removeFilesInDirectory(kLxmfUnreadDir);
    const uint32_t delivery = removeFilesInDirectory(kLxmfDeliveryDir);
    const uint32_t audio = removeFilesInDirectory(kLxmfAudioDir);
    const uint32_t corrupt = removeFilesInDirectory(kLxmfCorruptDir);
    const uint32_t indexes = purgeDerivedMessageIdIndexMarkers();
    FFat.remove(kLxmfWriteTempPath);
    FFat.remove(kLxmfAudioWriteTempPath);
    FFat.remove(kLxmfCountPath);

    lxmfMessageCount_ = 0;
    lxmfRetainedMessageCount_ = 0;
    unreadLxmfCount_ = 0;
    lxmfMessageIdIndexComplete_ = true;
    lxmfMessageIdIndexDeferred_ = false;
    memset(ramMessages_, 0, sizeof(ramMessages_));
    memset(ramAudio_, 0, sizeof(ramAudio_));
    ramMessageCount_ = 0;
    ramMessageNext_ = 0;
    ramAudioNext_ = 0;
    ramJournalMode_ = false;

    const bool counterOk = saveLxmfMessageCount();
    refreshSpaceStats();
    Serial.printf("[m11:history] wiped chat history inbox=%lu outbox=%lu unread=%lu delivery=%lu audio=%lu corrupt=%lu indexes=%lu free=%llu identity/settings/peers preserved\n",
                  static_cast<unsigned long>(inbox), static_cast<unsigned long>(outbox),
                  static_cast<unsigned long>(unread), static_cast<unsigned long>(delivery),
                  static_cast<unsigned long>(audio), static_cast<unsigned long>(corrupt),
                  static_cast<unsigned long>(indexes), static_cast<unsigned long long>(freeBytes_));
    return counterOk;
}

bool StorageService::loadIdentity(uint8_t *buffer, size_t capacity, size_t &bytesRead) const {
    bytesRead = 0;
    if (!ready_ || !buffer || capacity == 0 || !FFat.exists(kIdentityPath)) return false;
    File in = FFat.open(kIdentityPath, FILE_READ);
    if (!in) return false;
    const size_t fileSize = static_cast<size_t>(in.size());
    if (fileSize > capacity) { in.close(); return false; }
    bytesRead = in.read(buffer, fileSize);
    in.close();
    return bytesRead == fileSize;
}

bool StorageService::saveIdentity(const uint8_t *buffer, size_t length) const {
    if (!ready_ || !buffer || length == 0) return false;
    File out = FFat.open(kIdentityPath, FILE_WRITE);
    if (!out) return false;
    const size_t wrote = out.write(buffer, length);
    out.flush(); out.close();
    return wrote == length;
}

bool StorageService::readLxmfMessageIdIndex(const uint8_t messageId[32], uint32_t &sequence) const {
    sequence = 0;
    if (!ready_ || !messageId || messageIdIsZero(messageId)) return false;
    char path[96]{};
    messageIdPath(path, sizeof(path), messageId);
    if (!path[0] || !FFat.exists(path)) return false;

    File in = FFat.open(path, FILE_READ);
    if (!in) return false;
    const bool valid = in.size() == sizeof(sequence) &&
        in.read(reinterpret_cast<uint8_t *>(&sequence), sizeof(sequence)) == sizeof(sequence) &&
        sequence != 0;
    in.close();
    return valid;
}

bool StorageService::writeLxmfMessageIdIndex(const uint8_t messageId[32], uint32_t sequence) const {
    if (!ready_ || !messageId || messageIdIsZero(messageId) || sequence == 0) return false;
    char path[96]{};
    messageIdPath(path, sizeof(path), messageId);
    if (!path[0]) return false;

    uint32_t existingSequence = 0;
    if (readLxmfMessageIdIndex(messageId, existingSequence)) return true;
    if (FFat.exists(path)) FFat.remove(path); // malformed/truncated derived marker

    char tempPath[104]{};
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    if (FFat.exists(tempPath)) FFat.remove(tempPath);

    File out = FFat.open(tempPath, FILE_WRITE);
    if (!out) return false;
    const size_t wrote = out.write(reinterpret_cast<const uint8_t *>(&sequence), sizeof(sequence));
    out.flush(); out.close();
    if (wrote != sizeof(sequence)) {
        FFat.remove(tempPath);
        return false;
    }
    if (!FFat.rename(tempPath, path)) {
        FFat.remove(tempPath);
        return false;
    }
    return true;
}

bool StorageService::containsLxmfMessageId(const uint8_t messageId[32]) const {
    if (!ready_ || !messageId || messageIdIsZero(messageId)) return false;

    // Emergency RAM records participate in dedupe exactly like persisted
    // records. This prevents retransmitted Link packets from duplicating the
    // volatile journal while FFat is unavailable for writes.
    for (const auto &slot : ramMessages_) {
        if (slot.valid && !messageIdIsZero(slot.record.messageId) &&
            memcmp(slot.record.messageId, messageId, 32) == 0) return true;
    }

    uint32_t indexedSequence = 0;
    if (readLxmfMessageIdIndex(messageId, indexedSequence)) {
        // A valid marker is a deliberate dedupe tombstone while retained. Its
        // record may have just been pruned; retention removes such markers in
        // the same maintenance pass, but a reset between operations must not
        // turn an already-seen message into a second history record.
        return true;
    }
    if (lxmfMessageIdIndexComplete_) return false;

    // Metadata is an accelerator, never the source of truth. If the boot-time
    // rebuild was incomplete after an interrupted write or storage error, scan
    // readable retained history rather than trusting a missing marker.
    const uint32_t floor = lxmfMessageCount_ > kMaxRetainedMessages
        ? lxmfMessageCount_ - kMaxRetainedMessages
        : 0;
    for (uint32_t sequence = lxmfMessageCount_; sequence > floor; --sequence) {
        LxmfMessageRecord record{};
        if (!loadLxmfMessage(sequence, record)) continue;
        if (!messageIdIsZero(record.messageId) && memcmp(record.messageId, messageId, 32) == 0) return true;
    }
    return false;
}

bool StorageService::quarantineLxmfRecord(uint32_t sequence) {
    if (!ready_ || sequence == 0) return false;
    char source[48]{};
    lxmfMessagePath(source, sizeof(source), sequence);
    if (!FFat.exists(source)) return false;

    char destination[64]{};
    for (uint8_t suffix = 0; suffix < 100; ++suffix) {
        if (suffix == 0) {
            snprintf(destination, sizeof(destination), "/lxmf/corrupt/%08lu.bad",
                     static_cast<unsigned long>(sequence));
        } else {
            snprintf(destination, sizeof(destination), "/lxmf/corrupt/%08lu.%02u.bad",
                     static_cast<unsigned long>(sequence), static_cast<unsigned>(suffix));
        }
        if (!FFat.exists(destination)) break;
        destination[0] = '\0';
    }
    if (!destination[0]) return false;

    if (!FFat.rename(source, destination)) {
        Serial.printf("[storage] WARNING: could not quarantine corrupt record #%lu\n",
                      static_cast<unsigned long>(sequence));
        return false;
    }
    Serial.printf("[storage] quarantined corrupt LXMF record #%lu -> %s\n",
                  static_cast<unsigned long>(sequence), destination);
    return true;
}

void StorageService::trimCorruptQuarantine() {
    if (!ready_) return;
    uint32_t count = 0;
    uint32_t oldestSequence = UINT32_MAX;
    char oldestPath[96]{};

    File dir = FFat.open(kLxmfCorruptDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            ++count;
            const uint32_t seq = parseLeadingSequence(entry.name());
            if (seq != 0 && seq < oldestSequence) {
                oldestSequence = seq;
                strncpy(oldestPath, entry.name(), sizeof(oldestPath) - 1);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    while (count > kMaxQuarantinedRecords && oldestPath[0]) {
        char fullPath[112]{};
        if (oldestPath[0] == '/') strncpy(fullPath, oldestPath, sizeof(fullPath) - 1);
        else snprintf(fullPath, sizeof(fullPath), "%s/%s", kLxmfCorruptDir, oldestPath);
        if (!FFat.remove(fullPath)) break;
        --count;

        oldestSequence = UINT32_MAX;
        oldestPath[0] = '\0';
        File scan = FFat.open(kLxmfCorruptDir);
        if (!scan || !scan.isDirectory()) { if (scan) scan.close(); break; }
        File candidate = scan.openNextFile();
        while (candidate) {
            if (!candidate.isDirectory()) {
                const uint32_t seq = parseLeadingSequence(candidate.name());
                if (seq != 0 && seq < oldestSequence) {
                    oldestSequence = seq;
                    strncpy(oldestPath, candidate.name(), sizeof(oldestPath) - 1);
                }
            }
            candidate.close();
            candidate = scan.openNextFile();
        }
        scan.close();
    }
}

bool StorageService::rebuildLxmfMessageIdIndex() {
    if (!ready_) return false;
    uint32_t readable = 0;
    uint32_t unreadable = 0;
    uint32_t quarantined = 0;
    uint32_t added = 0;

    for (uint32_t sequence = 1; sequence <= lxmfMessageCount_; ++sequence) {
        char messagePath[48]{};
        lxmfMessagePath(messagePath, sizeof(messagePath), sequence);
        if (!FFat.exists(messagePath)) continue;

        LxmfMessageRecord record{};
        if (!loadLxmfMessage(sequence, record)) {
            ++unreadable;
            if (quarantineLxmfRecord(sequence)) ++quarantined;
            continue;
        }
        ++readable;
        if (messageIdIsZero(record.messageId)) continue;

        uint32_t indexedSequence = 0;
        if (!readLxmfMessageIdIndex(record.messageId, indexedSequence)) {
            char idPath[96]{};
            messageIdPath(idPath, sizeof(idPath), record.messageId);
            if (idPath[0] && FFat.exists(idPath)) FFat.remove(idPath);
            if (!writeLxmfMessageIdIndex(record.messageId, sequence)) return false;
            ++added;
        }
    }

    lxmfRetainedMessageCount_ = readable;
    Serial.printf("[storage] LXMF history audit: readable=%lu unreadable=%lu quarantined=%lu id_markers_added=%lu\n",
                  static_cast<unsigned long>(readable),
                  static_cast<unsigned long>(unreadable),
                  static_cast<unsigned long>(quarantined),
                  static_cast<unsigned long>(added));
    return true;
}

bool StorageService::unreadMarkerExists(uint32_t sequence) const {
    if (!ready_ || sequence == 0) return false;
    char path[64]{};
    unreadMarkerPath(path, sizeof(path), sequence);
    return FFat.exists(path);
}

bool StorageService::writeUnreadMarker(uint32_t sequence) {
    if (!ready_ || sequence == 0) return false;
    char path[64]{};
    unreadMarkerPath(path, sizeof(path), sequence);
    if (FFat.exists(path)) return true;

    File out = FFat.open(path, FILE_WRITE);
    if (!out) return false;
    constexpr uint8_t marker = 0x01;
    const size_t wrote = out.write(&marker, sizeof(marker));
    out.flush(); out.close();
    if (wrote != sizeof(marker)) {
        FFat.remove(path);
        return false;
    }
    return true;
}

uint32_t StorageService::rebuildUnreadState() {
    if (!ready_) return 0;
    uint32_t unread = 0;
    File dir = FFat.open(kLxmfUnreadDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }

    File entry = dir.openNextFile();
    while (entry) {
        char path[96]{};
        bool removeMarker = false;
        if (!entry.isDirectory() && hasSuffix(entry.name(), ".unr")) {
            strncpy(path, entry.name(), sizeof(path) - 1);
            const uint32_t sequence = parseLeadingSequence(entry.name());
            LxmfMessageRecord record{};
            if (sequence == 0 || !loadLxmfMessage(sequence, record) || record.outgoing) {
                removeMarker = true;
            } else {
                ++unread;
            }
        }
        entry.close();
        if (removeMarker && path[0]) {
            char fullPath[112]{};
            if (path[0] == '/') strncpy(fullPath, path, sizeof(fullPath) - 1);
            else snprintf(fullPath, sizeof(fullPath), "%s/%s", kLxmfUnreadDir, path);
            FFat.remove(fullPath);
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return unread;
}

uint32_t StorageService::pruneUnreadMarkersThrough(uint32_t sequenceCutoff) {
    if (!ready_ || sequenceCutoff == 0) return 0;
    uint32_t pruned = 0;
    File dir = FFat.open(kLxmfUnreadDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }

    File entry = dir.openNextFile();
    while (entry) {
        char path[96]{};
        bool removeMarker = false;
        if (!entry.isDirectory() && hasSuffix(entry.name(), ".unr")) {
            const uint32_t sequence = parseLeadingSequence(entry.name());
            if (sequence == 0 || sequence <= sequenceCutoff) {
                strncpy(path, entry.name(), sizeof(path) - 1);
                removeMarker = true;
            }
        }
        entry.close();
        if (removeMarker && path[0]) {
            char fullPath[112]{};
            if (path[0] == '/') strncpy(fullPath, path, sizeof(fullPath) - 1);
            else snprintf(fullPath, sizeof(fullPath), "%s/%s", kLxmfUnreadDir, path);
            if (FFat.remove(fullPath)) ++pruned;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    if (pruned >= unreadLxmfCount_) unreadLxmfCount_ = 0;
    else unreadLxmfCount_ -= pruned;
    return pruned;
}

uint32_t StorageService::pruneMessageIdMarkersThrough(uint32_t sequenceCutoff) {
    if (!ready_ || sequenceCutoff == 0) return 0;
    uint32_t pruned = 0;

    File dir = FFat.open(kLxmfMetadataDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    File entry = dir.openNextFile();
    while (entry) {
        char path[112]{};
        bool removeMarker = false;
        if (!entry.isDirectory() && hasSuffix(entry.name(), ".idx")) {
            strncpy(path, entry.name(), sizeof(path) - 1);
            uint32_t sequence = 0;
            if (entry.size() != sizeof(sequence) ||
                entry.read(reinterpret_cast<uint8_t *>(&sequence), sizeof(sequence)) != sizeof(sequence) ||
                sequence == 0 || sequence <= sequenceCutoff) {
                removeMarker = true;
            }
        }
        entry.close();
        if (removeMarker && path[0]) {
            char fullPath[128]{};
            if (path[0] == '/') strncpy(fullPath, path, sizeof(fullPath) - 1);
            else snprintf(fullPath, sizeof(fullPath), "%s/%s", kLxmfMetadataDir, path);
            if (FFat.remove(fullPath)) ++pruned;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return pruned;
}

void StorageService::applyRetentionPolicy() {
    if (!ready_) return;
    if (lxmfMessageCount_ <= kMaxRetainedMessages) {
        Serial.printf("[storage] LXMF retention: max=%lu retained=%lu high_sequence=%lu pruned=0 markers_pruned=0 unread_pruned=0\n",
                      static_cast<unsigned long>(kMaxRetainedMessages),
                      static_cast<unsigned long>(lxmfRetainedMessageCount_),
                      static_cast<unsigned long>(lxmfMessageCount_));
        return;
    }

    const uint32_t cutoff = lxmfMessageCount_ - kMaxRetainedMessages;
    uint32_t pruned = 0;
    for (uint32_t sequence = 1; sequence <= cutoff; ++sequence) {
        char path[48]{};
        lxmfMessagePath(path, sizeof(path), sequence);
        if (FFat.exists(path) && FFat.remove(path)) ++pruned;
    }
    const uint32_t markersPruned = pruneMessageIdMarkersThrough(cutoff);
    const uint32_t unreadPruned = pruneUnreadMarkersThrough(cutoff);
    const uint32_t deliveryPruned = pruneDeliveryMarkersThrough(cutoff);
    const uint32_t audioPruned = pruneAudioSidecarsThrough(cutoff);
    if (pruned >= lxmfRetainedMessageCount_) lxmfRetainedMessageCount_ = 0;
    else lxmfRetainedMessageCount_ -= pruned;
    if (lxmfRetainedMessageCount_ > kMaxRetainedMessages) lxmfRetainedMessageCount_ = kMaxRetainedMessages;
    Serial.printf("[storage] LXMF retention: max=%lu retained=%lu high_sequence=%lu cutoff=%lu pruned=%lu markers_pruned=%lu unread_pruned=%lu delivery_pruned=%lu audio_pruned=%lu\n",
                  static_cast<unsigned long>(kMaxRetainedMessages),
                  static_cast<unsigned long>(lxmfRetainedMessageCount_),
                  static_cast<unsigned long>(lxmfMessageCount_),
                  static_cast<unsigned long>(cutoff),
                  static_cast<unsigned long>(pruned),
                  static_cast<unsigned long>(markersPruned),
                  static_cast<unsigned long>(unreadPruned),
                  static_cast<unsigned long>(deliveryPruned),
                  static_cast<unsigned long>(audioPruned));
}

LxmfMessageRecord *StorageService::findRamLxmfMessage(uint32_t sequence) {
    if (sequence == 0) return nullptr;
    for (auto &slot : ramMessages_) {
        if (slot.valid && slot.record.sequence == sequence) return &slot.record;
    }
    return nullptr;
}

const LxmfMessageRecord *StorageService::findRamLxmfMessage(uint32_t sequence) const {
    if (sequence == 0) return nullptr;
    for (const auto &slot : ramMessages_) {
        if (slot.valid && slot.record.sequence == sequence) return &slot.record;
    }
    return nullptr;
}

bool StorageService::loadRamLxmfMessage(uint32_t sequence, LxmfMessageRecord &record) const {
    const LxmfMessageRecord *found = findRamLxmfMessage(sequence);
    if (!found) return false;
    record = *found;
    return true;
}

bool StorageService::saveLxmfMessageToRam(LxmfMessageRecord &record) {
    if (!ready_) return false;

    record.sequence = lxmfMessageCount_ + 1;
    if (record.verificationState == LxmfVerificationState::Unknown) {
        if (record.signatureVerified) record.verificationState = LxmfVerificationState::Verified;
        else if (record.sourceKnown) record.verificationState = LxmfVerificationState::Known;
    }
    normalizeVerificationFields(record);
    record.read = record.outgoing;

    RamMessageSlot &slot = ramMessages_[ramMessageNext_];
    if (slot.valid && !slot.record.outgoing && !slot.record.read && unreadLxmfCount_ > 0) {
        --unreadLxmfCount_;
    }
    const bool growing = !slot.valid;
    slot.valid = true;
    slot.record = record;
    if (!record.outgoing) {
        slot.record.read = false;
        record.read = false;
        ++unreadLxmfCount_;
    }
    ramMessageNext_ = (ramMessageNext_ + 1) % kEmergencyRamMessages;
    if (growing && ramMessageCount_ < kEmergencyRamMessages) {
        ++ramMessageCount_;
        ++lxmfRetainedMessageCount_;
        if (lxmfRetainedMessageCount_ > kMaxRetainedMessages) lxmfRetainedMessageCount_ = kMaxRetainedMessages;
    }
    lxmfMessageCount_ = record.sequence;
    ramJournalMode_ = true;
    status_ = "mounted + emergency RAM journal";
    Serial.printf("[m10:storage] RAM JOURNAL %s #%lu state=%s retained_ram=%u; volatile until reboot/repair\n",
                  record.outgoing ? "outgoing" : "incoming",
                  static_cast<unsigned long>(record.sequence),
                  lxmfDeliveryStateName(record.deliveryState),
                  static_cast<unsigned>(ramMessageCount_));
    return true;
}

bool StorageService::saveLxmfMessage(LxmfMessageRecord &record) {
    if (!ready_) return false;

    if (!messageIdIsZero(record.messageId) && containsLxmfMessageId(record.messageId)) {
        Serial.println("[storage] duplicate LXMF message ID already retained; refusing second history record");
        return false;
    }

    // Once a boot enters RAM-journal mode, keep all later sequences in RAM.
    // Mixing new disk and RAM records after a transient free-space change can
    // otherwise create holes/sequence collisions across a reboot.
    if (ramJournalMode_ ||
        !ensureWriteHeadroom(sizeof(PersistedLxmfRecordV3) + 2048U, "LXMF journal")) {
        return saveLxmfMessageToRam(record);
    }

    record.sequence = lxmfMessageCount_ + 1;

    // Compatibility: callers that still set the legacy booleans are upgraded
    // into the explicit M1.4 trust state before persistence.
    if (record.verificationState == LxmfVerificationState::Unknown) {
        if (record.signatureVerified) record.verificationState = LxmfVerificationState::Verified;
        else if (record.sourceKnown) record.verificationState = LxmfVerificationState::Known;
    }
    normalizeVerificationFields(record);

    PersistedLxmfRecordV3 persisted{};
    persisted.magic = kLxmfRecordMagicV3;
    persisted.version = kLxmfRecordVersionV3;
    persisted.flags = (record.sourceKnown ? kFlagSourceKnown : 0) |
                      (record.signatureVerified ? kFlagSignatureVerified : 0) |
                      (record.outgoing ? kFlagOutgoing : 0);
    persisted.sequence = record.sequence;
    persisted.timestamp = record.timestamp;
    memcpy(persisted.sourceHash, record.sourceHash, sizeof(persisted.sourceHash));
    memcpy(persisted.destinationHash, record.destinationHash, sizeof(persisted.destinationHash));
    memcpy(persisted.messageId, record.messageId, sizeof(persisted.messageId));
    persisted.deliveryState = static_cast<uint8_t>(record.deliveryState);
    persisted.reserved[0] = static_cast<uint8_t>(record.verificationState);
    persisted.reserved[1] = kVerificationMetadataMarker;
    persisted.reserved[2] = 0;
    strncpy(persisted.title, record.title, sizeof(persisted.title) - 1);
    strncpy(persisted.content, record.content, sizeof(persisted.content) - 1);
    persisted.crc32 = crc32Ieee(reinterpret_cast<const uint8_t *>(&persisted),
                                offsetof(PersistedLxmfRecordV3, crc32));

    char path[48]{};
    lxmfMessagePath(path, sizeof(path), record.sequence);
    if (FFat.exists(path)) {
        Serial.printf("[storage] ERROR: refusing to overwrite existing history record %s\n", path);
        return false;
    }

    // Prepare mutable UI metadata before committing the authoritative history
    // rename. If power is lost before the history record appears, boot audit
    // removes the orphan marker. If power is lost immediately after the rename,
    // the already-created marker preserves the unread state.
    bool unreadMarkerPrepared = false;
    char unreadPath[64]{};
    record.read = record.outgoing;
    if (!record.outgoing) {
        unreadMarkerPath(unreadPath, sizeof(unreadPath), record.sequence);
        unreadMarkerPrepared = writeUnreadMarker(record.sequence);
        if (!unreadMarkerPrepared) {
            record.read = true;
            Serial.println("[storage] WARNING: unread marker could not be prepared; message will persist as read");
        }
    }

    if (FFat.exists(kLxmfWriteTempPath)) FFat.remove(kLxmfWriteTempPath);

    File out = FFat.open(kLxmfWriteTempPath, FILE_WRITE);
    if (!out) {
        if (unreadMarkerPrepared && unreadPath[0]) FFat.remove(unreadPath);
        return false;
    }
    const size_t wrote = out.write(reinterpret_cast<const uint8_t *>(&persisted), sizeof(persisted));
    out.flush(); out.close();
    if (wrote != sizeof(persisted)) {
        FFat.remove(kLxmfWriteTempPath);
        if (unreadMarkerPrepared && unreadPath[0]) FFat.remove(unreadPath);
        return false;
    }
    if (!FFat.rename(kLxmfWriteTempPath, path)) {
        FFat.remove(kLxmfWriteTempPath);
        if (unreadMarkerPrepared && unreadPath[0]) FFat.remove(unreadPath);
        return false;
    }
    if (unreadMarkerPrepared) {
        record.read = false;
        ++unreadLxmfCount_;
    }

    // The CRC-protected record file is authoritative. Derived index failures
    // are recoverable on the next boot.
    lxmfMessageCount_ = record.sequence;
    ++lxmfRetainedMessageCount_;
    if (!saveLxmfMessageCount()) {
        Serial.println("[storage] WARNING: history record persisted but sequence index update failed; boot recovery will repair it");
    }
    if (!messageIdIsZero(record.messageId) && !lxmfMessageIdIndexDeferred_ &&
        !writeLxmfMessageIdIndex(record.messageId, record.sequence)) {
        lxmfMessageIdIndexComplete_ = false;
        Serial.println("[storage] WARNING: history record persisted but message-ID marker update failed; scan fallback remains active");
    }

    applyRetentionPolicy();
    freeBytes_ = FFat.freeBytes();
    Serial.printf("[storage] persisted LXMF %s #%lu state=%s format=LMX3 -> %s\n",
                  record.outgoing ? "outgoing" : "incoming",
                  static_cast<unsigned long>(record.sequence),
                  lxmfDeliveryStateName(record.deliveryState), path);
    return true;
}

bool StorageService::loadLxmfMessage(uint32_t sequence, LxmfMessageRecord &record) const {
    record = LxmfMessageRecord{};
    if (!ready_ || sequence == 0 || sequence > lxmfMessageCount_) return false;
    if (loadRamLxmfMessage(sequence, record)) return true;

    char path[48]{};
    lxmfMessagePath(path, sizeof(path), sequence);
    File in = FFat.open(path, FILE_READ);
    if (!in) return false;
    const size_t fileSize = static_cast<size_t>(in.size());

    if (fileSize == sizeof(PersistedLxmfRecordV3)) {
        PersistedLxmfRecordV3 p{};
        const size_t got = in.read(reinterpret_cast<uint8_t *>(&p), sizeof(p));
        in.close();
        const uint32_t expectedCrc = crc32Ieee(reinterpret_cast<const uint8_t *>(&p),
                                               offsetof(PersistedLxmfRecordV3, crc32));
        if (got != sizeof(p) || p.magic != kLxmfRecordMagicV3 || p.version != kLxmfRecordVersionV3 ||
            p.sequence != sequence || p.crc32 != expectedCrc) {
            Serial.printf("[storage] WARNING: invalid/CRC-failed LMX3 record at %s; skipped\n", path);
            return false;
        }
        record.sequence = p.sequence;
        record.timestamp = p.timestamp;
        memcpy(record.sourceHash, p.sourceHash, sizeof(record.sourceHash));
        memcpy(record.destinationHash, p.destinationHash, sizeof(record.destinationHash));
        memcpy(record.messageId, p.messageId, sizeof(record.messageId));
        record.outgoing = (p.flags & kFlagOutgoing) != 0;
        if (p.reserved[1] == kVerificationMetadataMarker) {
            record.verificationState = sanitizeVerificationState(p.reserved[0]);
        } else {
            record.verificationState = legacyVerificationState(
                (p.flags & kFlagSourceKnown) != 0, record.outgoing);
        }
        normalizeVerificationFields(record);
        record.deliveryState = sanitizeDeliveryState(p.deliveryState);
        memcpy(record.title, p.title, sizeof(record.title)); record.title[sizeof(record.title)-1] = '\0';
        memcpy(record.content, p.content, sizeof(record.content)); record.content[sizeof(record.content)-1] = '\0';
        record.read = record.outgoing || !unreadMarkerExists(sequence);
        loadDeliveryOverlay(sequence, record);
        return true;
    }

    if (fileSize == sizeof(PersistedLxmfRecordV2)) {
        PersistedLxmfRecordV2 p{};
        const size_t got = in.read(reinterpret_cast<uint8_t *>(&p), sizeof(p));
        in.close();
        if (got != sizeof(p) || p.magic != kLxmfRecordMagicV2 || p.version != kLxmfRecordVersionV2 ||
            p.sequence != sequence) {
            Serial.printf("[storage] WARNING: invalid LMX2 record at %s; skipped\n", path);
            return false;
        }
        record.sequence = p.sequence;
        record.timestamp = p.timestamp;
        memcpy(record.sourceHash, p.sourceHash, sizeof(record.sourceHash));
        memcpy(record.destinationHash, p.destinationHash, sizeof(record.destinationHash));
        memcpy(record.messageId, p.messageId, sizeof(record.messageId));
        record.outgoing = (p.flags & kFlagOutgoing) != 0;
        record.verificationState = legacyVerificationState(
            (p.flags & kFlagSourceKnown) != 0, record.outgoing);
        normalizeVerificationFields(record);
        record.deliveryState = record.outgoing ? LxmfDeliveryState::Submitted : LxmfDeliveryState::Received;
        memcpy(record.title, p.title, sizeof(record.title)); record.title[sizeof(record.title)-1] = '\0';
        memcpy(record.content, p.content, sizeof(record.content)); record.content[sizeof(record.content)-1] = '\0';
        record.read = record.outgoing || !unreadMarkerExists(sequence);
        loadDeliveryOverlay(sequence, record);
        return true;
    }

    if (fileSize == sizeof(PersistedLxmfRecordV1)) {
        PersistedLxmfRecordV1 p{};
        const size_t got = in.read(reinterpret_cast<uint8_t *>(&p), sizeof(p));
        in.close();
        if (got != sizeof(p) || p.magic != kLxmfRecordMagicV1 || p.version != kLxmfRecordVersionV1 ||
            p.sequence != sequence) {
            Serial.printf("[storage] WARNING: invalid LMX1 record at %s; skipped\n", path);
            return false;
        }
        record.sequence = p.sequence;
        record.timestamp = p.timestamp;
        memcpy(record.sourceHash, p.sourceHash, sizeof(record.sourceHash));
        memcpy(record.messageId, p.messageId, sizeof(record.messageId));
        record.outgoing = false;
        record.verificationState = legacyVerificationState(
            (p.flags & kFlagSourceKnown) != 0, false);
        normalizeVerificationFields(record);
        record.deliveryState = LxmfDeliveryState::Received;
        memcpy(record.title, p.title, sizeof(record.title)); record.title[sizeof(record.title)-1] = '\0';
        memcpy(record.content, p.content, sizeof(record.content)); record.content[sizeof(record.content)-1] = '\0';
        record.read = record.outgoing || !unreadMarkerExists(sequence);
        loadDeliveryOverlay(sequence, record);
        return true;
    }

    in.close();
    Serial.printf("[storage] WARNING: unsupported/corrupt LXMF record at %s (%u bytes); skipped\n",
                  path, static_cast<unsigned>(fileSize));
    return false;
}

bool StorageService::saveLxmfAudio(uint32_t sequence, uint8_t mode,
                                   const uint8_t *data, size_t length) {
    if (!ready_ || sequence == 0 || sequence > lxmfMessageCount_ || !data ||
        length == 0 || length > kMaxLxmfAudioBytes) return false;
    if (ramJournalMode_ ||
        !ensureWriteHeadroom(sizeof(PersistedLxmfAudioHeaderV1) + length + 1024U, "LXMF audio sidecar")) {
        RamAudioSlot &slot = ramAudio_[ramAudioNext_];
        slot.valid = true;
        slot.sequence = sequence;
        slot.mode = mode;
        slot.length = static_cast<uint16_t>(length);
        memcpy(slot.data, data, length);
        ramAudioNext_ = (ramAudioNext_ + 1) % kEmergencyRamAudio;
        Serial.printf("[m10:storage] RAM AUDIO #%lu mode=0x%02x bytes=%u; volatile until reboot/repair\n",
                      static_cast<unsigned long>(sequence), mode, static_cast<unsigned>(length));
        return true;
    }
    char path[64]{};
    audioSidecarPath(path, sizeof(path), sequence);
    if (FFat.exists(path)) {
        Serial.printf("[m9:voice] refusing to overwrite audio sidecar for #%lu\n",
                      static_cast<unsigned long>(sequence));
        return false;
    }

    PersistedLxmfAudioHeaderV1 header{};
    header.magic = kLxmfAudioMagicV1;
    header.version = kLxmfAudioVersionV1;
    header.mode = mode;
    header.encodedBytes = static_cast<uint32_t>(length);
    header.payloadCrc32 = crc32Ieee(data, length);
    header.headerCrc32 = crc32Ieee(reinterpret_cast<const uint8_t *>(&header),
                                  offsetof(PersistedLxmfAudioHeaderV1, headerCrc32));

    if (FFat.exists(kLxmfAudioWriteTempPath)) FFat.remove(kLxmfAudioWriteTempPath);
    File out = FFat.open(kLxmfAudioWriteTempPath, FILE_WRITE);
    if (!out) return false;
    const size_t wroteHeader = out.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    const size_t wrotePayload = wroteHeader == sizeof(header) ? out.write(data, length) : 0;
    out.flush(); out.close();
    if (wroteHeader != sizeof(header) || wrotePayload != length) {
        FFat.remove(kLxmfAudioWriteTempPath);
        return false;
    }
    if (!FFat.rename(kLxmfAudioWriteTempPath, path)) {
        FFat.remove(kLxmfAudioWriteTempPath);
        return false;
    }
    refreshSpaceStats();
    Serial.printf("[m9:voice] persisted audio sidecar #%lu mode=0x%02x bytes=%u\n",
                  static_cast<unsigned long>(sequence), mode, static_cast<unsigned>(length));
    return true;
}

bool StorageService::loadLxmfAudioInfo(uint32_t sequence, LxmfAudioInfo &info) const {
    info = LxmfAudioInfo{};
    if (!ready_ || sequence == 0) return false;
    for (const auto &slot : ramAudio_) {
        if (slot.valid && slot.sequence == sequence) {
            info.mode = slot.mode;
            info.encodedBytes = slot.length;
            return true;
        }
    }
    char path[64]{}; audioSidecarPath(path, sizeof(path), sequence);
    File in = FFat.open(path, FILE_READ); if (!in) return false;
    PersistedLxmfAudioHeaderV1 header{};
    const size_t got = in.read(reinterpret_cast<uint8_t *>(&header), sizeof(header));
    const size_t fileSize = in.size(); in.close();
    const uint32_t headerCrc = crc32Ieee(reinterpret_cast<const uint8_t *>(&header),
                                         offsetof(PersistedLxmfAudioHeaderV1, headerCrc32));
    if (got != sizeof(header) || header.magic != kLxmfAudioMagicV1 ||
        header.version != kLxmfAudioVersionV1 || header.headerCrc32 != headerCrc ||
        header.encodedBytes == 0 || header.encodedBytes > kMaxLxmfAudioBytes ||
        fileSize != sizeof(header) + header.encodedBytes) return false;
    info.mode = header.mode;
    info.encodedBytes = header.encodedBytes;
    return true;
}

bool StorageService::loadLxmfAudio(uint32_t sequence, LxmfAudioInfo &info,
                                   uint8_t *buffer, size_t capacity, size_t &bytesRead) const {
    bytesRead = 0;
    info = LxmfAudioInfo{};
    if (!ready_ || !buffer || sequence == 0) return false;
    for (const auto &slot : ramAudio_) {
        if (slot.valid && slot.sequence == sequence) {
            if (slot.length > capacity) return false;
            memcpy(buffer, slot.data, slot.length);
            info.mode = slot.mode;
            info.encodedBytes = slot.length;
            bytesRead = slot.length;
            return true;
        }
    }
    char path[64]{}; audioSidecarPath(path, sizeof(path), sequence);
    File in = FFat.open(path, FILE_READ); if (!in) return false;
    PersistedLxmfAudioHeaderV1 header{};
    const size_t gotHeader = in.read(reinterpret_cast<uint8_t *>(&header), sizeof(header));
    const uint32_t headerCrc = crc32Ieee(reinterpret_cast<const uint8_t *>(&header),
                                         offsetof(PersistedLxmfAudioHeaderV1, headerCrc32));
    if (gotHeader != sizeof(header) || header.magic != kLxmfAudioMagicV1 ||
        header.version != kLxmfAudioVersionV1 || header.headerCrc32 != headerCrc ||
        header.encodedBytes == 0 || header.encodedBytes > kMaxLxmfAudioBytes ||
        header.encodedBytes > capacity || in.size() != sizeof(header) + header.encodedBytes) {
        in.close(); return false;
    }
    const size_t gotPayload = in.read(buffer, header.encodedBytes); in.close();
    if (gotPayload != header.encodedBytes || crc32Ieee(buffer, gotPayload) != header.payloadCrc32) {
        Serial.printf("[m9:voice] WARNING: audio sidecar CRC failure for #%lu\n",
                      static_cast<unsigned long>(sequence));
        return false;
    }
    info.mode = header.mode;
    info.encodedBytes = header.encodedBytes;
    bytesRead = gotPayload;
    return true;
}

bool StorageService::hasLxmfAudio(uint32_t sequence) const {
    LxmfAudioInfo info{};
    return loadLxmfAudioInfo(sequence, info);
}

bool StorageService::markLxmfMessageRead(uint32_t sequence) {
    if (!ready_ || sequence == 0 || sequence > lxmfMessageCount_) return false;
    if (LxmfMessageRecord *ram = findRamLxmfMessage(sequence)) {
        if (ram->outgoing || ram->read) return true;
        ram->read = true;
        if (unreadLxmfCount_ > 0) --unreadLxmfCount_;
        Serial.printf("[m10:storage] marked RAM LXMF #%lu read; unread=%lu\n",
                      static_cast<unsigned long>(sequence), static_cast<unsigned long>(unreadLxmfCount_));
        return true;
    }
    LxmfMessageRecord record{};
    if (!loadLxmfMessage(sequence, record)) return false;
    if (record.outgoing || record.read) return true;

    char path[64]{};
    unreadMarkerPath(path, sizeof(path), sequence);
    if (!FFat.exists(path)) return true;
    if (!FFat.remove(path)) return false;
    if (unreadLxmfCount_ > 0) --unreadLxmfCount_;
    Serial.printf("[storage] marked LXMF #%lu read; unread=%lu\n",
                  static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(unreadLxmfCount_));
    return true;
}

uint32_t StorageService::pruneAudioSidecarsThrough(uint32_t sequenceCutoff) {
    if (!ready_ || sequenceCutoff == 0) return 0;
    File dir = FFat.open(kLxmfAudioDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    uint32_t pruned = 0;
    File entry = dir.openNextFile();
    while (entry) {
        char name[96]{}; strncpy(name, entry.name(), sizeof(name)-1); entry.close();
        if (hasSuffix(name, ".aud") && parseLeadingSequence(name) <= sequenceCutoff) {
            if (FFat.remove(name)) ++pruned;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return pruned;
}

uint32_t StorageService::pruneDeliveryMarkersThrough(uint32_t sequenceCutoff) {
    if (!ready_ || sequenceCutoff == 0) return 0;
    File dir = FFat.open(kLxmfDeliveryDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    uint32_t pruned = 0;
    File entry = dir.openNextFile();
    while (entry) {
        char name[96]{}; strncpy(name, entry.name(), sizeof(name)-1); entry.close();
        if (hasSuffix(name, ".dlv") && parseLeadingSequence(name) <= sequenceCutoff) {
            if (FFat.remove(name)) ++pruned;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return pruned;
}

bool StorageService::loadDeliveryOverlay(uint32_t sequence, LxmfMessageRecord &record) const {
    if (!ready_ || sequence == 0) return false;
    char path[64]{}; deliveryOverlayPath(path, sizeof(path), sequence);
    File in = FFat.open(path, FILE_READ); if (!in) return false;
    PersistedDeliveryOverlayV1 p{};
    const size_t got = in.read(reinterpret_cast<uint8_t*>(&p), sizeof(p)); in.close();
    const uint32_t crc = crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedDeliveryOverlayV1, crc32));
    if (got != sizeof(p) || p.magic != kDeliveryOverlayMagic || p.version != kDeliveryOverlayVersion || p.crc32 != crc) {
        Serial.printf("[m2:store] WARN invalid delivery overlay for #%lu; ignored\n", static_cast<unsigned long>(sequence));
        return false;
    }
    record.deliveryState = sanitizeDeliveryState(p.state);
    record.deliveryAttempts = p.attempts;
    record.deliveryRtt = p.rttSeconds;
    memcpy(record.deliveryNote, p.note, sizeof(record.deliveryNote));
    record.deliveryNote[sizeof(record.deliveryNote)-1] = '\0';
    return true;
}

bool StorageService::updateLxmfDelivery(uint32_t sequence, LxmfDeliveryState state,
                                        uint8_t attempts, float rttSeconds, const char *note) {
    if (!ready_ || sequence == 0 || sequence > lxmfMessageCount_) return false;
    if (LxmfMessageRecord *ram = findRamLxmfMessage(sequence)) {
        if (!ram->outgoing) return false;
        ram->deliveryState = state;
        ram->deliveryAttempts = attempts;
        ram->deliveryRtt = rttSeconds;
        memset(ram->deliveryNote, 0, sizeof(ram->deliveryNote));
        if (note) strncpy(ram->deliveryNote, note, sizeof(ram->deliveryNote)-1);
        Serial.printf("[m10:storage] RAM delivery #%lu -> %s attempts=%u rtt=%.3f note=%s\n",
                      static_cast<unsigned long>(sequence), lxmfDeliveryStateName(state), attempts,
                      static_cast<double>(rttSeconds), note ? note : "-");
        return true;
    }
    LxmfMessageRecord existing{};
    if (!loadLxmfMessage(sequence, existing) || !existing.outgoing) return false;
    PersistedDeliveryOverlayV1 p{};
    p.magic = kDeliveryOverlayMagic; p.version = kDeliveryOverlayVersion;
    p.state = static_cast<uint8_t>(state); p.attempts = attempts; p.rttSeconds = rttSeconds;
    p.updatedAt = static_cast<double>(millis()) / 1000.0;
    if (note) strncpy(p.note, note, sizeof(p.note)-1);
    p.crc32 = crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedDeliveryOverlayV1, crc32));
    char path[64]{}; deliveryOverlayPath(path, sizeof(path), sequence);
    File out = FFat.open(path, FILE_WRITE); if (!out) return false;
    const size_t wrote = out.write(reinterpret_cast<const uint8_t*>(&p), sizeof(p)); out.flush(); out.close();
    if (wrote != sizeof(p)) return false;
    Serial.printf("[m2:store] delivery #%lu -> %s attempts=%u rtt=%.3f note=%s\n",
                  static_cast<unsigned long>(sequence), lxmfDeliveryStateName(state), attempts,
                  static_cast<double>(rttSeconds), note ? note : "-");
    return true;
}

namespace {
bool readPeerFile(const char *path, PeerRecord &peer) {
    File in = FFat.open(path, FILE_READ); if (!in) return false;
    PersistedPeerV1 p{}; const size_t got=in.read(reinterpret_cast<uint8_t*>(&p), sizeof(p)); in.close();
    const uint32_t crc=crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedPeerV1, crc32));
    if (got!=sizeof(p)||p.magic!=kPeerMagic||p.version!=kPeerVersion||p.crc32!=crc) return false;
    peer = PeerRecord{}; memcpy(peer.destinationHash,p.destinationHash,16); peer.lastSeen=p.lastSeen; peer.hops=p.hops;
    memcpy(peer.displayName,p.displayName,sizeof(peer.displayName)); peer.displayName[sizeof(peer.displayName)-1]='\0';
    memcpy(peer.announcedName,p.announcedName,sizeof(peer.announcedName)); peer.announcedName[sizeof(peer.announcedName)-1]='\0';
    peer.discovered=(p.flags&kPeerFlagDiscovered)!=0; peer.savedContact=(p.flags&kPeerFlagSavedContact)!=0;
    peer.favorite=(p.flags&kPeerFlagFavorite)!=0; peer.verifiedLxmf=(p.flags&kPeerFlagVerifiedLxmf)!=0; return true;
}
bool writePeerFile(const PeerRecord &peer) {
    PersistedPeerV1 p{}; p.magic=kPeerMagic; p.version=kPeerVersion; memcpy(p.destinationHash,peer.destinationHash,16);
    p.lastSeen=peer.lastSeen; p.hops=peer.hops;
    if(peer.discovered)p.flags|=kPeerFlagDiscovered; if(peer.savedContact)p.flags|=kPeerFlagSavedContact;
    if(peer.favorite)p.flags|=kPeerFlagFavorite; if(peer.verifiedLxmf)p.flags|=kPeerFlagVerifiedLxmf;
    strncpy(p.displayName,peer.displayName,sizeof(p.displayName)-1); strncpy(p.announcedName,peer.announcedName,sizeof(p.announcedName)-1);
    p.crc32=crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedPeerV1, crc32));
    char path[96]{}; peerPath(path,sizeof(path),peer.destinationHash); if(!path[0]) return false;
    File out=FFat.open(path,FILE_WRITE); if(!out)return false; size_t wrote=out.write(reinterpret_cast<const uint8_t*>(&p),sizeof(p)); out.flush(); out.close(); return wrote==sizeof(p);
}
}

void StorageService::cachePeer(const PeerRecord &peer) {
    bool nonzero = false;
    for (uint8_t byte : peer.destinationHash) nonzero = nonzero || byte != 0;
    if (!nonzero) return;
    for (size_t i = 0; i < ramPeerCount_; ++i) {
        if (memcmp(ramPeers_[i].destinationHash, peer.destinationHash, 16) == 0) {
            ramPeers_[i] = peer;
            return;
        }
    }
    if (ramPeerCount_ < kMaxPeers) {
        ramPeers_[ramPeerCount_++] = peer;
        return;
    }
    // Bounded cache: replace the least recently seen non-favorite entry.
    size_t victim = 0;
    for (size_t i = 1; i < kMaxPeers; ++i) {
        if (ramPeers_[victim].favorite && !ramPeers_[i].favorite) victim = i;
        else if (ramPeers_[victim].favorite == ramPeers_[i].favorite &&
                 ramPeers_[i].lastSeen < ramPeers_[victim].lastSeen) victim = i;
    }
    ramPeers_[victim] = peer;
}

bool StorageService::loadCachedPeer(const uint8_t destinationHash[16], PeerRecord &peer) const {
    if (!destinationHash) return false;
    for (size_t i = 0; i < ramPeerCount_; ++i) {
        if (memcmp(ramPeers_[i].destinationHash, destinationHash, 16) == 0) {
            peer = ramPeers_[i];
            return true;
        }
    }
    return false;
}

bool StorageService::loadPeer(const uint8_t destinationHash[16], PeerRecord &peer) const {
    peer = PeerRecord{};
    if (!ready_ || !destinationHash) return false;
    if (loadCachedPeer(destinationHash, peer)) return true;
    char path[96]{};
    peerPath(path, sizeof(path), destinationHash);
    return readPeerFile(path, peer);
}

bool StorageService::upsertDiscoveredPeer(const uint8_t destinationHash[16], const char *announcedName,
                                           double lastSeen, uint8_t hops) {
    if (!ready_ || !destinationHash) return false;
    PeerRecord peer{};
    loadPeer(destinationHash, peer);
    memcpy(peer.destinationHash, destinationHash, 16);
    peer.discovered = true;
    if (lastSeen > peer.lastSeen) peer.lastSeen = lastSeen;
    peer.hops = hops;
    if (announcedName && announcedName[0]) {
        strncpy(peer.announcedName, announcedName, sizeof(peer.announcedName)-1);
        if (!peer.savedContact) strncpy(peer.displayName, announcedName, sizeof(peer.displayName)-1);
    }
    cachePeer(peer);

    bool persisted = false;
    if (ensureWriteHeadroom(sizeof(PersistedPeerV1) + 512U, "peer metadata")) {
        persisted = writePeerFile(peer);
    }
    Serial.printf("[m10:peer] announce cached hops=%u name=%s persistence=%s peers_ram=%u\n",
                  hops, peer.announcedName[0] ? peer.announcedName : "(unnamed)",
                  persisted ? "disk" : "RAM-only", static_cast<unsigned>(ramPeerCount_));
    return true; // discovery remains operational even if metadata persistence cannot
}

bool StorageService::notePeerFromMessage(const uint8_t destinationHash[16], bool verifiedLxmf, double seenAt) {
    if (!ready_ || !destinationHash) return false;
    PeerRecord peer{};
    loadPeer(destinationHash, peer);
    memcpy(peer.destinationHash, destinationHash, 16);
    if (seenAt > peer.lastSeen) peer.lastSeen = seenAt;
    if (verifiedLxmf) peer.verifiedLxmf = true;
    cachePeer(peer);
    if (ensureWriteHeadroom(sizeof(PersistedPeerV1) + 512U, "peer metadata")) {
        (void)writePeerFile(peer);
    }
    return true;
}

bool StorageService::savePeerName(const uint8_t destinationHash[16], const char *displayName) {
    if (!ready_ || !destinationHash || !displayName) return false;
    PeerRecord peer{};
    loadPeer(destinationHash, peer);
    memcpy(peer.destinationHash, destinationHash, 16);
    peer.savedContact = true;
    strncpy(peer.displayName, displayName, sizeof(peer.displayName)-1);
    cachePeer(peer);
    const bool persisted = ensureWriteHeadroom(sizeof(PersistedPeerV1) + 512U, "contact metadata") && writePeerFile(peer);
    Serial.printf("[m2:contact] saved name=%s persistence=%s\n", peer.displayName,
                  persisted ? "disk" : "RAM-only");
    return true;
}

bool StorageService::setPeerFavorite(const uint8_t destinationHash[16], bool favorite) {
    if (!ready_ || !destinationHash) return false;
    PeerRecord peer{};
    loadPeer(destinationHash, peer);
    memcpy(peer.destinationHash, destinationHash, 16);
    peer.savedContact = true;
    peer.favorite = favorite;
    cachePeer(peer);
    const bool persisted = ensureWriteHeadroom(sizeof(PersistedPeerV1) + 512U, "contact metadata") && writePeerFile(peer);
    Serial.printf("[m2:contact] favorite=%s persistence=%s\n", favorite ? "yes" : "no",
                  persisted ? "disk" : "RAM-only");
    return true;
}

namespace {
bool peerAlreadyListed(const PeerRecord *peers, size_t count, const uint8_t hash[16]) {
    for (size_t i = 0; i < count; ++i) {
        if (memcmp(peers[i].destinationHash, hash, 16) == 0) return true;
    }
    return false;
}

void sortPeers(PeerRecord *peers, size_t count) {
    for (size_t i=0;i<count;++i) for (size_t j=i+1;j<count;++j) {
        bool swap=false;
        if (peers[j].favorite != peers[i].favorite) swap=peers[j].favorite;
        else if (peers[j].savedContact != peers[i].savedContact) swap=peers[j].savedContact;
        else swap=peers[j].lastSeen > peers[i].lastSeen;
        if (swap) { PeerRecord t=peers[i]; peers[i]=peers[j]; peers[j]=t; }
    }
}
}

size_t StorageService::listPeers(PeerRecord *peers, size_t capacity) const {
    if (!ready_ || !peers || capacity == 0) return 0;
    size_t count = 0;

    // RAM comes first: announces must appear immediately even when FFat cannot
    // create/update peer metadata files.
    for (size_t i = 0; i < ramPeerCount_ && count < capacity; ++i) {
        if (ramPeers_[i].discovered || ramPeers_[i].savedContact || ramPeers_[i].favorite) {
            peers[count++] = ramPeers_[i];
        }
    }

    File dir = FFat.open(kLxmfPeersDir);
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry && count < capacity) {
            char name[96]{}; strncpy(name, entry.name(), sizeof(name)-1); entry.close();
            uint8_t hash[16]{};
            if (parsePeerHash(name, hash) && !peerAlreadyListed(peers, count, hash)) {
                char path[96]{}; peerPath(path, sizeof(path), hash);
                PeerRecord peer{};
                if (readPeerFile(path, peer)) peers[count++] = peer;
            }
            entry = dir.openNextFile();
        }
        dir.close();
    } else if (dir) dir.close();

    sortPeers(peers, count);
    return count;
}

size_t StorageService::listContacts(PeerRecord *peers, size_t capacity) const {
    if (!ready_ || !peers || capacity == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < ramPeerCount_ && count < capacity; ++i) {
        if (ramPeers_[i].savedContact || ramPeers_[i].favorite) peers[count++] = ramPeers_[i];
    }
    File dir = FFat.open(kLxmfPeersDir);
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry && count < capacity) {
            char name[96]{}; strncpy(name, entry.name(), sizeof(name)-1); entry.close();
            uint8_t hash[16]{};
            if (parsePeerHash(name, hash) && !peerAlreadyListed(peers, count, hash)) {
                char path[96]{}; peerPath(path, sizeof(path), hash);
                PeerRecord peer{};
                if (readPeerFile(path, peer) && (peer.savedContact || peer.favorite)) peers[count++] = peer;
            }
            entry = dir.openNextFile();
        }
        dir.close();
    } else if (dir) dir.close();
    sortPeers(peers, count);
    return count;
}

bool StorageService::loadRadioProfile(rw::config::radio::Profile &profile) const {
    profile = rw::config::radio::defaultProfile();
    if (!ready_ || !FFat.exists(kRadioProfilePath)) return false;
    File f = FFat.open(kRadioProfilePath, FILE_READ);
    if (!f) return false;
    char text[160]{};
    const size_t n = f.readBytes(text, sizeof(text)-1);
    f.close(); text[n] = '\0';
    unsigned long freq=0,bw=0; unsigned sf=0,cr=0,sync=0,pre=0; int tx=0;
    if (sscanf(text, "RW3,%lu,%lu,%u,%u,%u,%u,%d", &freq,&bw,&sf,&cr,&sync,&pre,&tx) != 7) return false;
    rw::config::radio::Profile candidate{};
    candidate.frequencyHz=static_cast<uint32_t>(freq); candidate.bandwidthHz=static_cast<uint32_t>(bw);
    candidate.spreadingFactor=static_cast<uint8_t>(sf); candidate.codingRate=static_cast<uint8_t>(cr);
    candidate.syncWord=static_cast<uint8_t>(sync); candidate.preambleSymbols=static_cast<uint16_t>(pre);
    candidate.txPowerDbm=static_cast<int8_t>(tx);
    if (!rw::config::radio::validProfile(candidate)) return false;
    profile = candidate;
    return true;
}

bool StorageService::saveRadioProfile(const rw::config::radio::Profile &profile) const {
    if (!ready_ || !rw::config::radio::validProfile(profile)) return false;
    File f = FFat.open(kRadioProfilePath, FILE_WRITE);
    if (!f) return false;
    const int written = f.printf("RW3,%lu,%lu,%u,%u,%u,%u,%d\n",
        static_cast<unsigned long>(profile.frequencyHz), static_cast<unsigned long>(profile.bandwidthHz),
        profile.spreadingFactor, profile.codingRate, profile.syncWord, profile.preambleSymbols, profile.txPowerDbm);
    f.flush(); f.close();
    if (written <= 0) return false;
    Serial.printf("[m3:radio] saved pending profile %.3f MHz BW=%lu SF=%u CR=4/%u sync=0x%02X pre=%u TX=%d dBm\n",
        profile.frequencyHz/1000000.0, static_cast<unsigned long>(profile.bandwidthHz/1000UL),
        profile.spreadingFactor, profile.codingRate, profile.syncWord, profile.preambleSymbols, profile.txPowerDbm);
    return true;
}

bool StorageService::loadPowerProfile(rw::config::power::Profile &profile) const {
    if (!ready_ || !FFat.exists(kPowerProfilePath)) return false;
    File f = FFat.open(kPowerProfilePath, FILE_READ);
    if (!f) return false;
    char text[40]{};
    const size_t n = f.readBytes(text, sizeof(text) - 1);
    f.close();
    text[n] = '\0';
    unsigned long timeout = 0;
    unsigned lift = 0;
    const int fields = sscanf(text, "RW4,%lu,%u", &timeout, &lift);
    if (fields < 1) return false;
    rw::config::power::Profile candidate{};
    candidate.displayTimeoutMs = static_cast<uint32_t>(timeout);
    candidate.liftToWake = fields >= 2 ? (lift != 0) : false;
    if (!rw::config::power::validProfile(candidate)) return false;
    profile = candidate;
    return true;
}

bool StorageService::savePowerProfile(const rw::config::power::Profile &profile) const {
    if (!ready_ || !rw::config::power::validProfile(profile)) return false;
    File f = FFat.open(kPowerProfilePath, FILE_WRITE);
    if (!f) return false;
    const int written = f.printf("RW4,%lu,%u\n", static_cast<unsigned long>(profile.displayTimeoutMs),
                                 profile.liftToWake ? 1U : 0U);
    f.flush();
    f.close();
    if (written <= 0) return false;
    Serial.printf("[m12:power] saved display timeout=%s lift_to_wake=%s\n",
                  rw::config::power::timeoutName(profile.displayTimeoutMs), profile.liftToWake ? "ON" : "OFF");
    return true;
}

bool StorageService::loadLatestLxmfMessage(LxmfMessageRecord &record) const {
    record = LxmfMessageRecord{};
    if (!ready_ || lxmfMessageCount_ == 0) return false;

    const uint32_t floor = lxmfMessageCount_ > kMaxRetainedMessages
        ? lxmfMessageCount_ - kMaxRetainedMessages
        : 0;
    for (uint32_t sequence = lxmfMessageCount_; sequence > floor; --sequence) {
        char path[48]{};
        lxmfMessagePath(path, sizeof(path), sequence);
        if (!FFat.exists(path)) continue;
        if (loadLxmfMessage(sequence, record)) return true;
    }
    return false;
}



bool StorageService::loadNetworkProfile(rw::config::network::Profile &profile) const {
    profile = rw::config::network::defaultProfile();
    if (!ready_ || !FFat.exists(kNetworkProfilePath)) return false;
    File f = FFat.open(kNetworkProfilePath, FILE_READ);
    if (!f) return false;
    rw::config::network::Profile candidate{};
    const size_t got = f.read(reinterpret_cast<uint8_t*>(&candidate), sizeof(candidate));
    f.close();
    if (got != sizeof(candidate) || !rw::config::network::validProfile(candidate)) return false;
    candidate.ssid[sizeof(candidate.ssid)-1] = '\0';
    candidate.password[sizeof(candidate.password)-1] = '\0';
    candidate.targetHost[sizeof(candidate.targetHost)-1] = '\0';
    profile = candidate;
    return true;
}

bool StorageService::saveNetworkProfile(const rw::config::network::Profile &profile) const {
    if (!ready_ || !rw::config::network::validProfile(profile)) return false;
    const char *tmp = "/app/network.tmp";
    File f = FFat.open(tmp, FILE_WRITE);
    if (!f) return false;
    const size_t wrote = f.write(reinterpret_cast<const uint8_t*>(&profile), sizeof(profile));
    f.flush();
    f.close();
    if (wrote != sizeof(profile)) { FFat.remove(tmp); return false; }
    FFat.remove(kNetworkProfilePath);
    if (!FFat.rename(tmp, kNetworkProfilePath)) { FFat.remove(tmp); return false; }
    Serial.printf("[m5:store] network profile saved mode=%s enabled=%s port=%u\n",
                  rw::config::network::modeName(profile.mode), profile.enabled ? "yes" : "no",
                  static_cast<unsigned>(profile.port));
    return true;
}

bool StorageService::loadTransportProfile(rw::config::transport::Profile &profile) const {
    profile = rw::config::transport::defaultProfile();
    if (!ready_ || !FFat.exists(kTransportProfilePath)) return false;
    File f = FFat.open(kTransportProfilePath, FILE_READ);
    if (!f) return false;
    char text[32]{};
    const size_t n = f.readBytes(text, sizeof(text)-1);
    f.close();
    text[n] = '\0';
    unsigned enabled = 0;
    if (sscanf(text, "RW6,%u", &enabled) != 1 || enabled > 1) return false;
    profile.transportEnabled = enabled != 0;
    return true;
}

bool StorageService::saveTransportProfile(const rw::config::transport::Profile &profile) const {
    if (!ready_ || !rw::config::transport::validProfile(profile)) return false;
    const char *tmp = "/app/transport.tmp";
    File f = FFat.open(tmp, FILE_WRITE);
    if (!f) return false;
    const int written = f.printf("RW6,%u\n", profile.transportEnabled ? 1U : 0U);
    f.flush();
    f.close();
    if (written <= 0) { FFat.remove(tmp); return false; }
    FFat.remove(kTransportProfilePath);
    if (!FFat.rename(tmp, kTransportProfilePath)) { FFat.remove(tmp); return false; }
    Serial.printf("[m6:transport] saved mode=%s; reboot required to apply\n",
                  rw::config::transport::modeName(profile));
    return true;
}


bool StorageService::loadUserProfile(rw::config::user::Profile &profile) const {
    profile = rw::config::user::defaultProfile();
    if (!ready_ || !FFat.exists(kUserProfilePath)) return false;
    File f = FFat.open(kUserProfilePath, FILE_READ);
    if (!f) return false;
    PersistedUserProfileV1 p{};
    const size_t got = f.read(reinterpret_cast<uint8_t*>(&p), sizeof(p));
    f.close();
    constexpr uint32_t kMagic = 0x31573852UL; // RW81
    constexpr uint16_t kVersion = 1;
    const uint32_t crc = crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedUserProfileV1, crc32));
    if (got != sizeof(p) || p.magic != kMagic || p.version != kVersion || p.crc32 != crc) return false;
    rw::config::user::Profile candidate{};
    candidate.brightness = p.brightness;
    candidate.announcePolicy = static_cast<rw::config::user::AnnouncePolicy>(p.announcePolicy);
    candidate.notificationsEnabled = (p.flags & 0x01U) != 0;
    candidate.hapticsEnabled = (p.flags & 0x02U) != 0;
    candidate.peerDiscoveryVisible = (p.flags & 0x04U) != 0;
    strncpy(candidate.displayName, p.displayName, sizeof(candidate.displayName)-1);
    strncpy(candidate.quickReply1, p.quickReply1, sizeof(candidate.quickReply1)-1);
    strncpy(candidate.quickReply2, p.quickReply2, sizeof(candidate.quickReply2)-1);
    candidate.displayName[sizeof(candidate.displayName)-1] = '\0';
    candidate.quickReply1[sizeof(candidate.quickReply1)-1] = '\0';
    candidate.quickReply2[sizeof(candidate.quickReply2)-1] = '\0';
    // M11-build2 standardises the old fixed OK/On my way pair into two
    // genuinely user-owned canned reply slots. Only the untouched legacy
    // default pair is migrated to unset; any previously customised strings
    // are preserved byte-for-byte.
    if (strcmp(candidate.quickReply1, "OK") == 0 && strcmp(candidate.quickReply2, "On my way") == 0) {
        candidate.quickReply1[0] = '\0';
        candidate.quickReply2[0] = '\0';
        Serial.println("[m11:canned] migrated legacy default canned replies to unset slots");
    }
    if (!rw::config::user::validProfile(candidate)) return false;
    profile = candidate;
    return true;
}

bool StorageService::saveUserProfile(const rw::config::user::Profile &profile) const {
    if (!ready_ || !rw::config::user::validProfile(profile)) return false;
    PersistedUserProfileV1 p{};
    constexpr uint32_t kMagic = 0x31573852UL; // RW81
    constexpr uint16_t kVersion = 1;
    p.magic = kMagic;
    p.version = kVersion;
    p.brightness = profile.brightness;
    p.announcePolicy = static_cast<uint8_t>(profile.announcePolicy);
    if (profile.notificationsEnabled) p.flags |= 0x01U;
    if (profile.hapticsEnabled) p.flags |= 0x02U;
    if (profile.peerDiscoveryVisible) p.flags |= 0x04U;
    strncpy(p.displayName, profile.displayName, sizeof(p.displayName)-1);
    strncpy(p.quickReply1, profile.quickReply1, sizeof(p.quickReply1)-1);
    strncpy(p.quickReply2, profile.quickReply2, sizeof(p.quickReply2)-1);
    p.crc32 = crc32Ieee(reinterpret_cast<const uint8_t*>(&p), offsetof(PersistedUserProfileV1, crc32));
    const char *tmp = "/app/user.tmp";
    File f = FFat.open(tmp, FILE_WRITE);
    if (!f) return false;
    const size_t wrote = f.write(reinterpret_cast<const uint8_t*>(&p), sizeof(p));
    f.flush();
    f.close();
    if (wrote != sizeof(p)) { FFat.remove(tmp); return false; }
    FFat.remove(kUserProfilePath);
    if (!FFat.rename(tmp, kUserProfilePath)) { FFat.remove(tmp); return false; }
    Serial.printf("[m8:store] user settings saved name=%s notifications=%s haptics=%s announce=%s\n",
                  profile.displayName,
                  profile.notificationsEnabled ? "on" : "off",
                  profile.hapticsEnabled ? "on" : "off",
                  rw::config::user::announcePolicyName(profile.announcePolicy));
    return true;
}

} // namespace rw::storage
