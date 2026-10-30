> **M6 build2 corrective note:** The build1 POSIX/chdir persistence design described below failed on hardware and is superseded by `M6-BUILD2-NOTES.md`. M6 build2 restores the proven NoopFS startup lifecycle and uses runtime-only routing state.

# M6 Source Audit

## Frozen M5 acceptance boundary

M6 intentionally leaves these M5 source files byte-for-byte unchanged:

- `src/radio/RadioService.cpp/.h`
- `src/config/RadioConfig.h`
- `src/reticulum/WatchLoRaInterface.cpp/.h`
- `src/lxmf/LxmfService.cpp/.h`
- `src/transport/TransportService.cpp/.h`
- `src/transport/WatchTcpInterface.cpp/.h`
- `src/power/PowerManager.cpp/.h`
- both partition CSV files

These contain the hardware-accepted M5 LoRa PHY/framing, LXMF send/receive/discovery, Wi-Fi/TCP implementation and connected-standby behavior.

## Intentional M6 changes

- `platformio.ini`: enable RNS/microStore POSIX persistence, path-store sizing, M6 milestone macro, probe patch hook.
- `src/config/RnsBuildConfig.h`: enable path/known-destination/hashlist persistence while retaining provisioning opt-out.
- `src/config/TransportConfig.h`: endpoint/Transport user setting; default false.
- `src/storage/StorageService.*`: `/rns/internal` directory and atomic `/app/transport.cfg` sidecar.
- `src/reticulum/ReticulumService.*`: initialise/register POSIX persistence; optional Transport enable; LoRa gateway mode only when Transport is active.
- `src/ui/WatchUI.*`: larger clock and Settings -> Routing/reboot flow; Network diagnostics expose endpoint/Transport and persistence state.
- `src/main.cpp`: load transport profile before Reticulum startup.
- `scripts/patch_microreticulum_probe.py`: strict correction of the known 300->44 overflow in the pinned dependency.

## Compatibility invariants

- LMX1, LMX2 and LMX3 persisted message structures are unchanged from M5.
- Transport defaults OFF.
- M5 network interface profile defaults remain unchanged.
- Default LoRa profile remains 920.600 MHz / 125 kHz / SF8 / CR 4/5 / sync 0x12 / preamble 18 / 10 dBm.
- App identity and LXMF journal are not migrated into microReticulum's internal persistence tree.
- No live routing-mode mutation; a reboot is required.

## Verification status

Static release preflight and partition checks can be performed in the assistant environment. PlatformIO is not installed there, so the actual ESP32-S3 compile remains a user-side release gate. Transport forwarding additionally requires a multi-node hardware acceptance test.
