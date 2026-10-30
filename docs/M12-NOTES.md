# M12 — Final UX & Power

## Goal

Finish the watch-native wake/display interaction without touching the accepted communicator boundary. M12 is the final feature milestone before the M13 release candidate.

## User-visible changes

- Tap-to-wake removed.
- Crown short press toggles display on/off.
- Lift-to-wake setting added under Watch → Standby; default OFF.
- Incoming message wake remains.
- User brightness survives display sleep/wake.

## Crown design

The T-Watch S3 crown is the AXP2101 power key. M12 enables only the PMIC short-click interrupt and polls the board PMU IRQ line. It does not repurpose a GPIO and does not disable or replace the PMIC long-press power-off path.

## Lift design

M12 does not rewrite the BMA423 feature-engine configuration late in the project. While the display is off and the setting is enabled, PowerManager samples the existing BMA423 acceleration vector at 10 Hz. A >= ~50° gravity-vector rotation creates a candidate; ~200 ms of settled orientation completes the wake. A 650 ms post-screen-off guard and 1.8 s candidate timeout reduce immediate bounce and transient knocks.

This detector is opt-in and intentionally hardware-tunable. Its acceptance criterion is useful wrist wake without destabilising Reticulum or causing frequent accidental wakes.

## Persistence

Old power files `RW4,<timeout>` remain valid and imply lift-to-wake OFF. M12 writes `RW4,<timeout>,<0|1>`.

## Frozen boundary

ReticulumService, WatchLoRaInterface, RadioService, RadioConfig, RnsBuildConfig and both partition maps are unchanged from the accepted baseline.

## Next

M13 is Release Candidate: soak/stress, battery profiling, remaining UI corrections, documentation and bug fixes only. A passing M13 becomes the stable release.
