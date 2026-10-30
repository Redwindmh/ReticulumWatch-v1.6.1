# M8 Source Audit

M8 is layered above the hardware-accepted M6-build3/M7 endpoint baseline.

## Frozen endpoint boundary

The following files are SHA-256 identical to the accepted endpoint baseline:

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
- both partition tables

The LMX1, LMX2 and LMX3 persisted journal structs are unchanged.

## M8 additions

- versioned CRC-protected `/app/user.cfg` sidecar;
- persistent brightness, haptics and notification preferences;
- two editable quick replies;
- local device label;
- cached-peer list visibility preference;
- conservative Manual / On Start / Every 6 Hours announce scheduling through the already-accepted `LxmfService::requestAnnounce()` path;
- reorganised User / Messaging / Watch / Reticulum settings;
- M7 public identity, raw LXMF-address QR and manual 32-hex contact exchange retained.

## Deliberate non-changes

- local device label does not alter LXMF announce app-data;
- Transport remains Endpoint-locked;
- message retention remains fixed at 256;
- private identity key material is never exposed in QR;
- raise-to-wake remains deferred.

## Static release gate

`python3 scripts/release_preflight.py` passes 101 checks before packaging. PlatformIO compilation and hardware acceptance remain separate gates.
