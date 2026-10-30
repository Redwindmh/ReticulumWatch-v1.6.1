# M10 Notes — Stabilization and Recovery Foundation

## Accepted parent

Parent: `ReticulumWatch-M9-build5.zip`

SHA-256: `b119b44976ff241c6e28140f6dc6c20930f7018ab1ad72d068a76755d65bbd8a`

The user confirmed M9-build5 builds and flashes successfully and that watch-originated audio messages can be sent. Sideband playback remains silent on the tested phone. This is retained as an interoperability issue rather than used as justification for speculative Reticulum/LXMF changes.

## Goals in this M10 build

1. Preserve the frozen Reticulum/radio endpoint.
2. Make persistent storage failure non-destructive.
3. Preserve an emergency free-space reserve for authoritative journal writes.
4. Detect and recover from a wedged Arduino main loop without attempting filesystem repair from a supervisor task.
5. Add periodic runtime resource/latency diagnostics for long-duration testing.
6. Keep all M9 audio/voice functionality available for continued cross-client testing.

## Storage safety

M10 changes the FFat mount from auto-format-on-failure to preserve-on-failure. A mount failure is now a degraded-storage condition requiring explicit recovery.

The existing low-space cleanup remains deliberately narrow: only rebuildable message-ID index accelerators may be automatically removed. M10 additionally refuses new LXMF journal/audio writes when they would consume the final 16 KiB emergency reserve.

The current watch's FFat accounting anomaly is **not considered repaired**. Hardware logs showed only tens of KiB of visible application data while the filesystem reported almost no free space. M10 instrumentation is intended to make continued testing safer while a proper migration/repair path is designed.

## Supervisor

`HealthMonitor` is independent of Arduino `loopTask` and watches a main-loop heartbeat. It uses a 30 s grace period and a 30 s stale-heartbeat threshold. Recovery only logs and performs `esp_restart()`; it never calls FFat or protocol repair code from the supervisor task.

The normal loop prints health reports every 60 seconds. This creates evidence for heap/PSRAM degradation, unexpected loop latency and storage exhaustion during soak tests.

## Voice interoperability status

M9-build5 retained in M10:

- `FIELD_AUDIO=0x07`;
- outbound `AM_CODEC2_700C=0x03`;
- `[mode, encoded-bytes]` field shape;
- final active-Link MDU guard;
- dedicated 32 KiB Codec2 worker.

Sideband playback on the tested phone is not yet proven. Do not change the wire format again without receiver-side logs or another independent LXMF client test.
