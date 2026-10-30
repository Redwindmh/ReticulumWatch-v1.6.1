# M10-build2 Source Audit

## Intentional changes from M10

- `src/storage/StorageService.{h,cpp}` — bounded RAM message/audio failover, RAM-first peer discovery/cache, best-effort low-space metadata writes, low-space boot-counter preservation.
- `src/hal/WatchHardware.{h,cpp}` — local/UTC RTC correction, bounded timestamp discipline, asynchronous strong message haptics.
- `src/audio/AudioService.{h,cpp}` — non-blocking RX/TX alert-tone state on the existing audio worker.
- `src/lxmf/LxmfService.{h,cpp}` — audio-feedback hook, strong RX/TX feedback, bounded incoming timestamp discipline; existing transport/wire semantics retained.
- `src/ui/WatchUI.cpp` — live Peers refresh and RAM-journal status indication.
- `src/main.cpp` — connects AudioService to LxmfService feedback only after optional audio initialisation.
- `src/config/BuildConfig.h` — immutable artifact version `M10-build2`.
- documentation/release preflight — new regression checks and observed-failure rules.

## Explicitly unchanged

Release preflight verifies SHA-256 identity for the hardware-accepted Reticulum/radio boundary and both partition tables. M10-build2 does not patch microReticulum.

LMX1, LMX2 and LMX3 persisted record structures are unchanged. Emergency records are RAM objects only and do not introduce a new on-disk journal version.

## Storage safety

No automatic format, erase, partition mutation or journal deletion was added. The emergency RAM journal is entered when persistent writes cannot maintain the existing 16 KiB reserve. New RAM sequences are intentionally volatile and are never presented as durable.

## Compile/hardware status

Static preflight and local syntax checks are not PlatformIO compilation. M10-build2 must not be described as compile-verified until the user runs the real T-Watch S3 build, and must not be hardware-accepted until the listed tests pass on-device.
