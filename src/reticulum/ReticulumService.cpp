#include "reticulum/ReticulumService.h"

#include <Arduino.h>
#include <cstring>
#include <exception>
#include <new>
#include <vector>

#include "config/RadioConfig.h"
#include "radio/RadioService.h"
#include "reticulum/WatchLoRaInterface.h"
#include "reticulum/RnsNoopFileSystem.h"
#include "storage/StorageService.h"


namespace rw::reticulum {
namespace {
constexpr size_t kIdentityBytes = 64;

// RNS_USE_FS and RNS_USE_PROVISIONING remain force-disabled. microReticulum
// 0.5.0 nevertheless requires a registered microStore FileSystem before
// Reticulum::start(). Keep a process-lifetime no-op filesystem while the
// application continues to own identity/history persistence on FFat.
RnsNoopFileSystem rnsFilesystem = makeRnsNoopFileSystem();

void copyHex(char *destination, size_t capacity, const std::string &source) {
    if (!destination || capacity == 0) return;
    const size_t count = source.size() < capacity - 1 ? source.size() : capacity - 1;
    memcpy(destination, source.data(), count);
    destination[count] = '\0';
}
}

ReticulumService *ReticulumService::callbackInstance_ = nullptr;

ReticulumService::ReticulumService() = default;

void ReticulumService::logCallback(const char *message, RNS::LogLevel level) {
    Serial.printf("[rns:%s] %s\n", RNS::getLevelName(level), message ? message : "");
}

void ReticulumService::receivePacketCallback(const RNS::Bytes &raw, const RNS::Interface &interface) {
    (void)interface;
    if (!callbackInstance_) return;
    ++callbackInstance_->receivedPackets_;
    Serial.printf("[rns:rx] Transport accepted packet #%lu: %u bytes\n",
                  static_cast<unsigned long>(callbackInstance_->receivedPackets_),
                  static_cast<unsigned>(raw.size()));
}

void ReticulumService::transmitPacketCallback(const RNS::Bytes &raw, const RNS::Interface &interface) {
    (void)interface;
    if (!callbackInstance_) return;
    ++callbackInstance_->transmittedPackets_;
    Serial.printf("[rns:tx] Transport emitted packet #%lu: %u bytes\n",
                  static_cast<unsigned long>(callbackInstance_->transmittedPackets_),
                  static_cast<unsigned>(raw.size()));
}

bool ReticulumService::registerLoRaInterface(rw::radio::RadioService &radioService) {
    try {
        loraImpl_ = new WatchLoRaInterface(radioService, radioService.bitrate());
    } catch (const std::bad_alloc &) {
        loraImpl_ = nullptr;
    }

    if (!loraImpl_) {
        Serial.println("[rns] ERROR: could not allocate TWatchSX1262 interface");
        return false;
    }

    loraInterface_ = loraImpl_;
    RNS::Transport::register_interface(loraInterface_);
    Serial.printf("[rns] registered full-duplex LoRa interface MTU=%u bitrate~%lu bps\n",
                  static_cast<unsigned>(rw::config::radio::kReticulumMtu),
                  static_cast<unsigned long>(radioService.bitrate()));
    return true;
}

void ReticulumService::begin(rw::storage::StorageService &storage,
                             rw::radio::RadioService &radioService) {
    Serial.println("[rns] starting microReticulum 0.5.0 security-rebased endpoint");

    if (!storage.ready()) {
        status_ = "storage unavailable";
        Serial.println("[rns] ERROR: persistent storage is not ready");
        return;
    }
    if (!radioService.ready()) {
        status_ = "radio unavailable";
        Serial.println("[rns] ERROR: SX1262 service is not ready");
        return;
    }

    radio_ = &radioService;
    callbackInstance_ = this;
    RNS::set_log_callback(&ReticulumService::logCallback);
    RNS::Transport::set_receive_packet_callback(&ReticulumService::receivePacketCallback);
    RNS::Transport::set_transmit_packet_callback(&ReticulumService::transmitPacketCallback);

    RNS::Transport::path_table_maxsize(48);
    RNS::Transport::announce_table_maxsize(48);
    RNS::Transport::hashlist_maxsize(32);
    RNS::Transport::max_pr_tags(8);
    RNS::Identity::known_destinations_maxsize(48);
    RNS::Reticulum::clean_interval(15 * 60);
    RNS::Reticulum::persist_interval(60 * 60);

    // microStore 0.1.7's NoopFileSystem::init() intentionally returns false;
    // that is the adapter's no-op contract, not an initialization failure.
    // The object itself is valid and exists only to satisfy microReticulum's
    // unconditional storage_size()/storage_available() calls during start().
    if (!rnsFilesystem) {
        status_ = "RNS filesystem adapter invalid";
        Serial.println("[rns] ERROR: required no-op filesystem adapter is invalid");
        return;
    }
    RNS::Utilities::OS::register_filesystem(rnsFilesystem);
    Serial.println("[rns] registered microStore 0.1.7 Adapters::NoopFileSystem with RNS::Utilities::OS");
    Serial.println("[rns] internal RNS persistence/provisioning disabled; app identity/inbox remain on FFat");

    if (!registerLoRaInterface(radioService)) {
        status_ = "RNS LoRa interface failed";
        return;
    }

    try {
        reticulum_ = RNS::Reticulum();
        reticulum_.transport_enabled(false);
        reticulum_.probe_destination_enabled(true);
        reticulum_.start();
    } catch (const std::exception &error) {
        status_ = "RNS start exception";
        Serial.printf("[rns] ERROR: Reticulum start failed: %s\n", error.what());
        return;
    } catch (...) {
        status_ = "RNS start exception";
        Serial.println("[rns] ERROR: Reticulum start failed with unknown exception");
        return;
    }

    Serial.println("[rns] Reticulum started as endpoint (transport disabled; probes enabled)");

    // Security gate: test the exact Identity::validate() implementation linked
    // into this firmware. The old RatSpeak lineage accepted tampered signatures,
    // so a stale/broken dependency must fail closed before the LXMF destination
    // is exposed.
    if (!runSignatureValidationSelfTest()) {
        status_ = "signature validation self-test failed";
        Serial.println("[security] FATAL: Identity::validate() self-test failed; LXMF endpoint remains disabled");
        return;
    }

    if (!loadOrCreateIdentity(storage)) {
        status_ = "identity init failed";
        return;
    }

    createDeliveryDestination();
    if (!destination_) {
        status_ = "lxmf destination failed";
        Serial.println("[rns] ERROR: could not create lxmf.delivery destination");
        return;
    }

    ready_ = true;
    status_ = identityLoaded_ ? "RNS endpoint ready; identity reloaded"
                              : "RNS endpoint ready; identity created";

    Serial.printf("[rns] identity hash:    %s\n", identityHash_);
    Serial.printf("[rns] lxmf destination: %s\n", destinationHash_);
    Serial.println("[rns] SINGLE proofs: PROVE_ALL");
    Serial.println("[rns] incoming Links: enabled");
}


bool ReticulumService::runSignatureValidationSelfTest() {
    signatureValidationSelfTestPassed_ = false;

    try {
        RNS::Identity testIdentity;
        if (!testIdentity) {
            Serial.println("[security] Identity::validate self-test: could not create test identity");
            return false;
        }

        static const uint8_t kMessageRaw[] = {
            'R','e','t','i','c','u','l','u','m','W','a','t','c','h',' ',
            'M','1','.','4','.','0',' ','s','i','g','n','a','t','u','r','e',' ','t','e','s','t'
        };
        const RNS::Bytes message(kMessageRaw, sizeof(kMessageRaw));
        const RNS::Bytes signature = testIdentity.sign(message);
        if (signature.size() == 0) {
            Serial.println("[security] Identity::validate self-test: sign returned empty signature");
            return false;
        }

        const bool validAccepted = testIdentity.validate(signature, message);

        std::vector<uint8_t> tamperedSignatureRaw(signature.data(), signature.data() + signature.size());
        tamperedSignatureRaw[0] ^= 0x01;
        const RNS::Bytes tamperedSignature(tamperedSignatureRaw.data(), tamperedSignatureRaw.size());
        const bool tamperedSignatureRejected = !testIdentity.validate(tamperedSignature, message);

        std::vector<uint8_t> tamperedMessageRaw(message.data(), message.data() + message.size());
        tamperedMessageRaw[tamperedMessageRaw.size() - 1] ^= 0x01;
        const RNS::Bytes tamperedMessage(tamperedMessageRaw.data(), tamperedMessageRaw.size());
        const bool tamperedMessageRejected = !testIdentity.validate(signature, tamperedMessage);

        signatureValidationSelfTestPassed_ =
            validAccepted && tamperedSignatureRejected && tamperedMessageRejected;

        Serial.printf("[security] Identity::validate self-test: valid=%s tampered-signature=%s tampered-message=%s => %s\n",
                      validAccepted ? "PASS" : "FAIL",
                      tamperedSignatureRejected ? "REJECT" : "ACCEPT",
                      tamperedMessageRejected ? "REJECT" : "ACCEPT",
                      signatureValidationSelfTestPassed_ ? "PASS" : "FAIL");
        return signatureValidationSelfTestPassed_;
    } catch (const std::exception &error) {
        Serial.printf("[security] Identity::validate self-test exception: %s\n", error.what());
    } catch (...) {
        Serial.println("[security] Identity::validate self-test unknown exception");
    }

    return false;
}

bool ReticulumService::loadOrCreateIdentity(rw::storage::StorageService &storage) {
    uint8_t raw[kIdentityBytes]{};
    size_t bytesRead = 0;

    if (storage.loadIdentity(raw, sizeof(raw), bytesRead) && bytesRead == kIdentityBytes) {
        RNS::Identity loaded;
        const RNS::Bytes key(raw, bytesRead);
        if (loaded.load_private_key(key)) {
            identity_ = loaded;
            identityLoaded_ = true;
            copyHex(identityHash_, sizeof(identityHash_), identity_.hexhash());
            Serial.println("[rns] loaded persistent 64-byte identity from /rns/identity.key");
            return true;
        }
        Serial.println("[rns] WARNING: stored identity invalid; generating replacement");
    }

    identity_ = RNS::Identity();
    if (!identity_) {
        Serial.println("[rns] ERROR: identity generation failed");
        return false;
    }

    const RNS::Bytes privateKey = identity_.get_private_key();
    if (privateKey.size() != kIdentityBytes) {
        Serial.printf("[rns] ERROR: unexpected private identity length=%u\n",
                      static_cast<unsigned>(privateKey.size()));
        return false;
    }

    if (!storage.saveIdentity(privateKey.data(), privateKey.size())) {
        Serial.println("[rns] ERROR: generated identity could not be persisted");
        return false;
    }

    identityLoaded_ = false;
    copyHex(identityHash_, sizeof(identityHash_), identity_.hexhash());
    Serial.println("[rns] generated and persisted new 64-byte Reticulum identity");
    return true;
}

void ReticulumService::createDeliveryDestination() {
    destination_ = RNS::Destination(identity_,
                                    RNS::Type::Destination::IN,
                                    RNS::Type::Destination::SINGLE,
                                    "lxmf",
                                    "delivery");
    if (!destination_) return;

    destination_.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);
    destination_.accepts_links(true);
    copyHex(destinationHash_, sizeof(destinationHash_), destination_.hash().toHex());
}

