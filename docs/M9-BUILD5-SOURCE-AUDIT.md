# M9-build5 Source Audit

## Protocol references checked

- LXMF standard audio field: field `0x07` with payload `[audio_mode, encoded_audio]`.
- Sideband public playback implementation: Opus/Ogg or Codec2 modes from 700C through 3200. Codec2-450 is outside this playback range.
- Codec2 stable C API: `CODEC2_MODE_700C`, `codec2_create`, `codec2_samples_per_frame`, `codec2_bytes_per_frame`; 700C uses 40 ms frames (320 samples at 8 kHz), 28 bits rounded to 4 bytes per frame.
- LXST public mirror: real-time telephony/two-way-radio support exists, but the project explicitly marks APIs early-alpha/unstable and not documented. Current Sideband releases include true half-duplex LXST PTT. We therefore do not invent call signalling in this build.

## Frozen boundary

The M8-build3 hardware-accepted Reticulum/radio files and both partition maps are verified by SHA-256 in release preflight and must remain byte-identical. M9-build5 does not patch microReticulum.

## Changed runtime surfaces

- `src/audio/VoiceService.{h,cpp}`: outbound 700C, legacy-450 local decode, worker-only codec switching.
- `src/ui/WatchUI.{h,cpp}`: conversation hold-to-talk PTT interaction and 700C status text.
- `src/config/BuildConfig.h`: immutable build version bump.

No LMX journal format change is introduced. Audio sidecars continue recording the standard LXMF audio mode byte and encoded bytes.
