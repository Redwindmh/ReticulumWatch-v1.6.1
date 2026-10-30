#pragma once

#include <stddef.h>
#include <stdint.h>
#include "config/RadioConfig.h"
#include "config/PowerConfig.h"
#include "config/NetworkConfig.h"
#include "config/TransportConfig.h"
#include "config/UserConfig.h"

namespace rw::storage {

enum class LxmfVerificationState : uint8_t {
    Unknown = 0,
    Known = 1,
    Verified = 2,
    Failed = 3,
};

const char *lxmfVerificationStateName(LxmfVerificationState state);

enum class LxmfDeliveryState : uint8_t {
    Unknown = 0,
    Received = 1,
    Submitted = 2,
    Delivered = 3,
    Failed = 4,
    Retrying = 5,
};

const char *lxmfDeliveryStateName(LxmfDeliveryState state);

struct LxmfMessageRecord {
    uint32_t sequence{0};
    double timestamp{0.0};
    uint8_t sourceHash[16]{};
    uint8_t destinationHash[16]{};
    uint8_t messageId[32]{};
    bool sourceKnown{false};
    bool signatureVerified{false};
    LxmfVerificationState verificationState{LxmfVerificationState::Unknown};
    bool outgoing{false};
    LxmfDeliveryState deliveryState{LxmfDeliveryState::Unknown};
    bool read{true};
    uint8_t deliveryAttempts{0};
    float deliveryRtt{0.0f};
    char deliveryNote[49]{};
    char title[65]{};
    char content[257]{};
};

struct LxmfAudioInfo {
    uint8_t mode{0};
    uint32_t encodedBytes{0};
};

struct PeerRecord {
    uint8_t destinationHash[16]{};
    char displayName[33]{};
    char announcedName[33]{};
    double lastSeen{0.0};
    uint8_t hops{0};
    bool discovered{false};
    bool savedContact{false};
    bool favorite{false};
    bool verifiedLxmf{false};
};

class StorageService {
public:
    static constexpr uint32_t kMaxRetainedMessages = 256;
    static constexpr uint32_t kMaxQuarantinedRecords = 16;
    static constexpr size_t kMaxPeers = 48;
    static constexpr uint64_t kStoragePressureThresholdBytes = 256ULL * 1024ULL;
    static constexpr uint64_t kCriticalWriteReserveBytes = 16ULL * 1024ULL;
    // Emergency operation must never depend on a writable FAT volume. When
    // persistent journal writes are unavailable, keep a small bounded RAM
    // journal so RX/TX and the UI remain functional until reboot/repair.
    static constexpr size_t kEmergencyRamMessages = 16;
    static constexpr size_t kEmergencyRamAudio = 8;

    void begin();

    bool ready() const { return ready_; }
    const char *status() const { return status_; }
    uint32_t bootCount() const { return bootCount_; }
    uint64_t totalBytes() const { return totalBytes_; }
    uint64_t freeBytes() const { return freeBytes_; }
    uint32_t lxmfMessageCount() const { return lxmfMessageCount_; }
    uint32_t lxmfRetainedMessageCount() const { return lxmfRetainedMessageCount_; }
    uint32_t unreadLxmfCount() const { return unreadLxmfCount_; }
    bool underPressure() const { return freeBytes_ < kStoragePressureThresholdBytes; }
    bool criticalLowSpace() const { return freeBytes_ < kCriticalWriteReserveBytes; }
    bool ramJournalActive() const { return ramJournalMode_; }
    size_t ramJournalCount() const { return ramMessageCount_; }
    void refreshSpaceStats();
    // M11 explicit recovery mode: disable normal application filesystem writes
    // while the USB recovery service owns FFat for format/restore.
    void enterRecoveryMaintenance();

    bool loadIdentity(uint8_t *buffer, size_t capacity, size_t &bytesRead) const;
    bool saveIdentity(const uint8_t *buffer, size_t length) const;

    // The immutable LMX1/LMX2/LMX3 journal remains the source of message
    // content. M2.6 overlays mutable delivery/read state using sidecar files.
    bool saveLxmfMessage(LxmfMessageRecord &record);
    bool loadLxmfMessage(uint32_t sequence, LxmfMessageRecord &record) const;
    bool loadLatestLxmfMessage(LxmfMessageRecord &record) const;
    bool markLxmfMessageRead(uint32_t sequence);
    bool updateLxmfDelivery(uint32_t sequence, LxmfDeliveryState state,
                            uint8_t attempts, float rttSeconds,
                            const char *note = nullptr);

    bool containsLxmfMessageId(const uint8_t messageId[32]) const;

    // M9-build5 keeps standard LXMF audio fields in CRC-protected sidecars so
    // the immutable LMX1/LMX2/LMX3 compatibility contract remains unchanged.
    bool saveLxmfAudio(uint32_t sequence, uint8_t mode, const uint8_t *data, size_t length);
    bool loadLxmfAudioInfo(uint32_t sequence, LxmfAudioInfo &info) const;
    bool loadLxmfAudio(uint32_t sequence, LxmfAudioInfo &info,
                       uint8_t *buffer, size_t capacity, size_t &bytesRead) const;
    bool hasLxmfAudio(uint32_t sequence) const;

