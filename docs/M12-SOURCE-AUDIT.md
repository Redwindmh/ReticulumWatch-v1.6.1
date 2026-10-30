# M12 Source Audit

Parent: `ReticulumWatch-M11-build2.zip`  
Parent SHA-256: `4761bc1458ee490fef886db8ed5891a8a4f8566555cc06170cf51aa0d3312485`

## Protocol boundary

The following remain byte-identical to the frozen accepted hashes enforced by `scripts/release_preflight.py`:

- `src/reticulum/ReticulumService.cpp/.h`
- `src/reticulum/WatchLoRaInterface.cpp/.h`
- `src/radio/RadioService.cpp/.h`
- `src/config/RnsBuildConfig.h`
- `src/config/RadioConfig.h`
- both partition CSVs

## Intentional runtime changes

- `src/config/PowerConfig.h`: persistent lift-to-wake flag, default OFF.
- `src/storage/StorageService.cpp`: backward-compatible RW4 power-profile parser/writer.
- `src/hal/WatchHardware.cpp/.h`: AXP2101 short-crown event latch, read-only BMA423 raw-vector helper, brightness retention.
- `src/power/PowerManager.cpp/.h`: crown display toggle, removal of sleeping touch wake, opt-in lift detector.
- `src/ui/WatchUI.cpp/.h`: power settings toggle and revised wake-policy text.
- version/docs/preflight metadata.

## External source checks

Before implementation, M12 checked LilyGo's T-Watch S3 hardware documentation for the AXP2101 PMU and BMA423 address/pins, and XPowersLib examples for the short power-key interrupt API. The raw accelerometer path performs reads only; it does not change BMA423 feature configuration.

## Verification boundary

Static/source audit is not PlatformIO compilation and not hardware acceptance. The artifact must be built/flashed and crown/lift behavior tested on the user's watch before M12 is accepted.
