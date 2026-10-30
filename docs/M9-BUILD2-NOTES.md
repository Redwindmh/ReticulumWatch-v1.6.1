# M9-build2 Notes — Recorded Voice over LXMF

## Parent acceptance

Parent: `ReticulumWatch-M9.zip`

SHA-256: `730c2c5137c4e90a5ebc4bb8f3cd83f75b2bd4315b4f647fb49483da339639ef`

The user hardware-tested M9 and confirmed local microphone recording and speaker playback. A follow-up concurrency test confirmed Reticulum packet RX, RNS injection/acceptance, Link/LXMF handling and Reticulum TX while both recording and playback were active. Normal text messaging remained healthy afterward.

That test closes the local-audio gate and permits recorded-message transport work above the endpoint boundary.

## Reference decisions

M9-build2 uses the established LXMF audio field instead of a ReticulumWatch-specific packet:

- `FIELD_AUDIO = 0x07`;
- value shape: `[audio_mode, encoded_bytes]`;
- `AM_CODEC2_450 = 0x02` on the LXMF wire.

Codec implementation:

- `sh123/esp32_codec2 @ 1.0.7`;
- `CODEC2_MODE_450` codec instance;
- codec input/output at 8 kHz;
- watch hardware audio stays on the already accepted 16 kHz PCM path, with bounded 2:1 downsample / 1:2 expansion around Codec2.

References consulted include the official LXMF source/project, Reticulum API/manual, the ESP32 Codec2 Arduino library, and working Reticulum voice implementations. Live LXST is intentionally still separate.

## MDU correction made during release audit

An early M9-build2 source pass allowed up to 384 encoded voice bytes. Release review caught that Reticulum's encrypted packet MDU is 383 bytes, while the signed LXMF wire data already carries 96 bytes of destination/source/signature plus the MessagePack payload structure.

That pre-release design was corrected before an artifact was issued:

- `VoiceService::kMaxEncodedBytes = 240`;
- preferred voice recording cap = 3000 ms;
- actual voice recording cap is additionally derived from runtime Codec2 frame size;
- `LxmfService::sendVoiceMessage()` checks the final `wire.size()` against the active Link's real `get_mdu()` before sending.

The local Audio Lab retains its tested four-second / 128 KiB buffer. Only network voice composition receives the shorter cap.

## Why no Resource transfer yet

Larger voice fields would normally require a larger-transfer mechanism rather than forcing them into a single encrypted packet. Reticulum provides Resources over Links, but this project uses pinned microReticulum 0.5.0 and the current interoperability history contains Resource-specific fixes that have not yet been proven on this watch baseline.

M9-build2 therefore does **not** enable Resource transfer or invent application fragmentation. Voice is active-Link, single-packet and fail-closed. This is intentionally narrow.

## Architecture

```text
T-Watch mic
    |
    v
AudioService (16 kHz PCM in PSRAM)
    |
    | beginCodecRead()
    v
VoiceService worker (core 0)
    |
    +-- downsample 16 kHz -> 8 kHz
    +-- Codec2 450 encode
    |
    v
VoiceService::loop() on normal service thread
    |
    v
LxmfService::sendVoiceMessage()
    |
    +-- standard FIELD_AUDIO 0x07
    +-- standard mode 0x02
    +-- signed LXMF wire
    +-- active-Link MDU guard
```

The codec worker never performs Reticulum sends. Network submission happens only from the ordinary service loop after encoding completes.

Inbound is the reverse: `LxmfService` verifies the normal LXMF message, extracts standard audio metadata, persists the immutable journal record plus a CRC-protected audio sidecar, and `VoiceService` later decodes that sidecar for playback on demand.

## Persistence contract

LMX1, LMX2 and LMX3 structs are unchanged. Audio bytes live in:

`/lxmf/metadata/audio/<sequence>.aud`

The sidecar is versioned and CRC protected. It stores the LXMF audio mode and encoded payload length/data. Retention pruning removes sidecars at the same sequence cutoff as journal records.

## Current UX

Conversation footer adds `VOICE`.

Voice compose:

- RECORD;
- PLAY;
- STOP;
- SEND;
- Codec2 and LXMF field diagnostics;
- safe maximum duration shown from actual codec geometry.

Thread history marks messages with audio sidecars as voice entries and offers `PLAY LATEST VOICE` for the newest visible voice entry. Per-message playback buttons are deferred to a later UI refinement.

## Expected limitations

- Voice send requires an already-active Link/reply route.
- No outgoing-Link creation is added solely for voice in this build.
- Only Codec2-450 playback is supported by the watch in this build.
- Larger audio attachments, Resource delivery and Opus are deferred.
- Live LXST/PTT remains a different future milestone.

## Acceptance checklist

Compile:

```bash
pio run -t fullclean
pio run -e twatch_s3_16mb
```

Hardware/interoperability:

- voice service starts without gating text messenger;
- active Link exists to test peer;
- record <= displayed safe cap;
- local preview plays;
- SEND produces an MDU-safe `FIELD_AUDIO` message;
- peer recognises/plays Codec2-450 if supported;
- inbound standard Codec2-450 voice is persisted and playable on watch;
- reboot retains received/sent voice sidecars alongside history;
- normal text TX/RX still works after voice encode/decode/playback;
- standby/wake and announce behavior remain unchanged.

Do not call M9-build2 hardware-accepted until these tests pass.
