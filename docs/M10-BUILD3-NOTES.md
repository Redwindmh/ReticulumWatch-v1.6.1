# M10-build3 Notes — Receive Chime Cadence

M10-build3 is a narrow notification refinement on the hardware-tested M10-build2 reliability baseline.

## Change

- Message receive sound is now six short identical synthesized beeps: 3 quick beeps, a short group pause, then 3 quick beeps.
- Tone is generated at runtime by the existing AudioService worker; no sampled copyrighted audio is bundled.
- RX haptic remains the strong three-pulse pattern from M10-build2.
- TX tone and two-pulse TX haptic are unchanged.
- Reticulum/LXMF/radio/storage/time behaviour is unchanged.

## Cadence

At 16 kHz: 55 ms beep, 30 ms gap, 55 ms beep, 30 ms gap, 55 ms beep, 145 ms pause, then the same three-beep group. Pitch is 1400 Hz. Total duration is 595 ms.

## Acceptance

1. Receive a text message over LXMF.
2. Confirm the audible cadence is three quick beeps, pause, three quick beeps.
3. Confirm strong RX haptic still occurs.
4. Send a message and confirm TX sound is unchanged.
5. Confirm normal RX/TX remains functional.
