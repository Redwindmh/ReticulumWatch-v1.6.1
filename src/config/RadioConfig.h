#pragma once

#include <stdint.h>
#include <stddef.h>

// M1.1 retains the proven M0.5 PHY profile. All PHY parameters are
// centralised here so the watch and its peer RNode can be kept identical.

#ifndef RW_LORA_FREQUENCY_HZ
#define RW_LORA_FREQUENCY_HZ 920600000UL
#endif

#ifndef RW_LORA_BANDWIDTH_HZ
#define RW_LORA_BANDWIDTH_HZ 125000UL
#endif

#ifndef RW_LORA_SPREADING_FACTOR
#define RW_LORA_SPREADING_FACTOR 8
#endif

#ifndef RW_LORA_CODING_RATE
#define RW_LORA_CODING_RATE 5
#endif

#ifndef RW_LORA_SYNC_WORD
#define RW_LORA_SYNC_WORD 0x12
#endif

#ifndef RW_LORA_PREAMBLE_SYMBOLS
#define RW_LORA_PREAMBLE_SYMBOLS 18
#endif

#ifndef RW_LORA_TX_POWER_DBM
#define RW_LORA_TX_POWER_DBM 10
#endif

namespace rw::config::radio {

inline constexpr uint32_t kFrequencyHz = RW_LORA_FREQUENCY_HZ;
inline constexpr uint32_t kBandwidthHz = RW_LORA_BANDWIDTH_HZ;
inline constexpr uint8_t kSpreadingFactor = RW_LORA_SPREADING_FACTOR;
inline constexpr uint8_t kCodingRate = RW_LORA_CODING_RATE;
inline constexpr uint8_t kSyncWord = RW_LORA_SYNC_WORD;
inline constexpr uint16_t kPreambleSymbols = RW_LORA_PREAMBLE_SYMBOLS;
inline constexpr int8_t kTxPowerDbm = RW_LORA_TX_POWER_DBM;

inline constexpr size_t kRNodeHeaderBytes = 1;
inline constexpr size_t kPhysicalMtu = 255;
inline constexpr size_t kSinglePayloadMtu = kPhysicalMtu - kRNodeHeaderBytes; // 254
inline constexpr size_t kReticulumMtu = 500;
inline constexpr uint8_t kSplitFlag = 0x01;
inline constexpr uint8_t kSequenceUnset = 0xFF;
inline constexpr uint32_t kSplitTimeoutMs = 3000;

struct Profile {
    uint32_t frequencyHz{kFrequencyHz};
    uint32_t bandwidthHz{kBandwidthHz};
    uint8_t spreadingFactor{kSpreadingFactor};
    uint8_t codingRate{kCodingRate};
    uint8_t syncWord{kSyncWord};
    uint16_t preambleSymbols{kPreambleSymbols};
    int8_t txPowerDbm{kTxPowerDbm};
};

inline constexpr Profile defaultProfile() { return Profile{}; }

inline bool validProfile(const Profile &p) {
    const bool bw = p.bandwidthHz == 62500UL || p.bandwidthHz == 125000UL ||
                    p.bandwidthHz == 250000UL || p.bandwidthHz == 500000UL;
    return p.frequencyHz >= 150000000UL && p.frequencyHz <= 960000000UL &&
           bw && p.spreadingFactor >= 5 && p.spreadingFactor <= 12 &&
           p.codingRate >= 5 && p.codingRate <= 8 &&
           p.preambleSymbols >= 6 && p.preambleSymbols <= 64 &&
           p.txPowerDbm >= -9 && p.txPowerDbm <= 22;
}

inline uint32_t nominalBitrate(const Profile &p) {
    const uint32_t symbolsPerSecond = p.bandwidthHz / (1UL << p.spreadingFactor);
    return (symbolsPerSecond * p.spreadingFactor * 4UL) / p.codingRate;
}

static_assert(kPhysicalMtu == 255, "RNode physical MTU must remain 255 bytes");
static_assert(kSinglePayloadMtu == 254, "RNode single-frame payload must remain 254 bytes");
static_assert(kReticulumMtu == 500, "Reticulum protocol MTU must remain 500 bytes");
static_assert(kSpreadingFactor >= 5 && kSpreadingFactor <= 12, "LoRa SF must be 5..12");
static_assert(kCodingRate >= 5 && kCodingRate <= 8, "LoRa coding-rate denominator must be 5..8");
static_assert(kTxPowerDbm >= -9 && kTxPowerDbm <= 22, "SX1262 TX power must be -9..22 dBm");

inline constexpr float frequencyMHz() {
    return static_cast<float>(kFrequencyHz) / 1000000.0f;
}

inline constexpr float bandwidthKHz() {
    return static_cast<float>(kBandwidthHz) / 1000.0f;
}

// Approximate encoded LoRa payload bit rate used only for microReticulum
// interface prioritisation. It is not an airtime/duty-cycle calculation.
inline constexpr uint32_t nominalBitrate() {
    const uint32_t symbolsPerSecond = kBandwidthHz / (1UL << kSpreadingFactor);
    return (symbolsPerSecond * kSpreadingFactor * 4UL) / kCodingRate;
}

} // namespace rw::config::radio
