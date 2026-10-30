# ReticulumWatch M5 Source Audit

## Frozen M4 networking boundary

The following files must remain byte-for-byte identical to the hardware-accepted M4 baseline and are enforced by `scripts/release_preflight.py`:

- `src/radio/RadioService.cpp`
- `src/radio/RadioService.h`
- `src/config/RadioConfig.h`
- `src/reticulum/WatchLoRaInterface.cpp`
- `src/reticulum/WatchLoRaInterface.h`
- `src/reticulum/ReticulumService.cpp`
- `src/reticulum/ReticulumService.h`
- both partition CSVs

## New M5 surface

- `src/config/NetworkConfig.h`: reboot-applied Wi-Fi/TCP profile; default OFF.
- `src/transport/WatchTcpInterface.*`: standard Reticulum HDLC framing over TCP.
- `src/transport/TransportService.*`: opt-in Wi-Fi STA/client or AP/server startup and dynamic interface registration.
- `src/storage/StorageService.*`: `/app/network.cfg` sidecar only.
- `src/ui/WatchUI.*`: Interfaces settings, TCP status/counters, 48 px clock.
- `scripts/build_epoch.py`: injects a timezone-independent UTC build epoch for RTC fallback.
- `src/hal/WatchHardware.cpp`: uses the seeded system clock for Unix time if RTC data is invalid; fallback watch-face formatting uses JST without changing the Unix epoch.

## Protocol framing

The TCP implementation uses the standard Reticulum TCP interface framing: `0x7e` frame flags and `0x7d` escaping with XOR `0x20`. It deliberately does not use RNode/KISS framing.

## Persistence

No LMX journal structure was changed. Network settings are disposable configuration metadata and do not participate in message-history CRCs.
