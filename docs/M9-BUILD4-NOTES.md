# M9-build4 Notes

## Why this build exists

M9-build3 compiled and flashed successfully on the LilyGo T-Watch S3, but hardware boot logs showed a repeated stack-canary panic before WatchUI initialisation:

```text
[m9:audio] ready ...
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
...
make_analysis_window -> codec2_create -> VoiceService::begin -> setup -> loopTask
```

The display was blank because the device rebooted before `watchUI.begin()` completed.

## Runtime correction

`VoiceService::begin()` now allocates the bounded decode buffer and starts `rw-voice`, but does not construct Codec2. The worker has a 32 KiB stack and performs `codec2_create(CODEC2_MODE_450)` plus frame-geometry discovery from inside that task. Encode/decode remain on the same worker. The main Arduino loop only dispatches completed voice sends through the existing LXMF service.

Voice remains optional: failure of the worker or codec leaves text Reticulum/LXMF available.

## FFat pressure handling

The same M9-build3 log reported an 8 MiB Reticulum FFat partition with zero free bytes and repeated failures to write derived message-ID and peer metadata. M9-build4 does not format or erase authoritative data.

When free space is below 256 KiB at boot, it:

1. logs logical usage summaries for known directories;
2. deletes only root `/lxmf/metadata/*.idx` and `*.idx.tmp` message-ID accelerator files;
3. defers rebuilding those accelerators for the current boot;
4. uses the existing retained-history scan fallback for message dedupe.

It does **not** automatically delete identity keys, LMX journal records, contacts/peer records, unread/delivery overlays, settings, corrupt quarantine entries, or voice sidecars.

## Protocol boundary

No frozen Reticulum, SX1262/radio, RNS configuration, or partition-map file is changed from the hardware-accepted baseline. LXMF voice remains standard `FIELD_AUDIO=0x07`, `AM_CODEC2_450=0x02`, active-Link only, with the existing signed-wire MDU guard.

## Acceptance targets

- boot reaches WatchUI without stack-canary panic;
- serial shows `Codec2 worker scheduled ... initialisation deferred off loopTask` and then `[m9:voice] ready ... stack=32768`;
- watch face appears;
- text announce/TX/RX remains functional;
- Audio Lab still records/plays;
- voice composer records/encodes without reset;
- storage-pressure diagnostics show whether derived-index purge reclaimed space;
- no authoritative/user storage is automatically erased.
