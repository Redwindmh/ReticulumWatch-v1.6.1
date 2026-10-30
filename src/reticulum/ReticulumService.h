#pragma once

#include "reticulum/RnsCompat.h"

namespace rw::radio { class RadioService; }
namespace rw::storage { class StorageService; }

namespace rw::reticulum {

class WatchLoRaInterface;

class ReticulumService {
public:
    ReticulumService();

    void begin(rw::storage::StorageService &storage, rw::radio::RadioService &radio);
    void loop();

    bool announceDestination(RNS::Destination &destination,
                             const RNS::Bytes &appData = RNS::Bytes(),
                             const char *label = "destination");

    const char *status() const { return status_; }
    bool ready() const { return ready_; }
    bool identityLoadedFromStorage() const { return identityLoaded_; }
    bool signatureValidationSelfTestPassed() const { return signatureValidationSelfTestPassed_; }
    const char *identityHash() const { return identityHash_; }
    const char *destinationHash() const { return destinationHash_; }
    const RNS::Identity &identity() const { return identity_; }
    RNS::Destination &destination() { return destination_; }
    const RNS::Destination &destination() const { return destination_; }
    uint32_t injectedPackets() const { return injectedPackets_; }
    uint32_t receivedPackets() const { return receivedPackets_; }
    uint32_t transmittedPackets() const { return transmittedPackets_; }

private:
    bool registerLoRaInterface(rw::radio::RadioService &radio);
    bool loadOrCreateIdentity(rw::storage::StorageService &storage);
    bool runSignatureValidationSelfTest();
    void createDeliveryDestination();

    static void logCallback(const char *message, RNS::LogLevel level);
    static void receivePacketCallback(const RNS::Bytes &raw, const RNS::Interface &interface);
    static void transmitPacketCallback(const RNS::Bytes &raw, const RNS::Interface &interface);

    RNS::Reticulum reticulum_{RNS::Type::NONE};
    RNS::Identity identity_{RNS::Type::NONE};
    RNS::Destination destination_{RNS::Type::NONE};
    RNS::Interface loraInterface_{RNS::Type::NONE};

    WatchLoRaInterface *loraImpl_{nullptr};
    rw::radio::RadioService *radio_{nullptr};

    bool ready_{false};
    bool identityLoaded_{false};
    bool signatureValidationSelfTestPassed_{false};
    const char *status_{"not started"};
    char identityHash_[65]{};
    char destinationHash_[65]{};
    uint32_t injectedPackets_{0};
    uint32_t receivedPackets_{0};
    uint32_t transmittedPackets_{0};

    static ReticulumService *callbackInstance_;
};

} // namespace rw::reticulum
