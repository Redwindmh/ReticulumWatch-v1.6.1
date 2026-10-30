# M12-build3 Source Audit

Parent: `ReticulumWatch-M12-build2` (immutable failed lift-wake hardware iteration).

Functional changes are limited to the M12 power/hardware wake path:

- `src/hal/WatchHardware.{h,cpp}`: BMA423 GPIO14 ISR latch, feature re-arm, feature-status decode, lift event latch.
- `src/power/PowerManager.{h,cpp}`: consume hardware lift event; remove raw-acceleration polling detector; explicit CONNECTED-IDLE state logging.
- version/docs/preflight/manifest bookkeeping.

Protocol-critical Reticulum/radio files and partition maps remain frozen. LMX1/LMX2/LMX3 persistent record layouts are unchanged.

Static/API evidence checked for the pinned BMA423 interface:

- `SensorBMA423::FEATURE_TILT`
- `SensorBMA423::FEATURE_WAKEUP`
- `SensorBMA423::INT_TILT`
- `SensorBMA423::INT_WAKEUP`
- `enableFeature(...)`
- `enableTiltIRQ()`
- `enableWakeupIRQ()`
- `readIrqStatus()`

Static preflight is not a PlatformIO compile or hardware acceptance.
