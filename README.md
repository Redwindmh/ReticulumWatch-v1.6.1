# ReticulumWatch v1.6.1

ReticulumWatch turns a LilyGO T-Watch S3 with an SX1262 LoRa radio into a standalone, wrist-worn [Reticulum](https://github.com/markqvist/Reticulum) and [LXMF](https://github.com/markqvist/LXMF) endpoint. It discovers peers, exchanges signed and encrypted text messages, records short Codec2 voice messages, keeps its identity and chat history on-device, and continues listening while the display is asleep. It does not need a phone, account, cloud service, or Internet connection for direct LoRa operation.

This repository contains the v1.6.1 final maintenance release. It is a working endpoint firmware, not an RNode peripheral and not a Reticulum Transport Node.

> **Radio and licensing notice**
>
> You are responsible for selecting a legal frequency, bandwidth, duty cycle, antenna, and transmit power for your hardware and jurisdiction. The compiled default is **920.600 MHz, 125 kHz, SF8, CR 4/5, 10 dBm**, which is deployment-specific and is not legal everywhere. This repository also has no top-level project license at present; see [Licensing](#licensing) before redistributing source or binaries.

## What it does

- Runs a complete embedded Reticulum endpoint through `microReticulum 0.5.0`.
- Uses the watch's SX1262 directly as a bidirectional Reticulum interface.
- Sends and receives LXMF text over opportunistic packets and active Reticulum Links.
- Discovers `lxmf.delivery` announces and keeps a local Peers/Contacts directory.
- Shows conversations, unread counts, delivery state, hop count, age, RSSI/SNR diagnostics, identity hashes, and address QR codes.
- Records, previews, sends, stores, and plays short LXMF `FIELD_AUDIO` voice clips using Codec2 700C. Legacy local Codec2 450 clips remain playable.
- Stores a persistent 64-byte Reticulum private identity, settings, peer metadata, message journal, unread state, delivery overlays, and audio sidecars on a dedicated FFat partition.
- Preserves communications under low-storage conditions with bounded RAM fallbacks instead of silently erasing identity or history.
- Supports optional Reticulum-over-Wi-Fi/TCP in station/client or access-point/server mode.
- Keeps LoRa and Reticulum running in connected standby while only the display sleeps.
- Provides USB-serial health diagnostics and a backup/restore recovery protocol.

## Reticulum's modus operandi

Reticulum is not one centrally operated mesh and it is not an IP replacement service that someone else hosts. It is a cryptography-first networking stack that lets people build many independent networks over whatever carriers they have: LoRa, packet radio, serial, Wi-Fi, Ethernet, TCP, I2P, or custom links.

The basic flow is:

1. **Identity:** each node creates a cryptographic identity. Applications derive destinations from that identity; LXMF uses an `lxmf.delivery` destination.
2. **Announce:** a destination emits a signed announce. Nearby Reticulum nodes learn that the destination exists and which interface leads toward it. Transport Nodes may propagate that knowledge across multiple hops.
3. **Path selection:** a sender addresses a short destination hash, not a phone number, DNS name, or server account. Reticulum selects a known next hop/interface or requests a path when needed.
4. **Delivery:** small packets can be sent opportunistically. A Reticulum Link can provide an encrypted, authenticated session with proofs and larger usable payloads.
5. **Application protocol:** LXMF supplies the signed message format on top of Reticulum. Reticulum moves packets; LXMF defines the message, timestamp, content, fields, and signature.

Private application traffic is encrypted for its destination and signatures bind data to cryptographic identities. Human-readable names are only local or application metadata; the destination hash is the durable address. Reticulum does not create physical connectivity by itself—two regions still need a radio path, cable, local network, or gateway between them. See the [Reticulum manual](https://github.com/markqvist/Reticulum/tree/master/docs) and [protocol repository](https://github.com/markqvist/Reticulum) for the authoritative model.

## How the firmware works

### Runtime architecture

| Layer | Implementation | Responsibility |
| --- | --- | --- |
| Hardware | `WatchHardware` / LilyGoLib | Display, touch, crown/PMU, RTC, accelerometer, haptics, microphone, speaker, PSRAM, and board validation |
| Power | `PowerManager` | Display timeout, crown toggle, optional lift-to-wake, and connected-standby accounting |
| Radio | `RadioService` / RadioLib | SX1262 setup, interrupt-driven RX, TX, CRC checks, signal metrics, and physical framing |
| Reticulum interface | `WatchLoRaInterface` | Feeds complete packets to microReticulum and queues half-duplex replies while the radio is busy |
| Network | `ReticulumService` / microReticulum | Persistent identity, destination creation, announces, Links, paths, proofs, and Reticulum packet processing |
| Messaging | `LxmfService` | LXMF packing/parsing, signature checks, duplicate suppression, peer discovery, sending, retries, and delivery state |
| Audio | `AudioService` + `VoiceService` | Non-blocking I2S record/playback and worker-task Codec2 encode/decode |
| Storage | `StorageService` | FFat mount, identity, CRC-protected settings, bounded journal, contacts, audio, and RAM fallback |
| Optional IP interface | `TransportService` / `WatchTcpInterface` | Wi-Fi station/TCP client or Wi-Fi AP/TCP server with HDLC framing |
| UI | `WatchUI` / LVGL | Watch face, conversations, peers, contacts, composer, QR codes, settings, and diagnostics |
| Supervision/recovery | `HealthMonitor` + `RecoveryService` | Runtime health, controlled restart behavior, USB backup, format gating, and verified restore |

Boot proceeds in dependency order: hardware, power policy, storage, radio, Reticulum, LXMF, optional Wi-Fi/TCP, optional audio/voice, UI, health monitor, and recovery console. Audio failure degrades only voice/audio features. A Reticulum or LXMF failure is reported as a degraded communicator instead of being hidden behind a normal boot message.

### LoRa packet path

The SX1262 uses explicit headers and a two-byte PHY CRC. The watch applies an RNode-compatible one-byte on-air header to each physical frame. A Reticulum packet up to 500 bytes fits into either one 254-byte payload or two split physical frames. The receiver reassembles matching split frames with a three-second timeout and then hands the original Reticulum bytes to microReticulum. After every received packet and every transmission, continuous receive mode is explicitly re-armed.

All communicating radios must use the same frequency, bandwidth, spreading factor, coding rate, sync word, and preamble length. A nearby LoRa signal is not enough; mismatched PHY settings cannot interoperate.

### Identity, messaging, and trust

On the first healthy storage boot, the firmware generates and persists a 64-byte private Reticulum identity in `/rns/identity.key`. The corresponding identity and LXMF destination hashes can be displayed or shared as QR codes; the private key is never placed in a QR code.

At boot, the firmware runs positive and negative signature-validation self-tests against the linked microReticulum implementation. A valid signature must pass, while a modified signature and modified message must fail. The LXMF endpoint does not start if this test fails.

Incoming LXMF messages are bounded, parsed as MessagePack, matched to this watch's destination, deduplicated by message ID, and recorded with their verification state. Outbound text prefers an existing active Link, otherwise it uses path discovery/opportunistic delivery with bounded retry and conservative receipt reporting. “Submitted” means the local stack accepted a send; “Delivered” is reserved for validated proof state.

### Recorded voice

The microphone records 16 kHz, 16-bit mono PCM into PSRAM. A dedicated 32 KiB worker task converts the bounded recording to Codec2 700C, places it in standard LXMF field `0x07`, and submits it only over an active Reticulum Link. Codec construction and audio I/O never run on Arduino's main loop task, so radio and messaging work continue.

This is recorded LXMF audio, not live push-to-talk and not LXST telephony. v1.6.1 intentionally removed the older custom PTT-burst UI. Receiver playback still depends on the other LXMF client supporting the same audio field/mode; delivery alone does not prove audible playback on every client.

### Storage and failure behavior

The dedicated partition is labelled `reticulum` and mounted at `/reticulum`. Application data is divided into `/app`, `/rns`, and `/lxmf`. Message records and settings use version markers and CRCs; authoritative message writes use a temporary file followed by rename. The journal retains up to 256 messages.

The firmware never automatically formats a failed volume. Under pressure it may discard only rebuildable message-ID index files. If a mounted volume cannot accept new messages, a bounded 16-message RAM journal and RAM audio cache keep live communication usable for that boot. RAM-only messages are lost on restart.

### Power behavior

The default display timeout is 30 seconds. The crown toggles the display. Optional lift-to-wake uses the BMA423 interrupt path. Touch intentionally does not wake a sleeping display. “Connected standby” means the display is off while the ESP32, SX1262, and Reticulum endpoint continue running; it is not ESP32 deep sleep.

## Supported hardware and defaults

Required target:

- LilyGO **T-Watch S3** with an **SX1262** LoRa module.
- ESP32-S3 with 8 MiB OPI PSRAM.
- Either 16 MiB flash (`twatch_s3_16mb`, default) or 8 MiB flash (`twatch_s3_8mb`).

Compiled defaults:

| Setting | Default |
| --- | --- |
| Frequency | 920.600 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | 8 |
| Coding rate | 4/5 |
| Sync word | `0x12` |
| Preamble | 18 symbols |
| TX power | 10 dBm |
| Reticulum MTU | 500 bytes |
| Serial speed | 115200 baud |
| Display timeout | 30 seconds |
| Announce policy | Manual |
| Wi-Fi/TCP | Disabled |
| Routing mode | Endpoint, locked |
| Watch timezone | JST (`JST-9`) |

The timezone is currently compiled in at `src/hal/WatchHardware.cpp`; protocol time remains Unix UTC, but the watch face and RTC conversion use Japan Standard Time.

## Install from source

### 1. Prerequisites

- Git.
- Python 3.9 or newer.
- [PlatformIO Core](https://github.com/platformio/platformio-core) or the [PlatformIO VS Code extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html).
- A data-capable USB cable.
- A T-Watch S3/SX1262 whose 8 MiB or 16 MiB flash size you know.

One convenient CLI installation is:

```sh
pipx install platformio
```

PlatformIO downloads the pinned ESP32 platform and libraries during the first build.

### 2. Clone and select the target

```sh
git clone https://github.com/Redwindmh/ReticulumWatch-v1.6.1.git
cd ReticulumWatch-v1.6.1
pio device list
```

Use `twatch_s3_16mb` for the normal 16 MiB watch. Use `twatch_s3_8mb` only for verified 8 MiB hardware. The firmware prints the actual and expected flash sizes at boot and reports a mismatch.

On macOS, explicitly choose the current `/dev/cu.usbmodem...` device; do not let PlatformIO select `/dev/cu.Bluetooth-Incoming-Port`. Linux commonly uses `/dev/ttyACM0` or `/dev/ttyUSB0`, and Windows uses a `COM` port.

### 3. Run the static checks and build

```sh
python3 scripts/check_partitions.py
python3 scripts/release_preflight.py
pio run -e twatch_s3_16mb
```

For 8 MiB hardware, replace the environment name with `twatch_s3_8mb`.

### 4. Flash the application

Replace the sample port with the value from `pio device list`:

```sh
pio run -e twatch_s3_16mb -t upload --upload-port /dev/cu.usbmodemXXXX
```

#### Fresh device or intentionally erased storage

A blank Reticulum partition must be formatted once. The repository includes an empty `data/` directory so PlatformIO can create a valid FFat image:

```sh
pio run -e twatch_s3_16mb -t uploadfs --upload-port /dev/cu.usbmodemXXXX
```

**`uploadfs` overwrites the entire Reticulum data partition. Run it only for a fresh/erased install. Never run it during a normal upgrade: it destroys the existing private identity, settings, contacts, messages, and saved audio.**

For an upgrade, flash only the application. The normal upload preserves the data partition:

```sh
pio run -e twatch_s3_16mb -t upload --upload-port /dev/cu.usbmodemXXXX
```

If normal upload cannot connect, follow LilyGO's official [T-Watch S3 download-mode procedure](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/lilygo-t-watch-s3.md#entering-download-mode-on-the-t-watch-s3). It requires access to the internal BOOT button; take care around the battery and its wires.

### 5. Monitor first boot

```sh
pio device monitor --port /dev/cu.usbmodemXXXX --baud 115200
```

A healthy boot reports matching flash size, mounted storage, an SX1262 profile, a passing signature self-test, a persistent identity, an LXMF delivery destination, and completed initialization. If storage says `mount failed (preserved)` on a truly fresh install, stop and upload the empty FFat image as described above. Do not format a watch that may contain an identity you need.

## First use

1. Configure the same legal RF profile on this watch and its peer(s). Radio changes are saved immediately but apply after reboot.
2. Open **MENU → ANNOUNCE** on each endpoint, or enable a conservative startup/six-hour announce policy under **SETTINGS → RETICULUM**. Repeated rapid announces are deliberately unhelpful because Reticulum rate-limits and deprioritizes them.
3. Open **PEERS** to view discovered `lxmf.delivery` destinations. Save a name or favorite to move a peer into **CONTACTS**.
4. Open a peer and start a conversation, or use **NEW MSG** and enter the exact 32-hex LXMF destination hash.
5. In a conversation, use **REPLY** for text, buttons **1** and **2** for configured quick replies, and **VOICE** for record/preview/send when an active Link exists.
6. Use **SETTINGS → IDENTITY** to view the public identity and LXMF address. Back up the device before destructive storage work.

The home screen shows local time, date, battery, charging state, and unread count. The crown sleeps or wakes the display. Lift-to-wake is optional; tap-to-wake is disabled.

## Settings and interfaces

### Radio

**SETTINGS → RADIO** edits frequency in 100 kHz steps, bandwidth, spreading factor, coding rate, TX power, and preamble. The sync word is displayed but fixed by the current UI. Saved changes require a reboot.

### Wi-Fi/TCP

**SETTINGS → INTERFACES** provides:

- `STA/TCP CLIENT`: join an existing Wi-Fi network and connect to a Reticulum TCP server.
- `AP/TCP SERVER`: create a Wi-Fi access point and listen for one TCP client.

The default TCP port is 4242. TCP frames use Reticulum-compatible HDLC escaping and synthesize a Reticulum tunnel after connection. Interface changes require a reboot. LoRa remains available when Wi-Fi/TCP is disabled.

### User, messaging, and watch

- Local display name, two short canned replies, and discovered-peer visibility.
- Notification popup and haptic toggles.
- Low/medium/high brightness.
- Display timeout: never, 15, 30, or 60 seconds.
- Optional lift-to-wake.
- Explicit chat-history wipe. This preserves the identity, settings, contacts, and peer metadata.

The display name is local UI metadata in this release; it does not change the frozen LXMF announce payload.

## Backup and recovery

Install `pyserial`, then use the host helper against the USB CDC port:

```sh
python3 -m pip install pyserial
python3 tools/rw_recovery.py status --port /dev/cu.usbmodemXXXX
python3 tools/rw_recovery.py capture --port /dev/cu.usbmodemXXXX --out backups/watch-YYYYMMDD
```

For a mounted but damaged/full filesystem, the guarded repair flow is:

```sh
python3 tools/rw_recovery.py repair --port /dev/cu.usbmodemXXXX --out backups/watch-YYYYMMDD
```

`repair` captures every authoritative file off-device, verifies size and CRC, obtains a same-session token, formats only the `reticulum` partition, restores every file, verifies again, and reboots. Formatting is refused until the backup completes. Read [tools/rw_recovery.py](tools/rw_recovery.py) before using `repair`; interrupted recovery and `--force` restoration are advanced operations.

This recovery path is for a mountable volume whose contents can be backed up. A truly new blank device should use the one-time `uploadfs` installation step instead.

## Build and release checks

```sh
pio run -t fullclean
python3 scripts/check_partitions.py
python3 scripts/release_preflight.py
pio run -e twatch_s3_16mb
pio run -e twatch_s3_8mb
```

The Python preflight is a static regression gate for the frozen Reticulum/radio boundary, partition layout, storage safety, voice constants, and UI invariants. A passing preflight is not a successful PlatformIO compile, and a successful compile is not physical hardware acceptance. The final hardware checklist is [docs/V1.6.1-HARDWARE-TEST.md](docs/V1.6.1-HARDWARE-TEST.md).

## Known boundaries

- Endpoint mode is locked. This firmware does not route other nodes' traffic as a Transport Node.
- It is not a USB/BLE RNode for a host computer.
- No LXST live voice/PTT implementation is included.
- Voice send requires an active Reticulum Link and is intentionally short enough for the supported Link payload.
- Resource transfer and large LXMF attachments are not implemented.
- The watch face timezone is hard-coded to JST in v1.6.1.
- Connected standby blanks the display but does not put the ESP32 into light/deep sleep.
- Wi-Fi credentials are stored in the watch's local FFat settings; treat physical access to the watch as trusted access.
- A destination hash proves where data is addressed, not the real-world identity of the person holding it. Verify important contacts out of band.

## Repository map

```text
boards/       PlatformIO board definitions for 16 MiB and 8 MiB watches
data/         Empty FFat seed used only for a fresh-install uploadfs
docs/         Architecture, milestones, source audits, and hardware tests
partitions/   Dedicated dual-OTA + Reticulum FFat layouts
scripts/      Build epoch, partition checks, and release preflight
src/audio/    I2S audio and Codec2 voice worker
src/config/   Build, RF, network, power, user, and RNS feature configuration
src/hal/      T-Watch S3 hardware ownership
src/lxmf/     LXMF messenger endpoint
src/power/    Connected-standby policy
src/radio/    SX1262 physical framing and reassembly
src/recovery/ USB recovery service
src/reticulum/  microReticulum integration and LoRa interface
src/storage/  Identity, journal, settings, peers, and recovery-safe storage
src/system/   Health supervision
src/transport/  Wi-Fi/TCP Reticulum interface
src/ui/       LVGL watch interface
tools/        Host-side recovery helper
variants/     LilyGO-derived T-Watch S3 pin map
```

## v1.6.1 release note

v1.6.1 removes the old custom PTT-burst experiment from the conversation UI while retaining the interoperable recorded **VOICE** workflow: local record/preview, Codec2 700C encoding, LXMF `FIELD_AUDIO` send, stored voice playback, and legacy Codec2 450 playback. The bottom row is now `1 / 2 / VOICE / REPLY`. Reticulum, radio framing, storage formats, power/wake behavior, and voice wire semantics are unchanged from v1.6.0.

## Authors, upstream projects, and references

ReticulumWatch firmware and project history are authored by [Malcolm Hendricks (`Redwindmh`)](https://github.com/Redwindmh). Git history is the authoritative record for individual ReticulumWatch contributions.

The following credits are based on upstream project metadata and source notices. When an upstream project identifies only an organization or GitHub handle, that is retained rather than guessing a legal name. Every linked project also includes work from its wider contributor community.

### Direct build/runtime dependencies

| Project used here | Version/source | Author or steward credited upstream | GitHub |
| --- | --- | --- | --- |
| microReticulum | `attermann/microReticulum 0.5.0` | Chad Attermann; contributors include Nils-Lucas Schönfeld | [attermann/microReticulum](https://github.com/attermann/microReticulum) |
| microStore | `attermann/microStore 0.1.7` | Chad Attermann and contributors | [attermann/microStore](https://github.com/attermann/microStore) |
| LilyGoLib | `xinyuan-lilygo/LilyGoLib 0.2.0` | LilyGO, Lewis He, and contributors | [Xinyuan-LilyGO/LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) |
| LVGL | `lvgl 9.4.0` | Gábor Kiss-Vámosi and LVGL contributors | [lvgl/lvgl](https://github.com/lvgl/lvgl) |
| RadioLib | `jgromes/RadioLib 7.4.0` | Jan Gromeš and contributors | [jgromes/RadioLib](https://github.com/jgromes/RadioLib) |
| XPowersLib | `lewisxhe/XPowersLib 0.3.1` | Lewis He and contributors | [lewisxhe/XPowersLib](https://github.com/lewisxhe/XPowersLib) |
| SensorLib | `lewisxhe/SensorLib 0.3.3` | Lewis He and contributors | [lewisxhe/SensorLib](https://github.com/lewisxhe/SensorLib) |
| TinyGPSPlus | `mikalhart/TinyGPSPlus 1.1.0` | Mikal Hart and contributors | [mikalhart/TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) |
| IRremoteESP8266 | LilyGO C++20-compatibility branch | Mark Szabo, Sebastien Warin, current maintainer David Conran, contributors; originally based on Ken Shirriff's Arduino-IRremote work | [crankyoldgit/IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) |
| ArduinoJson | `bblanchon/ArduinoJson 7.4.2` | Benoît Blanchon and contributors | [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) |
| MsgPack | `hideakitai/MsgPack 0.4.2` | Hideaki Tai and contributors | [hideakitai/MsgPack](https://github.com/hideakitai/MsgPack) |
| ArxContainer, ArxTypeTraits, DebugLog | MsgPack transitive dependencies | Hideaki Tai and contributors | [ArxContainer](https://github.com/hideakitai/ArxContainer), [ArxTypeTraits](https://github.com/hideakitai/ArxTypeTraits), [DebugLog](https://github.com/hideakitai/DebugLog) |
| Arduino Cryptography Library | `rweather/Crypto 0.4.0` | Rhys Weatherley and contributors | [rweather/arduinolibs](https://github.com/rweather/arduinolibs) |
| esp32_codec2 | `sh123/esp32_codec2 1.0.7` | GitHub author `sh123`; wraps Codec2 by David Rowe and Codec2 contributors | [sh123/esp32_codec2_arduino](https://github.com/sh123/esp32_codec2_arduino), [drowe67/codec2](https://github.com/drowe67/codec2) |

### Toolchain, board support, and reference implementations

| Project/reference | How it informed this firmware | Author or steward | GitHub |
| --- | --- | --- | --- |
| Reticulum | Protocol, destination, announce, Link, path, proof, and interface reference | Mark Qvist and contributors | [markqvist/Reticulum](https://github.com/markqvist/Reticulum) |
| LXMF | Signed message format and standard fields | Mark Qvist and contributors | [markqvist/LXMF](https://github.com/markqvist/LXMF) |
| RNode Firmware | RNode-compatible LoRa framing and ecosystem reference | Mark Qvist and contributors | [markqvist/RNode_Firmware](https://github.com/markqvist/RNode_Firmware) |
| Sideband | LXMF client interoperability and Codec2 audio-mode reference | Mark Qvist and contributors | [markqvist/Sideband](https://github.com/markqvist/Sideband) |
| microReticulum Firmware | Embedded Reticulum lifecycle, transport, and provisioning reference | Chad Attermann, Nils-Lucas Schönfeld, and contributors | [attermann/microReticulum_Firmware](https://github.com/attermann/microReticulum_Firmware) |
| rsDeck / rsCardputer | Embedded Reticulum UI/configuration comparison used during milestone audits | Ratspeak project contributors | [ratspeak/rsDeck](https://github.com/ratspeak/rsDeck), [ratspeak/rsCardputer](https://github.com/ratspeak/rsCardputer) |
| LilyGoLib-PlatformIO | T-Watch S3 board, pin, and dependency baseline | LilyGO and contributors | [Xinyuan-LilyGO/LilyGoLib-PlatformIO](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO) |
| PlatformIO Core | Build, dependency resolution, upload, monitor, and filesystem targets | PlatformIO team; project founded/maintained by Ivan Kravets with contributors | [platformio/platformio-core](https://github.com/platformio/platformio-core) |
| pioarduino platform-espressif32 | Pinned ESP32 PlatformIO platform | pioarduino maintainers and contributors; forked from PlatformIO's ESP32 platform | [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) |
| Arduino core for ESP32 | Arduino framework and ESP32-S3 support | Espressif Systems and contributors | [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) |

See [THIRD_PARTY.md](THIRD_PARTY.md) and each upstream repository for authoritative copyright notices, contributor lists, and license text.

## Licensing

There is currently **no top-level `LICENSE` file** granting rights for the original ReticulumWatch source. Public visibility on GitHub is not itself a software license. Until the owner deliberately selects and adds a compatible license, normal copyright defaults apply to the project's original code.

Third-party dependencies retain their own licenses. In particular, `sh123/esp32_codec2` identifies itself as **GPL-3.0** and is linked into voice-enabled firmware. Anyone distributing firmware binaries must review the GPL-3.0 source/notice obligations and compatibility with every other component. This README and [THIRD_PARTY.md](THIRD_PARTY.md) are attribution aids, not legal advice.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [v1.6.1 release notes](docs/V1.6.1-RELEASE-NOTES.md)
- [v1.6.1 source audit](docs/V1.6.1-SOURCE-AUDIT.md)
- [v1.6.1 hardware test](docs/V1.6.1-HARDWARE-TEST.md)
- [Release checklist](docs/RELEASE-CHECKLIST.md)
- [Project mistake ledger](docs/MISTAKE-LEDGER.md)
