# M11 Source Audit

## Parent

- `ReticulumWatch-M10-build3.zip`
- SHA-256 `f19248b3fd5ed4e222d4adbbd906b719dc8a4bf26a216484f461727e179418d1`
- M10-build3 was hardware-accepted by the user before M11 work began.

## Frozen boundary

M11 does not intentionally change the accepted protocol/radio boundary:

- `src/reticulum/ReticulumService.cpp/.h`
- `src/reticulum/WatchLoRaInterface.cpp/.h`
- `src/radio/RadioService.cpp/.h`
- `src/config/RnsBuildConfig.h`
- `src/config/RadioConfig.h`
- both partition maps

## M11 additions

- `src/recovery/RecoveryService.cpp/.h`
  - USB-serial backup/restore protocol;
  - per-file CRC32 verification;
  - partition/FAT/logical-use diagnostics;
  - same-session backup token gate before format;
  - explicit FORCE restore path for interrupted recovery;
  - 64-byte identity presence check before successful restore/restart.
- `tools/rw_recovery.py`
  - verified backup capture;
  - safe repair orchestration;
  - verified restore;
  - standalone status/capture/restore commands.
- `StorageService::enterRecoveryMaintenance()`
  - disables ordinary persistent application operations while recovery owns FFat.
- `HealthMonitor::setMaintenanceMode()`
  - prevents the M10 loop supervisor from restarting the watch during an intentional long serial backup/restore operation.

## FFat API check

The Arduino-ESP32 FFat API exposes non-destructive mount via `begin(false, ...)`, unmount via `end()`, and explicit `format(...)` while unmounted. M11 uses explicit format only after a verified backup token; normal boot still uses the preservation-first non-formatting mount path.

## No protocol claim

M11 is storage/recovery work only. It does not change Reticulum packet framing, radio configuration, LXMF wire encoding, voice codec selection, or partitions.
