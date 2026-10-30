# M12-build4 — BMA423 acquisition/liveness correction

## Hardware evidence from M12-build3

M12-build3 established that connected standby is not stopping the application CPU: health telemetry and the 1 Hz motion heartbeat continued for many minutes. The crown path also produced repeated PMIC interrupt edges and successfully toggled the display.

Lift-to-wake nevertheless remained dead. Every BMA423 diagnostic read returned API success with XYZ exactly `(0,0,0)`, while GPIO14 accumulated zero interrupt edges. Therefore the remaining fault is below the gesture decision: accelerometer acquisition was not demonstrably live.

## Correction

SensorLib 0.3.3 exposes `configAccelerometer(...)` and `enableAccelerometer()` separately. M12-build4 explicitly configures the BMA423 for a feature-safe 100 Hz continuous stream, enables acquisition, then samples six times. Lift-to-wake may arm only if configuration succeeds, enabling succeeds, at least one read succeeds, and gravity/motion produces a non-trivial vector.

TILT/WAKEUP and GPIO14 remain the authoritative wake event. The liveness samples are diagnostic/qualification data only and never form a polling wake detector.

## Acceptance

M12-build4 is accepted only if hardware shows both:

- non-zero XYZ values after accelerometer bring-up; and
- at least one natural lift producing a BMA423 TILT or WAKEUP interrupt and display wake.

Crown behavior is a regression check, not a target of this correction.
