# M12-build2 — Crown/Lift Hardware Recovery

Parent M12 ZIP SHA-256: `017050c614a443de1c7f15871e8fc94c42ecf8f7114137a67a162f6f108c7e72`. The failed M12 artifact remains immutable.

## Trigger

M12 compiled/flashed but hardware testing showed that neither the crown nor Lift to Wake could wake the blanked display. The serial log confirmed that the setting was saved ON and the main loop continued running, but no crown or motion diagnostics appeared after connected standby.

## Root causes corrected

1. **Crown event observation was edge-unsafe.** M12 read AXP2101 status only when `PMU_INT` was still LOW at a later main-loop poll. The LilyGo reference implementation instead captures the falling edge and reads the latched PMIC status afterward. M12-build2 installs a tiny ISR that only latches the edge; normal-loop code reads/clears PMIC status. A 250 ms unconditional status poll is retained as a fallback. Long-press hardware power-off is untouched.
2. **Accelerometer access duplicated SensorLib.** M12 manually read/decoded BMA423 registers despite the sensor already being initialised by LilyGoLib. M12-build2 uses the exact pinned SensorLib 0.3.3 `SensorBMA423::getAccelerometer(int16_t&,int16_t&,int16_t&)` API.
3. **Lift debugging was opaque.** M12 logged only accepted candidates, so failed reads and insufficient movement were indistinguishable. M12-build2 reports a baseline, periodic X/Y/Z/magnitude/cosine samples while asleep, read failures, candidate transitions and accepted lift events.
4. **Threshold tuning was too conservative for an uncalibrated first hardware pass.** Candidate rotation is reduced from cosine 0.64 (~50 degrees) to 0.82 (~35 degrees), while a settled-orientation requirement remains.

## Hardware limitation

T-Watch S3 uses a Bosch BMA423 3-axis accelerometer. It does not provide gyro axes, so the diagnostic stream is 3-axis acceleration plus derived magnitude/orientation rather than true 6-axis IMU data.

## Acceptance

- build/flash M12-build2;
- enable Lift to Wake;
- allow standby;
- verify `[m12:motion]` samples change when moving/rotating the watch;
- short-press the crown and verify `[m12:crown]` plus `wake source=crown`;
- raise/rotate the watch and verify a candidate plus `wake source=lift`;
- confirm glass taps still do not wake the sleeping display;
- confirm incoming LXMF still wakes the screen and normal messaging remains functional.
