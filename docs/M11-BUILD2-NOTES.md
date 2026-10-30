# M11-build2 — Recovery Resume + Canned Replies + History Wipe

Parent: `ReticulumWatch-M11.zip`  
Parent SHA-256: `0c6e1630abca27cfc48025bff26265eae0f27ae16b0ae5798637253ed6cb2de0`

## Why this build exists

The first real M11 repair run verified a complete 97-file / 30,384-byte off-device backup and then explicitly formatted only the `reticulum` FFat partition. Restore subsequently stalled after the first restored file and the next `PUT READY`, leaving a valid host backup but an interrupted on-device restore.

The repair log showed the host sent multiple restore DATA records without an application-level acknowledgement. M11-build2 makes restore traffic self-pacing and safely repeatable.

## Recovery reliability changes

- every accepted `M11 DATA` chunk now returns `RWR1 DATA OK <bytes-written>`;
- the host waits for that acknowledgement before sending the next chunk;
- host restore retries the whole restore session up to three times;
- retries use explicit `FORCE`, which is safe because each `PUT` removes/recreates its destination before writing;
- the original verified backup format and CRC32 checks are unchanged;
- restore completion still requires a 64-byte persistent Reticulum identity.

## Canned replies

The two old fixed quick-reply concepts are now presented as **Canned Reply 1** and **Canned Reply 2** under Settings → Messaging.

- both are user-editable up to the existing 24-character storage limit;
- blank disables a canned-reply slot;
- disabled slots are omitted from conversation and notification actions;
- the old untouched `OK` + `On my way` pair is migrated to two unset slots;
- previously customised values are preserved;
- the existing binary `user.cfg` layout is unchanged.

## Wipe chat history

Settings → Messaging now contains **WIPE CHAT HISTORY** with a separate confirmation screen.

The wipe removes:

- LXMF inbox/outbox records;
- unread markers;
- delivery overlays;
- audio sidecars;
- corrupt-message quarantine records;
- rebuildable message-ID indexes;
- the in-RAM emergency journal/audio state.

It deliberately preserves:

- `/rns/identity.key`;
- user/network/radio/power/interface settings;
- saved contacts and discovered-peer metadata.

A successful wipe resets the message high-water counter and reboots.

## Recovery note for the interrupted M11 repair

Do **not** run another fresh repair before restoring the already-verified backup. After flashing M11-build2, resume from the existing host backup with:

```bash
uv run --with pyserial python tools/rw_recovery.py restore \
  --port /dev/cu.usbmodemXXXX \
  --backup backups/watch-repair \
  --force
```

The restored identity is not considered accepted until the watch reports `RWR1 RESTORE OK` and reboots.