void ReticulumService::loop() {
    if (!ready_ || !radio_ || !loraImpl_) return;

    rw::radio::ReceivedPacket packet;
    while (radio_->takePacket(packet)) {
        ++injectedPackets_;
        Serial.printf("[rns:rx] injecting LoRa packet #%lu: %u bytes\n",
                      static_cast<unsigned long>(injectedPackets_),
                      static_cast<unsigned>(packet.length));
        try {
            loraImpl_->ingest(packet.data, packet.length);
        } catch (const std::exception &error) {
            Serial.printf("[rns:rx] ERROR: ingest exception: %s\n", error.what());
        } catch (...) {
            Serial.println("[rns:rx] ERROR: unknown ingest exception");
        }
    }

    loraImpl_->loop();
    reticulum_.loop();
    loraImpl_->loop();
}

bool ReticulumService::announceDestination(RNS::Destination &destination,
                                           const RNS::Bytes &appData,
                                           const char *label) {
    if (!ready_ || !destination || !radio_ || !radio_->ready()) return false;

    try {
        Serial.printf("[rns:tx] announcing %s %s app_data=%u bytes\n",
                      label ? label : "destination",
                      destination.hash().toHex().c_str(),
                      static_cast<unsigned>(appData.size()));
        if (appData.size() > 0) destination.announce(appData);
        else destination.announce();
        return true;
    } catch (const std::exception &error) {
        Serial.printf("[rns:tx] ERROR: announce exception: %s\n", error.what());
    } catch (...) {
        Serial.println("[rns:tx] ERROR: unknown announce exception");
    }
    return false;
}

} // namespace rw::reticulum
