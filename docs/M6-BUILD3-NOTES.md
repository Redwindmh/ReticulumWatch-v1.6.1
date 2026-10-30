# M6 build3 — endpoint interoperability recovery

## Why this build exists

M6 build2 started Reticulum, but hardware logs showed a regression in real LXMF interoperability: many radio packets reached `Transport accepted`, while expected LXMF message/announce behavior was unreliable. Build3 therefore removes all M6 protocol/dependency deltas from endpoint operation.

## Recovery boundary

The following files are byte-for-byte restored from hardware-accepted M5:

- `src/reticulum/ReticulumService.cpp`
- `src/reticulum/ReticulumService.h`
- `src/config/RnsBuildConfig.h`
- `src/reticulum/WatchLoRaInterface.cpp/.h` (already unchanged)
- `src/lxmf/LxmfService.cpp/.h` (already unchanged)
- `src/radio/RadioService.cpp/.h` (already unchanged)

`platformio.ini` is also restored to the M5 dependency/build path except for `RW_MILESTONE=6`; the M6 probe patch hook is removed.

## Routing

Transport activation is locked OFF in build3. The Routing screen remains as documentation/UI scaffolding only. Transport will be reintroduced only after validating the official Reticulum interface-mode guidance, microReticulum `lora_transport` example and a working embedded implementation.

## References used during recovery

- Official Reticulum manual: non-Transport instances should generally leave interfaces at the default `full` mode.
- microReticulum project examples: `lora_announce`, `lora_transport`, `link_native`.
- microReticulum_Firmware: working self-contained embedded Reticulum implementation and provisioning/interface-mode model.
