#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "reticulum/RnsCompat.h"
#include "storage/StorageService.h"

namespace rw::hal { class WatchHardware; }
namespace rw::audio { class AudioService; }
namespace rw::reticulum { class ReticulumService; }

namespace rw::lxmf {

class LxmfService {
public:
    void begin(rw::hal::WatchHardware &hardware,
               rw::storage::StorageService &storage,
               rw::reticulum::ReticulumService &reticulum);
    void loop();
    void setAudioFeedback(rw::audio::AudioService *audio) { audioFeedback_ = audio; }
    bool requestAnnounce();

    bool canReplyTo(const uint8_t destinationHash[16]) const;
    bool canSendTo(const uint8_t destinationHash[16]) const;
    bool sendMessage(const uint8_t destinationHash[16], const char *content);
    bool sendReply(const uint8_t destinationHash[16], const char *content) {
        return sendMessage(destinationHash, content);
    }
    // M9-build5 interoperable recorded audio: standard LXMF FIELD_AUDIO.
    // Voice stays Link-only so no private fragmentation or unproven Resource
    // path is introduced into the accepted endpoint.
    bool sendVoiceMessage(const uint8_t destinationHash[16], uint8_t audioMode,
                          const uint8_t *encoded, size_t encodedBytes,
                          uint32_t durationMs = 0);

    bool pendingOutbound() const { return pendingOutbound_.active; }
    bool ready() const { return ready_; }
    const char *status() const { return status_; }
    const char *deliveryHash() const { return deliveryHash_; }
    const char *txStatus() const { return txStatus_; }
    uint32_t messageCount() const { return storage_ ? storage_->lxmfRetainedMessageCount() : 0; }
    uint32_t receivedPackets() const { return receivedPackets_; }
    uint32_t receivedLinks() const { return receivedLinks_; }
    uint32_t decodedMessages() const { return decodedMessages_; }
    uint32_t sentMessages() const { return sentMessages_; }
    uint32_t duplicateMessages() const { return duplicateMessages_; }
    uint32_t droppedPackets() const { return droppedPackets_; }
    uint32_t announceRequests() const { return announceRequests_; }
    uint32_t discoveredPeers() const { return discoveredPeers_; }
    uint32_t deliveredMessages() const { return deliveredMessages_; }
    uint32_t retriedPackets() const { return retriedPackets_; }
    bool haveLatestMessage() const { return haveLatest_; }
    const rw::storage::LxmfMessageRecord &latestMessage() const { return latest_; }

private:
    static constexpr size_t kQueueDepth = 6;
    static constexpr size_t kDestinationHashBytes = 16;
    static constexpr size_t kSourceHashBytes = 16;
    static constexpr size_t kSignatureBytes = 64;
    static constexpr size_t kFullPrefixBytes = 96;
    static constexpr size_t kMaxDeliveryBytes = 524;
    static constexpr size_t kMaxReplyChars = 220;
    static constexpr uint32_t kPathRequestTimeoutMs = 30000;
    static constexpr uint32_t kPathPollIntervalMs = 250;
    static constexpr uint8_t kMaxTxAttempts = 3;
    static constexpr uint32_t kRetryDelayMs = 1800;
    static constexpr uint32_t kRetryPathRecoveryMs = 12000;
    static constexpr int16_t kReceiptTimeoutSeconds = 12;
    static constexpr size_t kTxJobCount = 3;
    static constexpr uint8_t kFieldAudio = 0x07;
    static constexpr size_t kMaxAudioFieldBytes = 512;

    enum class DeliveryMethod : uint8_t { Opportunistic, Link };
    enum class ReceiptEvent : uint8_t { None, Delivered, Timeout };

    struct PendingDelivery {
        DeliveryMethod method{DeliveryMethod::Opportunistic};
        size_t length{0};
        uint8_t data[kMaxDeliveryBytes]{};
        RNS::Link link{RNS::Type::NONE};
    };

    struct PendingOutbound {
        bool active{false};
        uint8_t destinationHash[kDestinationHashBytes]{};
        char content[kMaxReplyChars + 1]{};
        uint32_t requestedAtMs{0};
        uint32_t lastPollMs{0};
    };

    struct TxJob {
        bool active{false};
        uint32_t sequence{0};
        uint8_t destinationHash[16]{};
        uint8_t messageId[32]{};
        uint8_t attempts{0};
        uint32_t nextAttemptMs{0};
        uint32_t pathRecoveryStartedMs{0};
        bool awaitingReceipt{false};
        volatile ReceiptEvent event{ReceiptEvent::None};
        RNS::Bytes body{};
        RNS::PacketReceipt receipt{RNS::Type::NONE};
    };

