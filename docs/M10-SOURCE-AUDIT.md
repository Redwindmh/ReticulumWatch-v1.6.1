# M10 Source Audit

## Frozen protocol/radio boundary

The release preflight verifies exact SHA-256 values for the accepted Reticulum/radio boundary and both partition maps. M10 intentionally does not modify those files.

## M10 intentional changes

- `src/system/HealthMonitor.{h,cpp}` — independent loop supervisor and periodic diagnostics.
- `src/main.cpp` — starts HealthMonitor after UI initialisation, emits loop heartbeat and calls non-blocking diagnostics.
- `src/storage/StorageService.{h,cpp}` — non-destructive FFat mount, live free-space refresh, emergency write-reserve guard.
- `src/ui/WatchUI.cpp` — M10 version diagnostic and low-storage indicator only; navigation/protocol behaviour unchanged.
- `src/config/BuildConfig.h` / `platformio.ini` — milestone/version metadata.
- documentation and release-preflight updates.

## Explicitly unchanged behaviour

- LoRa framing and split packet implementation.
- microReticulum endpoint startup path.
- announce handling.
- LXMF signing, addressing and text-message wire format.
- M9 `FIELD_AUDIO` construction/parsing.
- Codec2 mode selection and worker architecture.
- LMX1/LMX2/LMX3 persisted journal layout.
- partition maps.

## Safety rules

- No automatic FFat format on mount failure.
- No supervisor-task filesystem writes.
- No low-space deletion of authoritative/user data.
- Refuse growth writes before the emergency reserve is exhausted.
- Health-monitor failure must not gate communicator startup.
