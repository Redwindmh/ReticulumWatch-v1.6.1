# M9 Source Audit

## Baseline

Source parent: `ReticulumWatch-M8-build3.zip`

Expected parent SHA-256:

`b0df3232e84ced3b7bff03c64addbdf2550e5f99505e0e3657fca7f5e6e1ee54`

The parent ZIP was recovered from the user's Library and its SHA-256 matched before editing.

## Frozen boundary

M9 release preflight checks byte-for-byte SHA-256 equality for:

- `src/reticulum/ReticulumService.cpp`
- `src/reticulum/ReticulumService.h`
- `src/reticulum/WatchLoRaInterface.cpp`
- `src/reticulum/WatchLoRaInterface.h`
- `src/lxmf/LxmfService.cpp`
- `src/lxmf/LxmfService.h`
- `src/radio/RadioService.cpp`
- `src/radio/RadioService.h`
- `src/config/RnsBuildConfig.h`
- `src/config/RadioConfig.h`
- `partitions/reticulum_watch_16mb.csv`
- `partitions/reticulum_watch_8mb.csv`

## Intended changed surface

- `src/audio/AudioService.cpp`
- `src/audio/AudioService.h`
- `src/main.cpp`
- `src/ui/WatchUI.cpp`
- `src/ui/WatchUI.h`
- `src/config/BuildConfig.h`
- `platformio.ini`
- release/preflight and documentation files

No journal layout, identity storage, Reticulum interface mode, LoRa framing, announce encoder, Link behavior, radio profile, or partition layout is intentionally changed.
