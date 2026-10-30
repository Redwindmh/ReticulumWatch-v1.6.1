#include "lxmf/LxmfService.h"

#include <Arduino.h>
#include <MsgPack.h>
#include <cstring>
#include <exception>
#include <new>
#include <time.h>

#include "audio/AudioService.h"
#include "hal/WatchHardware.h"
#include "reticulum/ReticulumService.h"

namespace rw::lxmf {
namespace {
void bytesToHex(const uint8_t *bytes, size_t length, char *out, size_t capacity) {
    static const char hex[] = "0123456789abcdef";
    if (!out || capacity == 0) return;
    const size_t wholeBytes = (length < (capacity - 1) / 2) ? length : (capacity - 1) / 2;
    for (size_t i = 0; i < wholeBytes; ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[bytes[i] & 0x0F];
    }
    out[wholeBytes * 2] = '\0';
}


bool skipMsgPackValue(MsgPack::Unpacker &unpacker, unsigned depth = 0) {
    if (depth > 8) return false;
    if (unpacker.isNil()) { unpacker.unpackNil(); return true; }
    if (unpacker.isBool()) { (void)unpacker.unpackBool(); return true; }
    if (unpacker.isUInt()) { (void)unpacker.unpackUInt<uint64_t>(); return true; }
    if (unpacker.isInt()) { (void)unpacker.unpackInt<int64_t>(); return true; }
    if (unpacker.isFloat32()) { (void)unpacker.unpackFloat<float>(); return true; }
    if (unpacker.isFloat64()) { (void)unpacker.unpackFloat<double>(); return true; }
    if (unpacker.isStr()) { (void)unpacker.unpackString(); return true; }
    if (unpacker.isBin()) { (void)unpacker.unpackBinary<uint8_t>(); return true; }
    if (unpacker.isArray()) {
        const size_t n = unpacker.unpackArraySize();
        for (size_t i = 0; i < n; ++i) if (!skipMsgPackValue(unpacker, depth + 1)) return false;
        return true;
    }
    if (unpacker.isMap()) {
        const size_t n = unpacker.unpackMapSize();
        for (size_t i = 0; i < n; ++i) {
            if (!skipMsgPackValue(unpacker, depth + 1) || !skipMsgPackValue(unpacker, depth + 1)) return false;
        }
        return true;
    }
    if (unpacker.isTimestamp()) { (void)unpacker.unpackTimestamp(); return true; }
    if (unpacker.isExt()) { (void)unpacker.unpackExt(); return true; }
    return false;
}
}

class LxmfService::LxmfAnnounceHandler : public RNS::AnnounceHandler {
public:
    explicit LxmfAnnounceHandler(LxmfService &owner)
        : RNS::AnnounceHandler("lxmf.delivery"), owner_(owner) {}
    void received_announce(const RNS::Bytes &destination_hash,
                           const RNS::Identity &announced_identity,
                           const RNS::Bytes &app_data) override {
        owner_.handleAnnounce(destination_hash, announced_identity, app_data);
    }
private:
    LxmfService &owner_;
};

LxmfService *LxmfService::callbackInstance_ = nullptr;

void LxmfService::copyHex(char *destination, size_t capacity, const std::string &source) {
    if (!destination || capacity == 0) return;
    const size_t count = source.size() < capacity - 1 ? source.size() : capacity - 1;
    memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

void LxmfService::copyDisplayText(char *destination, size_t capacity,
                                  const uint8_t *source, size_t length) {
    if (!destination || capacity == 0) return;
    if (!source || length == 0) { destination[0] = '\0'; return; }
    const size_t limit = length < capacity - 1 ? length : capacity - 1;
    for (size_t i = 0; i < limit; ++i) {
        const uint8_t c = source[i];
        destination[i] = (c == 0 || (c < 0x20 && c != '\n' && c != '\t')) ? ' ' : static_cast<char>(c);
    }
    destination[limit] = '\0';
}

void LxmfService::begin(rw::hal::WatchHardware &hardware,
                        rw::storage::StorageService &storage,
                        rw::reticulum::ReticulumService &reticulum) {
    hardware_ = &hardware;
    storage_ = &storage;
    reticulum_ = &reticulum;
    callbackInstance_ = this;

    Serial.println("[lxmf] starting M4 integrated messenger endpoint");
    if (!storage.ready()) { status_ = "storage unavailable"; return; }
    if (!reticulum.ready()) { status_ = "RNS unavailable"; return; }

    RNS::Destination &destination = reticulum.destination();
    destination.set_packet_callback(&LxmfService::deliveryPacketCallback);
    destination.set_link_established_callback(&LxmfService::linkEstablishedCallback);
    copyHex(deliveryHash_, sizeof(deliveryHash_), destination.hash().toHex());

    announceHandler_ = RNS::HAnnounceHandler(new LxmfAnnounceHandler(*this));
    RNS::Transport::register_announce_handler(announceHandler_);
    Serial.println("[m2:peer] registered lxmf.delivery announce handler");
    Serial.println("[announce:compat] microReticulum 0.5.0 timestamp freshness path active; watch wall clock provided by HAL");

    if (storage.loadLatestLxmfMessage(latest_)) {
        haveLatest_ = true;
        rememberMessageId(latest_.messageId);
        Serial.printf("[lxmf] restored latest history message #%lu\n",
                      static_cast<unsigned long>(latest_.sequence));
        Serial.printf("[lxmf] durable dedupe lookup for latest message: %s\n",
                      storage.containsLxmfMessageId(latest_.messageId) ? "PASS" : "MISS");
    }

    // One-time-on-boot derived peer index rebuild. This only creates/updates
    // peer sidecars; it never rewrites historical LMX records.
    const uint32_t highSequence = storage.lxmfMessageCount();
    const uint32_t peerFloor = highSequence > rw::storage::StorageService::kMaxRetainedMessages
        ? highSequence - rw::storage::StorageService::kMaxRetainedMessages : 0;
    uint32_t indexedPeersFromHistory = 0;
    for (uint32_t sequence = highSequence; sequence > peerFloor; --sequence) {
        rw::storage::LxmfMessageRecord historical{};
        if (!storage.loadLxmfMessage(sequence, historical)) continue;
        const uint8_t *peerHash = historical.outgoing ? historical.destinationHash : historical.sourceHash;
        storage.notePeerFromMessage(peerHash,
                                    !historical.outgoing && historical.verificationState == rw::storage::LxmfVerificationState::Verified,
                                    historical.timestamp);
        ++indexedPeersFromHistory;
    }
    Serial.printf("[m2:peer] rebuilt peer metadata from %lu retained history records\n",
                  static_cast<unsigned long>(indexedPeersFromHistory));

    ready_ = true;
    status_ = "integrated messenger ready";
    Serial.printf("[lxmf] delivery destination: %s\n", deliveryHash_);
    Serial.println("[lxmf] RX: opportunistic + direct Link packets");
    Serial.println("[lxmf] TX M1.3: active-Link fast path + opportunistic/path-request send");
    Serial.println("[lxmf] history: LMX1+LMX2 compatible; LMX3 journal + M2.6 metadata overlays");
    Serial.println("[m2:tx] proof-backed opportunistic receipts + bounded retry enabled");
    Serial.println("[m2:receipt] Link proof status remains conservative on pinned microReticulum");
}

void LxmfService::deliveryPacketCallback(const RNS::Bytes &data, const RNS::Packet &packet) {
    if (callbackInstance_) callbackInstance_->enqueueOpportunistic(data, packet);
}

void LxmfService::linkEstablishedCallback(RNS::Link &link) {
    if (!callbackInstance_) return;
    ++callbackInstance_->receivedLinks_;
    Serial.printf("[lxmf:link] incoming Link #%lu established\n",
                  static_cast<unsigned long>(callbackInstance_->receivedLinks_));
    link.set_packet_callback(&LxmfService::linkPacketCallback);
    link.set_link_closed_callback(&LxmfService::linkClosedCallback);
}

void LxmfService::linkClosedCallback(RNS::Link &link) {
    if (!callbackInstance_) return;
    if (callbackInstance_->replyLink_ &&
        callbackInstance_->replyLink_.link_id() == link.link_id()) {
        callbackInstance_->replyLink_ = RNS::Link(RNS::Type::NONE);
        callbackInstance_->haveReplyPeer_ = false;
        callbackInstance_->txStatus_ = "reply link closed";
        Serial.println("[lxmf:tx] active reply Link closed");
    }
}

void LxmfService::linkPacketCallback(const RNS::Bytes &data, const RNS::Packet &packet) {
    if (callbackInstance_) callbackInstance_->enqueueLink(data, packet);
}

void LxmfService::enqueueOpportunistic(const RNS::Bytes &data, const RNS::Packet &packet) {
    ++receivedPackets_;
    const RNS::Bytes &destHash = packet.destination_hash();
    const size_t fullLength = destHash.size() + data.size();
    if (destHash.size() != kDestinationHashBytes || data.size() == 0 ||
        fullLength > kMaxDeliveryBytes || queueCount_ >= kQueueDepth) {
        ++droppedPackets_;
        return;
    }

    PendingDelivery &slot = queue_[queueTail_];
    slot = PendingDelivery{};
    slot.method = DeliveryMethod::Opportunistic;
    slot.length = fullLength;
    memcpy(slot.data, destHash.data(), destHash.size());
    memcpy(slot.data + destHash.size(), data.data(), data.size());
    queueTail_ = (queueTail_ + 1) % kQueueDepth;
    ++queueCount_;
    Serial.printf("[lxmf:rx] queued opportunistic #%lu: wire=%u bytes\n",
                  static_cast<unsigned long>(receivedPackets_), static_cast<unsigned>(fullLength));
}

void LxmfService::enqueueLink(const RNS::Bytes &data, const RNS::Packet &packet) {
    ++receivedPackets_;
    if (data.size() == 0 || data.size() > kMaxDeliveryBytes || queueCount_ >= kQueueDepth) {
        ++droppedPackets_;
        return;
    }
    PendingDelivery &slot = queue_[queueTail_];
    slot = PendingDelivery{};
    slot.method = DeliveryMethod::Link;
    slot.length = data.size();
    slot.link = packet.link();
    memcpy(slot.data, data.data(), data.size());
    queueTail_ = (queueTail_ + 1) % kQueueDepth;
    ++queueCount_;
    Serial.printf("[lxmf:rx] queued Link #%lu: wire=%u bytes active=%s\n",
                  static_cast<unsigned long>(receivedPackets_), static_cast<unsigned>(data.size()),
                  (slot.link && slot.link.status() == RNS::Type::Link::ACTIVE) ? "yes" : "no");
}

void LxmfService::loop() {
    if (!ready_) return;
    processQueuedDeliveries();
    processPendingOutbound();
    processTxJobs();
    if (announceRequested_) sendRequestedAnnounce();
}

void LxmfService::processQueuedDeliveries() {
    while (queueCount_ > 0) {
        PendingDelivery delivery = queue_[queueHead_];
        queue_[queueHead_] = PendingDelivery{};
        queueHead_ = (queueHead_ + 1) % kQueueDepth;
        --queueCount_;
        processDelivery(delivery);
    }
}

bool LxmfService::isDuplicateMessageId(const uint8_t messageId[32]) const {
    if (!messageId) return false;
    if (haveLastMessageId_ && memcmp(lastMessageId_, messageId, 32) == 0) return true;
    return storage_ && storage_->containsLxmfMessageId(messageId);
}

void LxmfService::rememberMessageId(const uint8_t messageId[32]) {
    memcpy(lastMessageId_, messageId, 32);
    haveLastMessageId_ = true;
}

void LxmfService::signalIncomingFeedback() {
    if (hardware_) hardware_->messageHaptic(true);
    if (audioFeedback_) {
        (void)audioFeedback_->playAlertTone(rw::audio::AudioService::AlertTone::MessageReceived);
    }
}

void LxmfService::signalOutgoingFeedback() {
    if (hardware_) hardware_->messageHaptic(false);
    if (audioFeedback_) {
        (void)audioFeedback_->playAlertTone(rw::audio::AudioService::AlertTone::MessageSent);
    }
}

void LxmfService::processDelivery(const PendingDelivery &delivery) {
    if (delivery.length <= kFullPrefixBytes) { ++droppedPackets_; return; }

    try {
        const RNS::Bytes raw(delivery.data, delivery.length);
        // Bytes::mid(begin, len) takes a LENGTH as its second argument.
        // LXMF full wire is 16-byte destination + 16-byte source +
        // 64-byte signature + MessagePack payload.
        const RNS::Bytes destinationHash = raw.mid(0, kDestinationHashBytes);
        const RNS::Bytes sourceHash = raw.mid(kDestinationHashBytes, kSourceHashBytes);
        const RNS::Bytes signature = raw.mid(kDestinationHashBytes + kSourceHashBytes, kSignatureBytes);
        const RNS::Bytes packedPayload = raw.mid(kFullPrefixBytes);

        if (destinationHash != reticulum_->destination().hash()) {
            ++droppedPackets_;
            Serial.println("[lxmf:rx] DROP destination hash mismatch");
            return;
        }

        RNS::Bytes hashedPart;
        hashedPart.append(destinationHash);
        hashedPart.append(sourceHash);
        hashedPart.append(packedPayload);
        const RNS::Bytes messageId = RNS::Identity::full_hash(hashedPart);
        if (messageId.size() < 32) { ++droppedPackets_; return; }

        uint8_t messageIdRaw[32]{};
        memcpy(messageIdRaw, messageId.data(), sizeof(messageIdRaw));
        if (isDuplicateMessageId(messageIdRaw)) {
            ++duplicateMessages_;
            char idPrefix[17]{};
            bytesToHex(messageIdRaw, 8, idPrefix, sizeof(idPrefix));
            Serial.printf("[lxmf:rx] DUPLICATE message id=%s... already persisted; ignored\n", idPrefix);
            return;
        }

        MsgPack::Unpacker unpacker;
        if (!unpacker.feed(packedPayload.data(), packedPayload.size())) { ++droppedPackets_; return; }
        const size_t arraySize = unpacker.unpackArraySize();
        if (arraySize < 4) { ++droppedPackets_; return; }

        const double timestamp = unpacker.unpackFloat<double>();
        const MsgPack::bin_t<uint8_t> titleBytes = unpacker.unpackBinary<uint8_t>();
        const MsgPack::bin_t<uint8_t> contentBytes = unpacker.unpackBinary<uint8_t>();

        // Parse LXMF fields without assuming this watch owns the field map.
        // Unknown standard/application fields are skipped recursively. Audio
        // follows LXMF FIELD_AUDIO (0x07): [audio_mode, encoded_binary].
        uint8_t audioMode = 0;
        MsgPack::bin_t<uint8_t> audioBytes;
        bool haveAudio = false;
        if (!unpacker.isMap()) { ++droppedPackets_; return; }
        const size_t fieldCount = unpacker.unpackMapSize();
        for (size_t field = 0; field < fieldCount; ++field) {
            int64_t key = -1;
            if (unpacker.isUInt()) key = static_cast<int64_t>(unpacker.unpackUInt<uint64_t>());
            else if (unpacker.isInt()) key = unpacker.unpackInt<int64_t>();
            else {
                if (!skipMsgPackValue(unpacker) || !skipMsgPackValue(unpacker)) { ++droppedPackets_; return; }
                continue;
            }

            if (key == kFieldAudio && unpacker.isArray()) {
                const size_t audioParts = unpacker.unpackArraySize();
                if (audioParts >= 2 && (unpacker.isUInt() || unpacker.isInt())) {
                    const int64_t mode = unpacker.isUInt()
                        ? static_cast<int64_t>(unpacker.unpackUInt<uint64_t>())
                        : unpacker.unpackInt<int64_t>();
                    if (mode >= 0 && mode <= 255 && unpacker.isBin()) {
                        audioBytes = unpacker.unpackBinary<uint8_t>();
                        audioMode = static_cast<uint8_t>(mode);
                        haveAudio = !audioBytes.empty() && audioBytes.size() <= kMaxAudioFieldBytes;
                    } else if (!skipMsgPackValue(unpacker)) { ++droppedPackets_; return; }
                    for (size_t part = 2; part < audioParts; ++part) {
                        if (!skipMsgPackValue(unpacker)) { ++droppedPackets_; return; }
                    }
                } else {
                    for (size_t part = 0; part < audioParts; ++part) {
                        if (!skipMsgPackValue(unpacker)) { ++droppedPackets_; return; }
                    }
                }
            } else if (!skipMsgPackValue(unpacker)) {
                ++droppedPackets_; return;
            }
        }
        for (size_t extra = 4; extra < arraySize; ++extra) {
            if (!skipMsgPackValue(unpacker)) { ++droppedPackets_; return; }
        }

        rw::storage::LxmfMessageRecord record{};
        record.timestamp = timestamp;
        if (timestamp > 1577836800.0) { // 2020-01-01 sanity floor
            timeAnchorEpoch_ = timestamp;
            timeAnchorMillis_ = millis();
            haveTimeAnchor_ = true;
        }
        memcpy(record.sourceHash, sourceHash.data(), 16);
        memcpy(record.destinationHash, destinationHash.data(), 16);
        memcpy(record.messageId, messageIdRaw, 32);
        record.outgoing = false;
        record.deliveryState = rw::storage::LxmfDeliveryState::Received;
        const RNS::Identity sourceIdentity = RNS::Identity::recall(sourceHash);
        if (!sourceIdentity) {
            record.verificationState = rw::storage::LxmfVerificationState::Unknown;
        } else {
            RNS::Bytes signedPart = hashedPart;
            signedPart.append(messageId);
            const bool valid = sourceIdentity.validate(signature, signedPart);
            record.verificationState = valid
                ? rw::storage::LxmfVerificationState::Verified
                : rw::storage::LxmfVerificationState::Failed;
        }
        record.sourceKnown = record.verificationState != rw::storage::LxmfVerificationState::Unknown;
        record.signatureVerified = record.verificationState == rw::storage::LxmfVerificationState::Verified;

        // Link-carried timestamps can conservatively discipline a slightly stale
        // watch clock. WatchHardware enforces a +/-30 minute guard; opportunistic
        // messages require a verified LXMF signature before they are trusted for time.
        if (hardware_ && timestamp > 1577836800.0 &&
            (delivery.method == DeliveryMethod::Link || record.signatureVerified)) {
            hardware_->disciplineTime(timestamp,
                delivery.method == DeliveryMethod::Link ? "LXMF Link" : "verified LXMF", true);
        }
        copyDisplayText(record.title, sizeof(record.title), titleBytes.data(), titleBytes.size());
        copyDisplayText(record.content, sizeof(record.content), contentBytes.data(), contentBytes.size());
        if (haveAudio && record.content[0] == '\0') {
            strncpy(record.content, "[Voice message]", sizeof(record.content) - 1);
        }

        if (!storage_->saveLxmfMessage(record)) { ++droppedPackets_; return; }
        if (haveAudio && !storage_->saveLxmfAudio(record.sequence, audioMode,
                                                  audioBytes.data(), audioBytes.size())) {
            Serial.printf("[m9:voice] WARNING: message #%lu persisted but audio sidecar failed\n",
                          static_cast<unsigned long>(record.sequence));
        } else if (haveAudio) {
            Serial.printf("[m9:voice] received standard LXMF audio #%lu mode=0x%02x bytes=%u\n",
                          static_cast<unsigned long>(record.sequence), audioMode,
                          static_cast<unsigned>(audioBytes.size()));
        }
        storage_->notePeerFromMessage(record.sourceHash,
                                      record.verificationState == rw::storage::LxmfVerificationState::Verified,
                                      record.timestamp);
        Serial.printf("[m2:thread] message #%lu indexed for peer conversation\n",
                      static_cast<unsigned long>(record.sequence));

        if (delivery.method == DeliveryMethod::Link && delivery.link &&
            delivery.link.status() == RNS::Type::Link::ACTIVE) {
            replyLink_ = delivery.link;
            memcpy(replyPeerHash_, sourceHash.data(), 16);
            haveReplyPeer_ = true;
            Serial.printf("[lxmf:tx] live reply route learned from message #%lu\n",
                          static_cast<unsigned long>(record.sequence));
        }

        latest_ = record;
        haveLatest_ = true;
        rememberMessageId(record.messageId);
        ++decodedMessages_;

        char sourceHex[33]{};
        bytesToHex(record.sourceHash, 16, sourceHex, sizeof(sourceHex));
        Serial.printf("[lxmf:rx] MESSAGE #%lu via %s source=%s signature=%s\n",
                      static_cast<unsigned long>(record.sequence),
                      delivery.method == DeliveryMethod::Link ? "Link" : "opportunistic",
                      sourceHex,
                      rw::storage::lxmfVerificationStateName(record.verificationState));
        Serial.printf("[lxmf:rx] content: %s\n", record.content[0] ? record.content : "(empty)");
        signalIncomingFeedback();
    } catch (const std::bad_alloc &) {
        ++droppedPackets_;
        Serial.println("[lxmf:rx] ERROR: out of memory while decoding");
    } catch (const std::exception &error) {
        ++droppedPackets_;
        Serial.printf("[lxmf:rx] ERROR: decode exception: %s\n", error.what());
    } catch (...) {
        ++droppedPackets_;
        Serial.println("[lxmf:rx] ERROR: unknown decode exception");
    }
}

bool LxmfService::canReplyTo(const uint8_t destinationHash[16]) const {
    if (!ready_ || !destinationHash || !haveReplyPeer_ || !replyLink_) return false;
    return replyLink_.status() == RNS::Type::Link::ACTIVE &&
           memcmp(replyPeerHash_, destinationHash, 16) == 0;
}

bool LxmfService::canSendTo(const uint8_t destinationHash[16]) const {
    if (!ready_ || !destinationHash) return false;
    bool any = false;
    for (size_t i = 0; i < kDestinationHashBytes; ++i) any = any || destinationHash[i] != 0;
    return any;
}

double LxmfService::currentTimestamp() const {
    // Prefer a synchronised system clock. If ESP wall time has not been set,
    // anchor to a valid incoming LXMF timestamp. RTC/uptime remain fallbacks.
    const time_t now = ::time(nullptr);
    if (now > 1577836800) return static_cast<double>(now); // 2020-01-01 sanity floor
    if (haveTimeAnchor_) {
        return timeAnchorEpoch_ + static_cast<double>(millis() - timeAnchorMillis_) / 1000.0;
    }
    return hardware_ ? hardware_->unixTime() : RNS::Utilities::OS::time();
}

bool LxmfService::buildSignedMessage(const uint8_t destinationHashRaw[16], const char *content,
                                     RNS::Bytes &destinationHash, RNS::Bytes &sourceHash,
                                     RNS::Bytes &packedPayload, RNS::Bytes &messageId,
                                     RNS::Bytes &signature) {
    if (!destinationHashRaw || !content || content[0] == '\0') {
        txStatus_ = "message is empty";
        return false;
    }

    const size_t contentLength = strnlen(content, kMaxReplyChars + 1);
    if (contentLength == 0 || contentLength > kMaxReplyChars) {
        txStatus_ = "message too long";
        return false;
    }

    MsgPack::Packer packer;
    packer.packArraySize(4);
    packer.packFloat(currentTimestamp());
    static const uint8_t empty = 0;
    packer.packBinary(&empty, 0); // title
    packer.packBinary(reinterpret_cast<const uint8_t *>(content), contentLength);
    packer.packMapSize(0);        // fields

    destinationHash = RNS::Bytes(destinationHashRaw, kDestinationHashBytes);
    sourceHash = reticulum_->destination().hash();
    packedPayload = RNS::Bytes(packer.data(), packer.size());

    RNS::Bytes hashedPart;
    hashedPart.append(destinationHash);
    hashedPart.append(sourceHash);
    hashedPart.append(packedPayload);
    messageId = RNS::Identity::full_hash(hashedPart);

    RNS::Bytes signedPart = hashedPart;
    signedPart.append(messageId);
    signature = reticulum_->identity().sign(signedPart);

    if (signature.size() != kSignatureBytes || messageId.size() != 32) {
        txStatus_ = "signing failed";
        return false;
    }
    return true;
}

bool LxmfService::buildSignedVoiceMessage(const uint8_t destinationHashRaw[16], uint8_t audioMode,
                                          const uint8_t *encoded, size_t encodedBytes,
                                          RNS::Bytes &destinationHash, RNS::Bytes &sourceHash,
                                          RNS::Bytes &packedPayload, RNS::Bytes &messageId,
                                          RNS::Bytes &signature) {
    if (!destinationHashRaw || !encoded || encodedBytes == 0 || encodedBytes > kMaxAudioFieldBytes) {
        txStatus_ = "invalid audio payload";
        return false;
    }

    MsgPack::Packer packer;
    packer.packArraySize(4);
    packer.packFloat(currentTimestamp());
    static const uint8_t empty = 0;
    packer.packBinary(&empty, 0); // title
    packer.packBinary(&empty, 0); // content; audio is carried in FIELD_AUDIO
    packer.packMapSize(1);
    packer.packInteger(kFieldAudio);
    packer.packArraySize(2);
    packer.packInteger(audioMode);
    packer.packBinary(encoded, encodedBytes);

    destinationHash = RNS::Bytes(destinationHashRaw, kDestinationHashBytes);
    sourceHash = reticulum_->destination().hash();
    packedPayload = RNS::Bytes(packer.data(), packer.size());
    RNS::Bytes hashedPart;
    hashedPart.append(destinationHash);
    hashedPart.append(sourceHash);
    hashedPart.append(packedPayload);
    messageId = RNS::Identity::full_hash(hashedPart);
    RNS::Bytes signedPart = hashedPart;
    signedPart.append(messageId);
    signature = reticulum_->identity().sign(signedPart);
    if (signature.size() != kSignatureBytes || messageId.size() != 32) {
        txStatus_ = "audio signing failed";
        return false;
    }
    return true;
}

uint32_t LxmfService::persistOutgoing(const uint8_t destinationHashRaw[16], const char *content,
                                      const RNS::Bytes &sourceHash, const RNS::Bytes &messageId) {
    rw::storage::LxmfMessageRecord record{};
    record.timestamp = currentTimestamp();
    memcpy(record.sourceHash, sourceHash.data(), kSourceHashBytes);
    memcpy(record.destinationHash, destinationHashRaw, kDestinationHashBytes);
    memcpy(record.messageId, messageId.data(), 32);
    record.verificationState = rw::storage::LxmfVerificationState::Verified;
    record.sourceKnown = true;
    record.signatureVerified = true; // locally generated with our persistent identity
    record.outgoing = true;
    // Local Packet::send() success means submitted to Reticulum, not remotely
    // delivered. M1.3 intentionally preserves that distinction.
    record.deliveryState = rw::storage::LxmfDeliveryState::Submitted;
    strncpy(record.content, content, sizeof(record.content) - 1);
    record.content[sizeof(record.content) - 1] = '\0';

    if (!storage_->saveLxmfMessage(record)) {
        Serial.println("[lxmf:tx] WARNING: message submitted but history persistence failed");
        return 0;
    }

    latest_ = record;
    haveLatest_ = true;
    rememberMessageId(record.messageId);
    storage_->notePeerFromMessage(destinationHashRaw, false, record.timestamp);
    Serial.printf("[m2:thread] outgoing #%lu indexed for peer conversation\n",
                  static_cast<unsigned long>(record.sequence));
    return record.sequence;
}

bool LxmfService::sendLinkMessage(const uint8_t destinationHashRaw[16], const char *content) {
    if (!canReplyTo(destinationHashRaw)) {
        txStatus_ = "no active reply link";
        return false;
    }

    try {
        RNS::Bytes destinationHash;
        RNS::Bytes sourceHash;
        RNS::Bytes packedPayload;
        RNS::Bytes messageId;
        RNS::Bytes signature;
        if (!buildSignedMessage(destinationHashRaw, content, destinationHash, sourceHash,
                                packedPayload, messageId, signature)) {
            return false;
        }

        RNS::Bytes wire;
        wire.append(destinationHash);
        wire.append(sourceHash);
        wire.append(signature);
        wire.append(packedPayload);

        if (wire.size() > replyLink_.get_mdu()) {
            txStatus_ = "message exceeds link MDU";
            return false;
        }

        RNS::Packet packet(replyLink_, wire);
        const RNS::PacketReceipt receipt = packet.send();
        if (!receipt) {
            txStatus_ = "packet send failed";
            return false;
        }

        const uint32_t sequence = persistOutgoing(destinationHashRaw, content, sourceHash, messageId);
        if (sequence == 0) { txStatus_ = "history persistence failed"; return false; }
        storage_->updateLxmfDelivery(sequence, rw::storage::LxmfDeliveryState::Submitted,
                                     1, 0.0f, "Link sent; proof validation unavailable");
        ++sentMessages_;
        txStatus_ = "submitted over encrypted link";
        Serial.printf("[m2:receipt] #%lu Link send retained as SUBMITTED; no false delivery claim\n",
                      static_cast<unsigned long>(sequence));
        Serial.printf("[lxmf:tx] signed Link message #%lu submitted: wire=%u bytes, text=%u chars\n",
                      static_cast<unsigned long>(sentMessages_),
                      static_cast<unsigned>(wire.size()),
                      static_cast<unsigned>(strnlen(content, kMaxReplyChars + 1)));
        signalOutgoingFeedback();
        return true;
    } catch (const std::exception &error) {
        txStatus_ = "send exception";
        Serial.printf("[lxmf:tx] ERROR: Link send: %s\n", error.what());
        return false;
    } catch (...) {
        txStatus_ = "send exception";
        Serial.println("[lxmf:tx] ERROR: unknown Link send exception");
        return false;
    }
}

bool LxmfService::sendVoiceMessage(const uint8_t destinationHashRaw[16], uint8_t audioMode,
                                   const uint8_t *encoded, size_t encodedBytes,
                                   uint32_t durationMs) {
    if (!canReplyTo(destinationHashRaw)) {
        txStatus_ = "voice requires active Link";
        return false;
    }
    if (!encoded || encodedBytes == 0 || encodedBytes > kMaxAudioFieldBytes) {
        txStatus_ = "voice payload invalid";
        return false;
    }

    try {
        RNS::Bytes destinationHash, sourceHash, packedPayload, messageId, signature;
        if (!buildSignedVoiceMessage(destinationHashRaw, audioMode, encoded, encodedBytes,
                                     destinationHash, sourceHash, packedPayload,
                                     messageId, signature)) return false;
        RNS::Bytes wire;
        wire.append(destinationHash);
        wire.append(sourceHash);
        wire.append(signature);
        wire.append(packedPayload);
        const size_t mdu = replyLink_.get_mdu();
        if (wire.size() > mdu) {
            txStatus_ = "voice exceeds active Link MDU";
            Serial.printf("[m9:voice] voice rejected wire=%u MDU=%u encoded=%u\n",
                          static_cast<unsigned>(wire.size()), static_cast<unsigned>(mdu),
                          static_cast<unsigned>(encodedBytes));
            return false;
        }

        RNS::Packet packet(replyLink_, wire);
        const RNS::PacketReceipt receipt = packet.send();
        if (!receipt) { txStatus_ = "voice packet send failed"; return false; }

        const uint32_t sequence = persistOutgoing(destinationHashRaw, "[Voice message]",
                                                  sourceHash, messageId);
        if (sequence == 0) { txStatus_ = "voice history persistence failed"; return false; }
        if (!storage_->saveLxmfAudio(sequence, audioMode, encoded, encodedBytes)) {
            Serial.printf("[m9:voice] WARNING: outgoing #%lu submitted but audio sidecar failed\n",
                          static_cast<unsigned long>(sequence));
        }
        storage_->updateLxmfDelivery(sequence, rw::storage::LxmfDeliveryState::Submitted,
                                     1, 0.0f, "Voice Link sent; proof validation unavailable");
        ++sentMessages_;
        txStatus_ = "voice submitted over encrypted link";
        Serial.printf("[m9:voice] signed LXMF FIELD_AUDIO #%lu submitted wire=%u MDU=%u "
                      "mode=0x%02x encoded=%u duration=%lums\n",
                      static_cast<unsigned long>(sequence), static_cast<unsigned>(wire.size()),
                      static_cast<unsigned>(mdu), audioMode, static_cast<unsigned>(encodedBytes),
                      static_cast<unsigned long>(durationMs));
        signalOutgoingFeedback();
        return true;
    } catch (const std::exception &error) {
        txStatus_ = "voice send exception";
        Serial.printf("[m9:voice] ERROR: Link voice send: %s\n", error.what());
        return false;
    } catch (...) {
        txStatus_ = "voice send exception";
        Serial.println("[m9:voice] ERROR: unknown Link voice send exception");
        return false;
    }
}

bool LxmfService::sendOpportunisticNow(const uint8_t destinationHashRaw[16], const char *content) {
    try {
        const RNS::Bytes destinationHash(destinationHashRaw, kDestinationHashBytes);
        const RNS::Identity destinationIdentity = RNS::Identity::recall(destinationHash);
        if (!destinationIdentity) { txStatus_ = "destination identity unknown"; return false; }

        RNS::Destination destination(destinationIdentity,
                                     RNS::Type::Destination::OUT,
                                     RNS::Type::Destination::SINGLE,
                                     "lxmf", "delivery");
        if (!destination || destination.hash() != destinationHash) {
            txStatus_ = "destination identity mismatch";
            Serial.println("[m2:tx] ERROR recalled identity does not derive selected lxmf.delivery hash");
            return false;
        }

        RNS::Bytes signedDestinationHash, sourceHash, packedPayload, messageId, signature;
        if (!buildSignedMessage(destinationHashRaw, content, signedDestinationHash, sourceHash,
                                packedPayload, messageId, signature)) return false;

        RNS::Bytes body; body.append(sourceHash); body.append(signature); body.append(packedPayload);
        if (body.size() > RNS::Type::Packet::ENCRYPTED_MDU) { txStatus_ = "message exceeds encrypted MDU"; return false; }

        const uint32_t sequence = persistOutgoing(destinationHashRaw, content, sourceHash, messageId);
        if (sequence == 0) { txStatus_ = "history persistence failed"; return false; }
        TxJob *job = allocateTxJob(sequence);
        if (!job) {
            storage_->updateLxmfDelivery(sequence, rw::storage::LxmfDeliveryState::Failed, 0, 0.0f,
                                         "receipt queue full");
            txStatus_ = "receipt queue full"; return false;
        }
        memcpy(job->destinationHash, destinationHashRaw, 16);
        memcpy(job->messageId, messageId.data(), 32);
        job->body = body;
        if (!submitTxAttempt(*job)) {
            storage_->updateLxmfDelivery(sequence, rw::storage::LxmfDeliveryState::Retrying,
                                         job->attempts, 0.0f, "initial TX failed; retry scheduled");
            job->pathRecoveryStartedMs = millis();
            job->nextAttemptMs = millis() + kRetryDelayMs;
            txStatus_ = "initial TX failed; retry queued";
        } else {
            txStatus_ = "submitted; awaiting proof";
        }
        ++sentMessages_;
        Serial.printf("[m2:tx] logical message #%lu sequence=%lu submitted; proof tracking active\n",
                      static_cast<unsigned long>(sentMessages_), static_cast<unsigned long>(sequence));
        return true;
    } catch (const std::exception &error) {
        txStatus_ = "send exception";
        Serial.printf("[m2:tx] ERROR opportunistic send: %s\n", error.what());
        return false;
    } catch (...) {
        txStatus_ = "send exception";
        Serial.println("[m2:tx] ERROR unknown opportunistic send exception");
        return false;
    }
}

LxmfService::TxJob *LxmfService::allocateTxJob(uint32_t sequence) {
    for (auto &job : txJobs_) if (!job.active) { job = TxJob{}; job.active = true; job.sequence = sequence; return &job; }
    return nullptr;
}

LxmfService::TxJob *LxmfService::findTxJob(uint32_t sequence) {
    for (auto &job : txJobs_) if (job.active && job.sequence == sequence) return &job;
    return nullptr;
}

void LxmfService::markReceiptEvent(uint32_t sequence, ReceiptEvent event) {
    TxJob *job = findTxJob(sequence);
    if (!job) { Serial.printf("[m2:receipt] stale callback ignored for #%lu\n", static_cast<unsigned long>(sequence)); return; }
    job->event = event;
}

bool LxmfService::submitTxAttempt(TxJob &job) {
    if (!job.active || job.body.size() == 0 || job.attempts >= kMaxTxAttempts) return false;
    const RNS::Bytes destinationHash(job.destinationHash, 16);
    if (!RNS::Transport::has_path(destinationHash)) {
        RNS::Transport::request_path(destinationHash);
        Serial.printf("[m2:retry] #%lu path missing; requested path before attempt %u\n",
                      static_cast<unsigned long>(job.sequence), static_cast<unsigned>(job.attempts + 1));
        return false;
    }
    const RNS::Identity identity = RNS::Identity::recall(destinationHash);
    if (!identity) { RNS::Transport::request_path(destinationHash); return false; }
    RNS::Destination destination(identity, RNS::Type::Destination::OUT,
                                 RNS::Type::Destination::SINGLE, "lxmf", "delivery");
    if (!destination || destination.hash() != destinationHash) return false;

    ++job.attempts;
    RNS::Packet packet(destination, job.body);
    RNS::PacketReceipt receipt = packet.send();
    if (!receipt) {
        Serial.printf("[m2:tx] #%lu attempt=%u local Packet::send failed\n",
                      static_cast<unsigned long>(job.sequence), job.attempts);
        return false;
    }
    job.receipt = receipt;
    job.pathRecoveryStartedMs = 0;
    job.awaitingReceipt = true;
    job.event = ReceiptEvent::None;
    const uint32_t sequence = job.sequence;
    job.receipt.set_timeout(kReceiptTimeoutSeconds);
    job.receipt.set_delivery_handler([this, sequence](const RNS::PacketReceipt &) {
        markReceiptEvent(sequence, ReceiptEvent::Delivered);
    });
    job.receipt.set_timeout_handler([this, sequence](const RNS::PacketReceipt &) {
        markReceiptEvent(sequence, ReceiptEvent::Timeout);
    });
    storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Submitted,
                                 job.attempts, 0.0f, "packet sent; awaiting proof");
    Serial.printf("[m2:tx] #%lu physical attempt=%u submitted body=%u\n",
                  static_cast<unsigned long>(job.sequence), job.attempts,
                  static_cast<unsigned>(job.body.size()));
    if (job.attempts == 1) signalOutgoingFeedback();
    return true;
}

void LxmfService::processTxJobs() {
    const uint32_t now = millis();
    for (auto &job : txJobs_) {
        if (!job.active) continue;
        const ReceiptEvent event = job.event;
        if (event == ReceiptEvent::Delivered) {
            job.event = ReceiptEvent::None;
            float rtt = 0.0f;
            if (job.receipt) rtt = static_cast<float>(job.receipt.get_rtt());
            storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Delivered,
                                         job.attempts, rtt, "cryptographic proof received");
            ++deliveredMessages_;
            txStatus_ = "delivered (proof received)";
            Serial.printf("[m2:receipt] #%lu DELIVERED proof RTT=%.3fs attempts=%u\n",
                          static_cast<unsigned long>(job.sequence), static_cast<double>(rtt), job.attempts);
            job.active = false;
            continue;
        }
        if (event == ReceiptEvent::Timeout) {
            job.event = ReceiptEvent::None; job.awaitingReceipt = false;
            if (job.attempts >= kMaxTxAttempts) {
                storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Failed,
                                             job.attempts, 0.0f, "proof timeout; retry limit reached");
                txStatus_ = "delivery failed after retries";
                Serial.printf("[m2:retry] #%lu FAILED after %u attempts\n",
                              static_cast<unsigned long>(job.sequence), job.attempts);
                job.active = false; continue;
            }
            ++retriedPackets_;
            storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Retrying,
                                         job.attempts, 0.0f, "proof timeout; retry scheduled");
            job.nextAttemptMs = now + kRetryDelayMs * job.attempts;
            txStatus_ = "retry scheduled";
            Serial.printf("[m2:retry] #%lu timeout after attempt=%u; retry scheduled\n",
                          static_cast<unsigned long>(job.sequence), job.attempts);
        }
        if (!job.awaitingReceipt && job.nextAttemptMs != 0 && static_cast<int32_t>(now - job.nextAttemptMs) >= 0) {
            job.nextAttemptMs = 0;
            if (!submitTxAttempt(job)) {
                if (job.attempts >= kMaxTxAttempts) {
                    storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Failed,
                                                 job.attempts, 0.0f, "retry send failed");
                    job.active = false;
                } else {
                    if (job.pathRecoveryStartedMs == 0) job.pathRecoveryStartedMs = now;
                    const uint32_t pathWait = static_cast<uint32_t>(now - job.pathRecoveryStartedMs);
                    if (pathWait >= kRetryPathRecoveryMs) {
                        storage_->updateLxmfDelivery(job.sequence, rw::storage::LxmfDeliveryState::Failed,
                                                     job.attempts, 0.0f, "path recovery window expired");
                        txStatus_ = "delivery failed: no route";
                        Serial.printf("[m2:retry] #%lu FAILED: path recovery window expired after %lums\n",
                                      static_cast<unsigned long>(job.sequence),
                                      static_cast<unsigned long>(pathWait));
                        job.active = false;
                    } else {
                        RNS::Transport::request_path(RNS::Bytes(job.destinationHash, 16));
                        job.nextAttemptMs = now + 3000;
                        Serial.printf("[m2:retry] #%lu route/send unavailable; path recovery %lums/%lums\n",
                                      static_cast<unsigned long>(job.sequence),
                                      static_cast<unsigned long>(pathWait),
                                      static_cast<unsigned long>(kRetryPathRecoveryMs));
                    }
                }
            }
        }
    }
}

