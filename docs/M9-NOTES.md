# M9 Notes — Audio and Experimental Communication

## Scope of this build

M9 begins with a hardware-audio foundation beside the accepted M8-build3 communicator. It does **not** yet claim interoperable audio messaging or LXST telephony.

Implemented:

- bounded 4-second PCM capture at 16 kHz / 16-bit / mono;
- 128 KiB PSRAM-only recording allocation;
- local speaker playback at 16 kHz / 16-bit stereo;
- dedicated audio worker task on core 0;
- `RECORD`, `PLAY`, `STOP`, and `CLEAR` Audio Lab controls;
- duration, peak and PSRAM UI diagnostics;
- speaker rail powered only for playback;
- non-fatal audio startup/failure policy;
- unchanged M8-build3 Reticulum/LXMF/radio/partition boundary.

Deferred until this local path is hardware-accepted:

- LXMF audio-field encoding/decoding;
- Codec2/Opus selection and transcoding;
- persistent voice-message attachment storage;
- LXST session/wire implementation;
- live push-to-talk and jitter buffering.

## Reference gate consulted

Before implementing M9, the following were checked:

- Official LXST reference: https://github.com/markqvist/lxst
- Official Sideband: https://github.com/markqvist/Sideband
- Reticulum reference/manual: https://github.com/markqvist/Reticulum and https://markqvist.github.io/Reticulum/manual/
- Ratspeak / rsLXST reference: https://github.com/ratspeak/rsLXST
- LilyGoLib T-Watch S3 implementation: https://github.com/Xinyuan-LilyGO/LilyGoLib
- Arduino-ESP32 I2S API: https://github.com/espressif/arduino-esp32/blob/master/docs/en/api/i2s.rst

Reference conclusions applied here:

1. Recorded asynchronous audio and LXST real-time telephony are separate engineering problems.
2. The T-Watch S3 microphone path is PDM and LilyGoLib configures it for 16 kHz, 16-bit, mono capture.
3. LilyGoLib's current IDF-5 amplifier path starts at 160000 Hz stereo, so ReticulumWatch explicitly normalizes TX to 16000 Hz stereo for this PCM monitor path.
4. Arduino I2S exposes blocking `readBytes()`/`write()` style streaming calls; they are kept off the main communicator loop.
5. No private Reticulum audio protocol is introduced before LXMF/LXST interoperability work is studied and implemented deliberately.

## Safety boundary

M9 does not edit any frozen endpoint-critical file. `scripts/release_preflight.py` retains exact SHA-256 checks for all frozen files and adds M9 audio isolation checks.

Audio failure is local. `main.cpp` starts Reticulum/LXMF before audio and does not include audio readiness in the critical `DEGRADED` communicator gate.

## Compile status

The assistant execution environment used to package this source does not have PlatformIO installed, and package installation is unavailable without network access. Therefore this source has static/preflight validation only. Do not call M9 compile-verified until:

```bash
pio run -t fullclean
pio run -e twatch_s3_16mb
```

succeeds on the user's toolchain.

## Hardware acceptance checklist

- `[m9:audio] ready` appears without affecting normal boot.
- Audio Lab opens.
- Recording reaches a non-zero duration and peak.
- Four-second automatic cap returns state to `CLIP READY`.
- STOP works during record and playback.
- Playback is audible and not severely distorted.
- CLEAR resets the clip.
- At least three LXMF TX and three RX messages still work after audio capture/playback.
- Incoming Link establishes and reply works after audio use.
- Manual announce is observed remotely and a peer announce is received.
- Connected standby RX and wake behavior remain working.
- Heap/PSRAM remain stable across repeated record/play/clear cycles.

## Next M9 step after acceptance

Use the accepted local audio path as the source/sink for interoperable recorded voice messaging. Study exact LXMF field conventions and Sideband behavior first; do not modify the frozen `LxmfService` merely to force audio through as opaque private bytes. Live LXST/PTT follows recorded-message interoperability, not the reverse.
