# M13 — Release Candidate

## Goal

Prove that the hardware-accepted ReticulumWatch feature set is stable enough for the first stable release. Feature freeze applies. M13 must not destabilize the frozen Reticulum/LXMF/radio endpoint to chase optional behavior.

## Parent baseline

- Artifact: `ReticulumWatch-M12-build4.zip`
- SHA-256: `b7f54e87ab34092f8076b3a0aa02629bb780366aa325bb9858d2524db9505680`
- User hardware acceptance: lift-to-wake works; crown wake works; connected standby remains online.

## Functional freeze

M13 intentionally preserves M12-build4 behavior. In particular:

- BMA423 lift wake remains GPIO14 interrupt-driven.
- TILT + WAKEUP remain the accepted hardware wake events.
- Sleeping touch remains disabled as a wake source.
- Crown short press remains the AXP2101 display toggle.
- LoRa/Reticulum/LXMF stay active in connected standby.
- M6-build3 endpoint-critical Reticulum/radio boundary remains byte-for-byte frozen.
- M9 audio/voice remains optional and must never gate text messaging.
- M10 persistence failure still degrades to bounded RAM rather than a communications outage.
- M11 destructive storage recovery remains confirmation- and backup-gated.

## Lift sensitivity note

A lighter lift trigger is desirable, but the exact pinned SensorLib 0.3.3 BMA423 interface does not expose a public wake-sensitivity setter. The release candidate therefore does not fake sensitivity by changing range/ODR, add an any-motion wake, or return to raw XYZ polling. Both existing TILT and WAKEUP events are retained.

Any future sensitivity correction must be evidence-backed, narrow, and performed through a documented BMA423 feature configuration path.

## Acceptance matrix

### A. Boot / identity / storage

- Boot cleanly at least 10 times.
- Reticulum identity remains stable across all boots.
- No unexpected quarantine/index rebuild loop.
- Storage free-space telemetry remains sane.
- No automatic formatting occurs.

### B. Reticulum / LXMF soak

- At least 60 minutes continuous RX with ordinary announces present.
- At least 25 incoming LXMF messages and 25 outgoing replies/sends.
- Include active-Link and opportunistic/path-request sends.
- No persistent message duplication or unexplained loss caused by local firmware.
- Text communication remains usable before, during and after audio use.

### C. Connected standby / wake cycling

- At least 50 automatic screen-off cycles.
- At least 20 crown wake/toggle cycles.
- At least 20 natural lift-to-wake cycles with Lift to Wake enabled.
- Confirm sleeping glass taps do not wake the display.
- Confirm incoming LXMF can still wake/notify.
- Confirm selected brightness is restored after wake.

### D. Runtime health

Observe `[m10:health]` reports during the soak:

- no supervisor-triggered recovery;
- no monotonic heap collapse;
- no monotonic PSRAM collapse;
- loop gap remains far below the 30 s supervisor threshold during normal operation;
- loop stack high-water mark retains useful margin.

### E. Battery / thermal behavior

- Record starting/ending battery percentage and elapsed time for a representative connected-standby session.
- Repeat once with Lift to Wake enabled and once disabled if practical.
- Note unusual warmth, charging anomalies or rapid battery collapse.

M13 does not claim a laboratory battery-life figure from firmware alone; hardware observations are the acceptance evidence.

### F. Audio regression

- Local mic capture still works.
- Local playback still works.
- Voice recording does not starve Reticulum.
- Known Sideband cross-client playback uncertainty remains non-blocking unless new evidence identifies a local standards defect.

### G. Recovery sanity

- `M11 STATUS` remains responsive.
- Off-device backup can be initiated without altering ordinary files.
- Do not format merely to test M13 unless recovery is actually required.

## Release decision

If the matrix passes with no release-blocking defects, publish the first stable release from this line. Do not invent M14 for feature work before stable.