bool LxmfService::queuePathRequest(const uint8_t destinationHashRaw[16], const char *content) {
    if (pendingOutbound_.active) {
        txStatus_ = "another send is awaiting a path";
        return false;
    }

    pendingOutbound_ = PendingOutbound{};
    pendingOutbound_.active = true;
    memcpy(pendingOutbound_.destinationHash, destinationHashRaw, kDestinationHashBytes);
    strncpy(pendingOutbound_.content, content, kMaxReplyChars);
    pendingOutbound_.content[kMaxReplyChars] = '\0';
    pendingOutbound_.requestedAtMs = millis();
    pendingOutbound_.lastPollMs = pendingOutbound_.requestedAtMs;

    const RNS::Bytes destinationHash(destinationHashRaw, kDestinationHashBytes);
    RNS::Transport::request_path(destinationHash);
    txStatus_ = "path requested; message queued";

    char destinationHex[33]{};
    bytesToHex(destinationHashRaw, kDestinationHashBytes, destinationHex, sizeof(destinationHex));
    Serial.printf("[m2:tx] path requested for %s; one outbound message queued in RAM\n",
                  destinationHex);
    return true;
}

void LxmfService::processPendingOutbound() {
    if (!pendingOutbound_.active) return;

    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - pendingOutbound_.requestedAtMs) >= kPathRequestTimeoutMs) {
        char destinationHex[33]{};
        bytesToHex(pendingOutbound_.destinationHash, kDestinationHashBytes,
                   destinationHex, sizeof(destinationHex));
        Serial.printf("[m2:retry] path request timed out for %s; queued message not sent\n",
                      destinationHex);
        pendingOutbound_ = PendingOutbound{};
        txStatus_ = "path request timed out";
        return;
    }

    if (static_cast<uint32_t>(now - pendingOutbound_.lastPollMs) < kPathPollIntervalMs) return;
    pendingOutbound_.lastPollMs = now;

    // If the peer establishes a new incoming Link while the path request is
    // pending, use the already-proven encrypted Link path immediately instead
    // of waiting for an announce.
    if (canReplyTo(pendingOutbound_.destinationHash)) {
        PendingOutbound ready = pendingOutbound_;
        pendingOutbound_ = PendingOutbound{};
        Serial.println("[lxmf:tx] live Link became available for queued outbound message");
        if (!sendLinkMessage(ready.destinationHash, ready.content)) {
            Serial.printf("[lxmf:tx] ERROR: queued Link send failed: %s\n", txStatus_);
        }
        return;
    }

    const RNS::Bytes destinationHash(pendingOutbound_.destinationHash, kDestinationHashBytes);
    if (!RNS::Transport::has_path(destinationHash)) return;

    const RNS::Identity destinationIdentity = RNS::Identity::recall(destinationHash);
    if (!destinationIdentity) return;

    PendingOutbound ready = pendingOutbound_;
    pendingOutbound_ = PendingOutbound{};

    char destinationHex[33]{};
    bytesToHex(ready.destinationHash, kDestinationHashBytes,
               destinationHex, sizeof(destinationHex));
    Serial.printf("[lxmf:tx] path + identity resolved for %s; sending opportunistically\n",
                  destinationHex);

    if (!sendOpportunisticNow(ready.destinationHash, ready.content)) {
        Serial.printf("[lxmf:tx] ERROR: queued opportunistic send failed: %s\n", txStatus_);
    }
}