    class LxmfAnnounceHandler;

    static void deliveryPacketCallback(const RNS::Bytes &data, const RNS::Packet &packet);
    static void linkEstablishedCallback(RNS::Link &link);
    static void linkClosedCallback(RNS::Link &link);
    static void linkPacketCallback(const RNS::Bytes &data, const RNS::Packet &packet);

    void enqueueOpportunistic(const RNS::Bytes &data, const RNS::Packet &packet);
    void enqueueLink(const RNS::Bytes &data, const RNS::Packet &packet);
    void processQueuedDeliveries();
    void processDelivery(const PendingDelivery &delivery);
    void processPendingOutbound();
    void processTxJobs();
    void sendRequestedAnnounce();
    RNS::Bytes announceAppData() const;
    double currentTimestamp() const;

    void handleAnnounce(const RNS::Bytes &destinationHash,
                        const RNS::Identity &announcedIdentity,
                        const RNS::Bytes &appData);
    void markReceiptEvent(uint32_t sequence, ReceiptEvent event);

    bool buildSignedMessage(const uint8_t destinationHashRaw[16], const char *content,
                            RNS::Bytes &destinationHash, RNS::Bytes &sourceHash,
                            RNS::Bytes &packedPayload, RNS::Bytes &messageId,
                            RNS::Bytes &signature);
    bool buildSignedVoiceMessage(const uint8_t destinationHashRaw[16], uint8_t audioMode,
                                 const uint8_t *encoded, size_t encodedBytes,
                                 RNS::Bytes &destinationHash, RNS::Bytes &sourceHash,
                                 RNS::Bytes &packedPayload, RNS::Bytes &messageId,
                                 RNS::Bytes &signature);
    bool sendLinkMessage(const uint8_t destinationHash[16], const char *content);
    bool sendOpportunisticNow(const uint8_t destinationHash[16], const char *content);
    bool queuePathRequest(const uint8_t destinationHash[16], const char *content);
    uint32_t persistOutgoing(const uint8_t destinationHash[16], const char *content,
                             const RNS::Bytes &sourceHash, const RNS::Bytes &messageId);
    TxJob *allocateTxJob(uint32_t sequence);
    TxJob *findTxJob(uint32_t sequence);
    bool submitTxAttempt(TxJob &job);

    static void copyHex(char *destination, size_t capacity, const std::string &source);
    static void copyDisplayText(char *destination, size_t capacity, const uint8_t *source, size_t length);
    bool isDuplicateMessageId(const uint8_t messageId[32]) const;
    void rememberMessageId(const uint8_t messageId[32]);
    void signalIncomingFeedback();
    void signalOutgoingFeedback();

    rw::hal::WatchHardware *hardware_{nullptr};
    rw::audio::AudioService *audioFeedback_{nullptr};
    rw::storage::StorageService *storage_{nullptr};
    rw::reticulum::ReticulumService *reticulum_{nullptr};

    PendingDelivery queue_[kQueueDepth]{};
    size_t queueHead_{0};
    size_t queueTail_{0};
    size_t queueCount_{0};

    RNS::Link replyLink_{RNS::Type::NONE};
    uint8_t replyPeerHash_[16]{};
    bool haveReplyPeer_{false};
    PendingOutbound pendingOutbound_{};
    TxJob txJobs_[kTxJobCount]{};
    RNS::HAnnounceHandler announceHandler_{};

    rw::storage::LxmfMessageRecord latest_{};
    bool haveLatest_{false};
    uint8_t lastMessageId_[32]{};
    bool haveLastMessageId_{false};

    double timeAnchorEpoch_{0.0};
    uint32_t timeAnchorMillis_{0};
    bool haveTimeAnchor_{false};

    bool ready_{false};
    bool announceRequested_{false};
    const char *status_{"not started"};
    const char *txStatus_{"idle"};
    char deliveryHash_[65]{};

    uint32_t receivedPackets_{0};
    uint32_t receivedLinks_{0};
    uint32_t decodedMessages_{0};
    uint32_t sentMessages_{0};
    uint32_t duplicateMessages_{0};
    uint32_t droppedPackets_{0};
    uint32_t announceRequests_{0};
    uint32_t discoveredPeers_{0};
    uint32_t deliveredMessages_{0};
    uint32_t retriedPackets_{0};

    static LxmfService *callbackInstance_;
};

} // namespace rw::lxmf