    // Contacts/discovered peers are derived metadata and can be discarded or
    // rebuilt without touching the LXMF history journal.
    bool upsertDiscoveredPeer(const uint8_t destinationHash[16], const char *announcedName,
                              double lastSeen, uint8_t hops);
    bool notePeerFromMessage(const uint8_t destinationHash[16], bool verifiedLxmf, double seenAt);
    bool savePeerName(const uint8_t destinationHash[16], const char *displayName);
    bool setPeerFavorite(const uint8_t destinationHash[16], bool favorite);
    bool loadPeer(const uint8_t destinationHash[16], PeerRecord &peer) const;
    size_t listPeers(PeerRecord *peers, size_t capacity) const;
    size_t listContacts(PeerRecord *peers, size_t capacity) const;

    bool loadRadioProfile(rw::config::radio::Profile &profile) const;
    bool saveRadioProfile(const rw::config::radio::Profile &profile) const;
    bool loadPowerProfile(rw::config::power::Profile &profile) const;
    bool savePowerProfile(const rw::config::power::Profile &profile) const;
    bool loadNetworkProfile(rw::config::network::Profile &profile) const;
    bool saveNetworkProfile(const rw::config::network::Profile &profile) const;
    bool loadTransportProfile(rw::config::transport::Profile &profile) const;
    bool saveTransportProfile(const rw::config::transport::Profile &profile) const;
    bool loadUserProfile(rw::config::user::Profile &profile) const;
    bool saveUserProfile(const rw::config::user::Profile &profile) const;

    // Explicit user action only. Removes message history and message-derived
    // overlays/audio while preserving identity, settings, contacts and peers.
    bool wipeChatHistory();

private:
    bool ensureDirectories();
    bool ensureWriteHeadroom(size_t bytesNeeded, const char *operation);
    bool updateBootCounter();
    bool loadLxmfMessageCount();
    bool saveLxmfMessageCount() const;
    bool rebuildLxmfMessageIdIndex();
    bool writeLxmfMessageIdIndex(const uint8_t messageId[32], uint32_t sequence) const;
    bool readLxmfMessageIdIndex(const uint8_t messageId[32], uint32_t &sequence) const;
    bool quarantineLxmfRecord(uint32_t sequence);
    void trimCorruptQuarantine();
    void applyRetentionPolicy();
    uint32_t pruneMessageIdMarkersThrough(uint32_t sequenceCutoff);
    bool unreadMarkerExists(uint32_t sequence) const;
    bool writeUnreadMarker(uint32_t sequence);
    uint32_t rebuildUnreadState();
    uint32_t pruneUnreadMarkersThrough(uint32_t sequenceCutoff);
    uint32_t pruneDeliveryMarkersThrough(uint32_t sequenceCutoff);
    uint32_t pruneAudioSidecarsThrough(uint32_t sequenceCutoff);
    bool loadDeliveryOverlay(uint32_t sequence, LxmfMessageRecord &record) const;
    bool saveLxmfMessageToRam(LxmfMessageRecord &record);
    bool loadRamLxmfMessage(uint32_t sequence, LxmfMessageRecord &record) const;
    LxmfMessageRecord *findRamLxmfMessage(uint32_t sequence);
    const LxmfMessageRecord *findRamLxmfMessage(uint32_t sequence) const;
    void cachePeer(const PeerRecord &peer);
    bool loadCachedPeer(const uint8_t destinationHash[16], PeerRecord &peer) const;

    bool ready_{false};
    const char *status_{"not mounted"};
    uint32_t bootCount_{0};
    uint32_t lxmfMessageCount_{0};
    uint32_t lxmfRetainedMessageCount_{0};
    uint32_t unreadLxmfCount_{0};
    bool lxmfMessageIdIndexComplete_{false};
    bool lxmfMessageIdIndexDeferred_{false};
    uint64_t totalBytes_{0};
    uint64_t freeBytes_{0};

    struct RamMessageSlot {
        bool valid{false};
        LxmfMessageRecord record{};
    };
    struct RamAudioSlot {
        bool valid{false};
        uint32_t sequence{0};
        uint8_t mode{0};
        uint16_t length{0};
        uint8_t data[512]{};
    };
    RamMessageSlot ramMessages_[kEmergencyRamMessages]{};
    RamAudioSlot ramAudio_[kEmergencyRamAudio]{};
    size_t ramMessageCount_{0};
    size_t ramMessageNext_{0};
    size_t ramAudioNext_{0};
    bool ramJournalMode_{false};

    // Announce discovery is operational state, not authoritative history. Keep
    // a RAM cache so the PEERS screen still works if metadata persistence is
    // impossible on a full/corrupt FAT volume.
    PeerRecord ramPeers_[kMaxPeers]{};
    size_t ramPeerCount_{0};
};

} // namespace rw::storage
