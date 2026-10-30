> **M6 build3 corrective note:** The build1 POSIX/chdir persistence design described below failed on hardware and is superseded by `M6-BUILD2-NOTES.md`. M6 build3 restores the proven NoopFS startup lifecycle and uses runtime-only routing state.

# M6 — Routing and Transport Notes

## Scope

M6 advances ReticulumWatch from a standalone endpoint toward an optional embedded Transport Node while preserving M5 as the accepted messaging/radio baseline.

### 1. Internal RNS persistence

`RNS_USE_FS` is enabled and microStore 0.1.7 uses `Adapters::PosixFileSystem`. Before `Reticulum::start()`, ReticulumWatch changes the POSIX working directory to `/reticulum/rns/internal`, initialises the adapter, then registers it with `RNS::Utilities::OS`.

The intent is to persist the stack's path table, known destinations and hashlist without moving or rewriting ReticulumWatch's own permanent application identity and LXMF history.

### 2. Optional Transport mode

The default is endpoint mode. `/app/transport.cfg` contains the user-selected mode. Settings -> Routing persists edits and requires reboot to rebuild Reticulum safely.

When Transport is active:

- `reticulum_.transport_enabled(true)` is set before `start()`;
- the LoRa interface is placed in `MODE_GATEWAY`;
- the UI/network diagnostics report `TRANSPORT`;
- the user is warned that forwarding traffic raises airtime and battery use.

M5 Wi-Fi/TCP remains a separate optional interface and is unchanged in M6.

### 3. Neighbor-probe rate-limit correction

Compiler output from the exact pinned 0.5.0 dependency exposed:

`static const uint8_t NEIGHBOR_PROBE_RATELIMIT = 300;`

The value overflows to 44. `scripts/patch_microreticulum_probe.py` accepts only that known declaration (or an already-fixed `uint16_t` form) and rewrites the storage type to `uint16_t`. Any unexpected source shape aborts the build.

### 4. Watch face

The date remains the same M5 18 px Montserrat font. LVGL 9.4's built-in Montserrat set stops at 48 px, so M6 retains the 48 px clock font and applies a 288/256 (~112.5%) transform scale to the time label instead of referencing a nonexistent 56 px built-in font.

## Hardware acceptance

First test with Transport still OFF:

1. boot and confirm persistence reports READY;
2. send/receive LXMF in both directions;
3. send and receive announces;
4. verify standby/wake behavior;
5. confirm the clock is larger while date sizing is unchanged.

Then enable Settings -> Routing -> Transport and reboot:

1. verify startup reports `mode=TRANSPORT`;
2. verify LoRa interface reports `MODE_GATEWAY`;
3. verify ordinary watch messaging still works;
4. if a suitable topology is available, prove forwarding between two nodes that cannot directly communicate (or between LoRa and the optional TCP interface).

Transport forwarding is not considered hardware-accepted until an actual forwarded packet/Link is observed through the watch.
