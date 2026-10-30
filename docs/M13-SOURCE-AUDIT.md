# M13 Source Audit

## Change boundary

M13 is a release-candidate/package milestone. Relative to M12-build4, intended source changes are limited to:

- `src/config/BuildConfig.h` — version string `M13`;
- `platformio.ini` — `RW_MILESTONE=13`;
- release-candidate documentation/preflight expectations.

No Reticulum, LXMF, radio, storage layout, partition, audio transport or wake-path source file is intentionally changed.

## Frozen endpoint

The release preflight continues to hash-lock the accepted Reticulum/radio boundary:

- `src/reticulum/ReticulumService.*`
- `src/reticulum/WatchLoRaInterface.*`
- `src/radio/RadioService.*`
- `src/config/RnsBuildConfig.h`
- `src/config/RadioConfig.h`
- both partition maps

## M12 wake path

M13 preserves M12-build4 BMA423 acquisition qualification and GPIO14 TILT/WAKEUP interrupt wake logic byte-for-byte.

## Verification status

Static preflight, partition checks, target compile and package/hash results are recorded at packaging time. Hardware acceptance remains separate and requires the M13 acceptance matrix in `docs/M13-NOTES.md`.
