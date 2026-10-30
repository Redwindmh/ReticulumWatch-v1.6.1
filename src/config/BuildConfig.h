#pragma once

#include <stdint.h>

namespace rw::config {

inline constexpr const char *kProjectName = "Reticulum Watch";
inline constexpr const char *kVersion = "1.6.1";
inline constexpr uint8_t kInitialBrightness = 150;
inline constexpr uint32_t kSerialBaud = 115200;

#ifndef RW_EXPECTED_FLASH_MB
#define RW_EXPECTED_FLASH_MB 16
#endif
inline constexpr uint32_t kExpectedFlashMB = RW_EXPECTED_FLASH_MB;

} // namespace rw::config
