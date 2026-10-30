# M6 build3 corrective notes

## Failure in M6 build1

Hardware log showed `chdir(/reticulum/rns/internal) failed`, followed by `Reticulum=RNS persistence cwd failed; LXMF=RNS unavailable`. Raw SX1262 packets still arrived but were not drained because ReticulumService was not ready, causing `completed-packet slot busy; dropped newest packet`.

## Correction

- Restored the hardware-accepted M5 microStore `NoopFileSystem` lifecycle.
- Disabled `RNS_USE_FS` and POSIX persistence in this corrective build.
- Kept M6 Transport Node mode; route/path state is runtime-only.
- Optional routing persistence can no longer gate Reticulum startup.
- UI labels explicitly say `Route store: RAM`.

## Acceptance

1. Boot must report registered `NoopFileSystem`.
2. Boot must reach `Reticulum started as endpoint` (or transport node if selected).
3. No `M6-build3 DEGRADED` line.
4. RX packets must show `[rns:rx] injecting LoRa packet`, not repeated `completed-packet slot busy`.
5. Announce and quick reply must emit `[rns:tx] Transport emitted packet`.
