# ReticulumWatch v1.6.1

ReticulumWatch is a standalone Reticulum/LXMF wrist communicator for the LilyGo T-Watch S3. The original M0-M13 roadmap is complete; post-1.0 work uses semantic versions.

## v1.6.1 — final maintenance release

v1.6.1 removes the old custom PTT-burst experiment from the conversation UI and keeps the interoperable recorded LXMF VOICE workflow.

- Removed the `PTT` button and its hold/release capture callbacks.
- Removed PTT-specific status text and state.
- Kept `VOICE` recording, local preview, Codec2 700C encoding, LXMF `FIELD_AUDIO` send, and stored voice playback.
- Reflowed the bottom chat row to `1 / 2 / VOICE / REPLY`, giving VOICE and REPLY more space.
- Retains the v1.6.0 photo-informed UI fit fixes, numeric canned replies, black/white chat theme, stronger general haptics, and dedicated keyboard haptics.
- Reticulum, radio, storage formats, wake/power behavior, and VoiceService wire semantics remain unchanged.

The removed PTT path was a custom bounded LXMF audio-burst interaction, not LXST PTT. Real interoperable Reticulum PTT belongs to LXST-capable clients such as Sideband/Partyline/rnphone and is outside this finished firmware scope.

## Build / test

```sh
pio run -t fullclean
pio run -e twatch_s3_16mb
```

Then flash and verify text TX/RX, VOICE record/preview/send/playback, Peer Details, standby RX, crown wake, lift wake, and keyboard haptics.

PlatformIO compilation and hardware acceptance are separate gates from the included static preflight.
