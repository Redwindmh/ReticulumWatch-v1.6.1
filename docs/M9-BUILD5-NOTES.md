# M9-build5 Notes

## Scope

M9-build5 is the Sideband-interoperability and PTT-burst follow-up to hardware-accepted M9-build4.

### User-observed M9-build4 acceptance

The hardware log demonstrated:

- clean boot with Codec2 on the 32 KiB worker;
- UI reaches ready state;
- normal LXMF Link/text receive remains functional;
- 3 s Codec2-450 capture encoded to 225 bytes;
- standard LXMF `FIELD_AUDIO` message submitted and persisted;
- radio/Reticulum remained functional after voice send.

Sideband received the message but did not appear able to play it. Reference review explains why: Sideband's public audio playback branch accepts Codec2 audio beginning at `AM_CODEC2_700C` through `AM_CODEC2_3200`; M9-build4 sent `AM_CODEC2_450`. The LXMF field shape `[mode, bytes]` remains correct.

## Changes

1. Outbound audio is now `FIELD_AUDIO=0x07`, `AM_CODEC2_700C=0x03`.
2. Existing local mode `0x02` (Codec2 450) sidecars remain decodable.
3. Codec mode switching occurs only on the dedicated 32 KiB `rw-voice` worker.
4. The outbound clip bound remains 240 encoded bytes and final signed-wire size is still checked against the live active-Link MDU. Codec2 700C runtime geometry yields about a 2.4 s maximum burst with the current bound.
5. Conversation threads add hold-to-talk PTT: press begins bounded capture; release stops and queues a normal LXMF `FIELD_AUDIO` message.
6. This is an LXMF PTT-burst experiment, not a claim of LXST telephony. Full LXST call setup is deferred until the exact current signalling contract can be verified rather than guessed.

## M9-build4 storage-pressure observation retained

The watch still reports an almost-full FFat despite the traversable application data accounting for only tens of KiB. M9-build5 does not format or erase the filesystem. Full filesystem integrity/migration work belongs to M10 stabilization.

## Hardware acceptance targets

- Build/flash/boot succeeds.
- Boot log reports outbound mode `0x03` / Codec2-700C.
- Manual VOICE message appears/playable in Sideband.
- PTT press/release produces `[m9:ptt]` logs and a Sideband-playable voice message.
- Existing M9-build4 mode-0x02 sidecar still plays locally when selected.
- Text LXMF send/receive still works afterward.