bool LxmfService::sendMessage(const uint8_t destinationHashRaw[16], const char *content) {
    if (!ready_ || !storage_ || !reticulum_ || !canSendTo(destinationHashRaw)) {
        txStatus_ = "LXMF send unavailable";
        return false;
    }
    if (!content || content[0] == '\0') {
        txStatus_ = "message is empty";
        return false;
    }
    const size_t contentLength = strnlen(content, kMaxReplyChars + 1);
    if (contentLength == 0 || contentLength > kMaxReplyChars) {
        txStatus_ = "message too long";
        return false;
    }

    // Keep the already-proven M1.2 direct-Link route as the preferred fast path.
    if (canReplyTo(destinationHashRaw)) return sendLinkMessage(destinationHashRaw, content);

    const RNS::Bytes destinationHash(destinationHashRaw, kDestinationHashBytes);
    if (RNS::Transport::has_path(destinationHash)) {
        const RNS::Identity destinationIdentity = RNS::Identity::recall(destinationHash);
        if (destinationIdentity) return sendOpportunisticNow(destinationHashRaw, content);
    }

    return queuePathRequest(destinationHashRaw, content);
}

void LxmfService::handleAnnounce(const RNS::Bytes &destinationHash,
                                 const RNS::Identity &announcedIdentity,
                                 const RNS::Bytes &appData) {
    if (!ready_ || !storage_ || destinationHash.size() != 16 || !announcedIdentity) return;
    if (destinationHash == reticulum_->destination().hash()) return;
    char name[33]{};
    try {
        MsgPack::Unpacker unpacker;
        if (appData.size() && unpacker.feed(appData.data(), appData.size())) {
            const size_t count = unpacker.unpackArraySize();
            if (count >= 1) {
                const MsgPack::bin_t<uint8_t> display = unpacker.unpackBinary<uint8_t>();
                copyDisplayText(name, sizeof(name), display.data(), display.size());
            }
        }
    } catch (...) { name[0] = '\0'; }
    uint8_t hash[16]{}; memcpy(hash, destinationHash.data(), 16);
    const uint8_t hops = RNS::Transport::hops_to(destinationHash);
    storage_->upsertDiscoveredPeer(hash, name, currentTimestamp(), hops);
    ++discoveredPeers_;
    char shortHash[17]{}; bytesToHex(hash, 8, shortHash, sizeof(shortHash));
    Serial.printf("[m2:peer] announce #%lu peer=%s... hops=%u name=%s app_data=%u\n",
                  static_cast<unsigned long>(discoveredPeers_), shortHash, hops,
                  name[0] ? name : "(unnamed)", static_cast<unsigned>(appData.size()));
}

RNS::Bytes LxmfService::announceAppData() const {
    static const uint8_t kAppData[] = {
        0x93, 0xC4, 0x0F,
        'R','e','t','i','c','u','l','u','m',' ','W','a','t','c','h',
        0xC0, 0x90
    };
    return RNS::Bytes(kAppData, sizeof(kAppData));
}

bool LxmfService::requestAnnounce() {
    if (!ready_ || announceRequested_) return false;
    announceRequested_ = true;
    ++announceRequests_;
    return true;
}

void LxmfService::sendRequestedAnnounce() {
    announceRequested_ = false;
    const time_t announceEpoch = ::time(nullptr);
    Serial.printf("[announce:compat] wall-clock epoch=%lld before lxmf.delivery announce\n",
                  static_cast<long long>(announceEpoch));
    const RNS::Bytes appData = announceAppData();
    if (reticulum_->announceDestination(reticulum_->destination(), appData, "lxmf.delivery")) {
        Serial.printf("[lxmf:tx] lxmf.delivery announce emitted for %s (emitted; discovery handler active)\n", deliveryHash_);
    } else {
        Serial.println("[lxmf:tx] ERROR: lxmf.delivery announce failed");
    }
}

} // namespace rw::lxmf
