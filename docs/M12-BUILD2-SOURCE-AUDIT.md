# M12-build2 Source Audit

Parent: immutable M12 source artifact, ZIP SHA-256 `017050c614a443de1c7f15871e8fc94c42ecf8f7114137a67a162f6f108c7e72`.

Changed runtime files:

- `src/config/BuildConfig.h` — version only.
- `src/hal/WatchHardware.cpp/.h` — PMU IRQ edge latch/status fallback, crown diagnostics, pinned SensorLib BMA423 read API.
- `src/power/PowerManager.cpp/.h` — motion telemetry and tuned rotation/settle detector.

Protocol/radio/storage/message-format boundaries remain unchanged. No Reticulum, LXMF wire format, RadioService, WatchLoRaInterface, partition map, journal record or identity format changes are part of this correction.

Reference grounding:

- LilyGo T-Watch S3 identifies GPIO21 as the AXP2101 PMU interrupt and BMA423 as the 3-axis accelerometer.
- LilyGoLib's T-Watch S3 implementation attaches a falling-edge ISR on `PMU_INT`, then reads PMIC IRQ status later.
- pinned SensorLib 0.3.3 exposes `SensorBMA423::getAccelerometer(int16_t&, int16_t&, int16_t&)`.

Static/source validation is not a PlatformIO compile and not hardware acceptance. The user's real build and the new diagnostic lines are the acceptance evidence.
