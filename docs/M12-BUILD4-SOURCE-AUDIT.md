# M12-build4 Source Audit

## Parent

- Parent: `ReticulumWatch-M12-build3.zip`
- Parent SHA-256: `96c95fca068c2822cef7b599565e7043ffccf882c5f73918b758a6b6eb8187b2`
- Parent remains immutable.

## Intentional source changes

- `src/hal/WatchHardware.cpp/.h`
  - add BMA423 acquisition liveness gate;
  - call exact pinned SensorLib 0.3.3 `configAccelerometer(...)` and `enableAccelerometer()` APIs;
  - require non-zero XYZ evidence before TILT/WAKEUP IRQ arming;
  - add explicit bring-up diagnostics.
- version/release documentation and release-preflight metadata.

## Frozen boundary

No intentional changes to Reticulum service, LoRa interface, radio service/configuration, RNS build configuration, LXMF wire/journal formats, audio/voice transport, partition maps, crown event implementation, or notification behavior.

## Verification boundary

Static/source checks do not constitute PlatformIO or hardware acceptance. The release remains pending an actual T-Watch S3 build and physical lift test.
