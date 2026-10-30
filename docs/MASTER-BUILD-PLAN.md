# ReticulumWatch — Master Build Plan and Project Handoff

## 1. Project Definition

**ReticulumWatch** is a standalone firmware project for the **LilyGo T-Watch S3** that turns the watch into a self-contained **Reticulum/LXMF communicator**.

It is **not** an accessory for a smartphone and is **not** part of the Game Watch firmware.

The finished watch should be able to participate directly in Reticulum networks using its onboard SX1262 radio, exchange LXMF messages, maintain its own identity and message history, discover peers, operate through additional Reticulum interfaces, and function as a useful wearable communicator without requiring a phone or computer.

The long-term goal is essentially:

> **A Reticulum-native smartwatch / wrist communicator.**

The project should remain modular so that radio/networking, messaging, storage, UI, power management, and optional interfaces can evolve independently.

---

# 2. Target Hardware

Primary target:

**LilyGo T-Watch S3**

Confirmed hardware:

| ComponentHardware |                         |
| ----------------- | ----------------------- |
| MCU               | ESP32-S3 @ 240 MHz      |
| Flash             | 16 MiB on current watch |
| PSRAM             | 8 MiB OPI               |
| Radio             | SX1262                  |
| Display           | 240×240                 |
| Touch             | FT6X36                  |
| PMU               | AXP2101                 |
| Accelerometer     | BMA423                  |
| RTC               | PCF8563                 |
| Haptics           | DRV2605                 |
| Connectivity      | Wi-Fi + BLE + USB       |
| Audio             | Microphone + speaker    |

SX1262 pins:

| SignalGPIO |   |
| ---------- | - |
| SCK        | 3 |
| MISO       | 4 |
| MOSI       | 1 |
| CS         | 5 |
| RESET      | 8 |
| BUSY       | 7 |
| DIO1 / IRQ | 9 |

Both **16 MiB** and **8 MiB** T-Watch S3 variants should remain supported by the repository even though the current development watch is confirmed as 16 MiB.

---

# 3. Architectural Principles

These are project-level rules and should survive future milestone work.

### Networking must not depend on the GUI

Reticulum, radio reception, LXMF processing, persistence, and protocol timing must continue working regardless of which screen is displayed.

### Hardware ownership must be centralized

There should be one service responsible for each hardware subsystem.

For example:

```text
RadioService
    owns SX1262

StorageService
    owns persistent application storage

PowerManager
    coordinates power policy

WatchUI
    owns LVGL screens/widgets

ReticulumService
    owns the Reticulum runtime/interface

LxmfService
    owns LXMF behavior

```

Do not scatter direct hardware operations across UI or application code.

### Interrupt handlers must remain minimal

Interrupt context should only signal that work is pending.

For example:

```text
DIO1 interrupt
    ↓
set packetReady flag
    ↓
return

```

SPI, parsing, allocations, logging, Reticulum processing, filesystem access, and UI work happen outside the ISR.

### Persistent identity is critical user data

A firmware update must not casually replace the user's Reticulum identity.

The watch's identity should be created once and restored on subsequent boots.

### Milestones should be narrow and testable

A milestone should introduce a small number of new failure modes.

Do not simultaneously add:

```text
new radio code
+ new storage
+ new UI architecture
+ new power management
+ new protocol

```

unless they genuinely cannot be separated.

### Proven artifacts are immutable baselines

Do not recreate or silently alter an already-tested version number.

If `M0.5` is confirmed working, later fixes become a new version.

### Build verification claims must be literal

Do not say a firmware ZIP is "build verified" unless it was actually compiled.

Static inspection is not the same as compilation.

---

# 4. Core Software Architecture

The intended architecture is:

```text
┌─────────────────────────────────────────────┐
│                  Watch UI                   │
│                                             │
│ Watch face · Inbox · Conversations          │
│ Contacts · Peers · Composer · Settings      │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│             Application Services            │
│                                             │
│ Identity · Contacts · Message DB            │
│ Notifications · Peer discovery              │
│ Power policy · Configuration                │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│                    LXMF                     │
│                                             │
│ Receive · Send · Delivery states · History  │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│               microReticulum                │
│                                             │
│ Destinations · Packets · Paths · Links      │
│ Announces · Transport                       │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│             Reticulum Interfaces            │
│                                             │
│ SX1262 · Wi-Fi/TCP · BLE · USB/RNode        │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│                Watch Hardware               │
│                                             │
│ PMU · Display · Touch · IMU · RTC            │
│ Haptics · SX1262 · Audio                    │
└─────────────────────────────────────────────┘

```

Suggested service boundaries:

```text
src/
├── config/
├── hal/
│   └── WatchHardware
├── power/
│   └── PowerManager
├── radio/
│   └── RadioService
├── reticulum/
│   ├── ReticulumService
│   └── Reticulum interfaces
├── lxmf/
│   └── LxmfService
├── storage/
│   └── StorageService
├── contacts/
├── messaging/
├── notifications/
├── transport/
├── ui/
└── main.cpp

```

---

# 5. Master Milestone Plan

The implementation should be considered a sequence of major milestones rather than continuing to turn every bug fix into a new conceptual milestone.

---

## M0 — Platform and Reticulum Foundation

**Purpose:** Establish the watch as a stable standalone Reticulum-capable embedded device.

This foundation is substantially complete.

### M0.1 — Hardware Bring-Up

Required:

- initialize T-Watch S3 hardware;
- display;
- touch;
- PMU;
- accelerometer;
- RTC;
- haptics;
- PSRAM detection;
- flash-size detection;
- board variants;
- stable boot;
- no accidental radio transmission.

**Status: COMPLETE**

---

### M0.2 — Persistent Storage and Radio Ownership

Required:

- dedicated Reticulum/application FAT partition;
- `/app`;
- `/rns`;
- persistent boot counter;
- explicit `RadioService` ownership of SX1262;
- radio initialization;
- radio standby;
- 8 MiB and 16 MiB partition maps.

**Status: COMPLETE**

---

### M0.3 — microReticulum Core and Persistent Identity

Required:

- microReticulum starts reliably;
- microStore registration contract satisfied;
- Reticulum event loop runs;
- permanent 64-byte watch identity;
- identity loaded after reboot;
- stable identity hash;
- stable application destination;
- no RF required yet.

**Status: COMPLETE**

Important established behavior:

```text
/reticulum/rns/identity.key

```

contains the persistent application identity.

---

### M0.4 — Reticulum LoRa Receive Path

Required:

```text
SX1262
→ DIO1
→ RadioService
→ RNode framing
→ microReticulum interface
→ Reticulum

```

Includes:

- interrupt-driven RX;
- RNode-compatible one-byte framing;
- physical LoRa MTU handling;
- logical Reticulum MTU;
- split-frame reassembly;
- RSSI/SNR diagnostics;
- Reticulum packet injection.

**Status: CORE PATH COMPLETE**

Real OTA packets were received and accepted by microReticulum.

Single-frame reception is confirmed.

The deliberate >254-byte split-frame acceptance test should still be retained as a regression test.

---

### M0.5 — Reticulum LoRa Transmission

Required:

```text
Reticulum
→ WatchLoRaInterface
→ RNode framing
→ SX1262
→ external RNode

```

Includes:

- RNode-compatible TX framing;
- split TX support;
- return to RX after TX;
- application announce;
- external Reticulum node recognizing the watch.

**Status: COMPLETE**

Confirmed result:

The watch transmitted an announce and the Raspberry Pi reported the watch destination as **one hop away via the RNode interface**.

This is the milestone that proves the T-Watch is genuinely an independent Reticulum endpoint.

---

# M1 — LXMF Messaging Core

**Purpose:** Turn the Reticulum endpoint into an actual communicator.

This is the milestone currently in progress.

Do not jump into elaborate smartwatch UI until the underlying message endpoint works reliably.

---

## M1.1 — LXMF Endpoint and Receive Pipeline

Required:

- create `lxmf.delivery` destination from the permanent watch identity;
- announce LXMF destination;
- receive opportunistic LXMF;
- decode LXMF payload;
- determine sender;
- retain LXMF message ID;
- persist received message;
- increment inbox count;
- notify UI;
- haptic incoming-message notification.

Expected pipeline:

```text
LoRa packet
    ↓
Reticulum
    ↓
lxmf.delivery
    ↓
LxmfService
    ↓
LXMF decode
    ↓
MessageStore
    ↓
NotificationService
    ↓
Inbox UI

```

### Current state

The watch currently creates:

```text
lxmf.delivery
4d9615ebfb00b614fa6102448e629b0d

```

The Pi can address it.

During the first test, the watch physically received and microReticulum accepted incoming packets:

```text
[radio:rx] ...
[rns:rx] accepted packet ...

```

but the expected LXMF destination callback did **not** fire.

Therefore:

**CURRENT BLOCKER:** trace the Reticulum destination/callback routing between accepted packets and the `lxmf.delivery` receive callback.

Do not modify the radio layer to solve this unless evidence shows the radio layer is responsible.

**Status: IN PROGRESS**

---

## M1.2 — Persistent Inbox and Message History

Once M1.1 works:

Required:

```text
/reticulum/lxmf/
├── inbox/
├── outbox/
└── metadata/

```

Store at minimum:

- message ID;
- source identity/destination;
- destination;
- timestamp;
- title;
- content;
- incoming/outgoing;
- delivery state;
- verification state.

Requirements:

- survive reboot;
- avoid duplicate messages;
- restore latest inbox entries;
- bounded storage / cleanup policy;
- corruption handling.

---

## M1.3 — Outgoing LXMF

Required:

- construct LXMF message;
- send opportunistically;
- eventually support DIRECT/Link delivery;
- show sending state;
- show delivered/failed state where available;
- preserve outbound history.

Acceptance test:

```text
Watch
→ LXMF
→ Reticulum
→ SX1262
→ Pi/RNode
→ Sideband/NomadNet/LXMF client

```

A human-readable message written on the watch must appear correctly on another LXMF client.

---

## M1.4 — LXMF Security Hardening

Before displaying a sender as cryptographically verified:

- move from the pinned microReticulum revision containing the known `Identity::validate()` problem;
- verify upstream fix;
- regression-test Reticulum radio operation;
- verify valid signatures;
- verify intentionally modified signatures fail;
- distinguish:
  - unknown;
  - known;
  - verified;
  - verification failed.

Do not label unvalidated messages as trusted.

---

# M2 — Messenger User Experience

**Purpose:** Turn the working LXMF endpoint into something comfortable to use on a watch.

**M2.6.0 integration status:** M2.2–M2.6 are consolidated into one integration milestone: conversations, contacts, discovered peers, composer/quick replies, and proof/retry delivery state. M2.1.0 is the frozen hardware-accepted substrate.

---

## M2.1 — Inbox

UI should provide:

- unread indicator;
- sender;
- short preview;
- time;
- newest-first list;
- open conversation;
- read/unread state.

---

## M2.2 — Conversations

Conversation screen:

```text
Contact / identity
──────────────────

Incoming message

             Outgoing reply

Incoming message

──────────────────
Reply

```

Required:

- chronological thread;
- incoming/outgoing distinction;
- scroll;
- delivery state;
- persistent history.

---

## M2.3 — Contacts

Contacts should associate human-friendly information with Reticulum identities.

Store:

- display name;
- identity hash;
- LXMF destination;
- optional notes;
- last seen;
- trusted/verification state;
- favorite/pinned state.

Contacts must persist independently of transient discovered peers.

---

## M2.4 — Peer Discovery

Use Reticulum announces to populate a **Discovered Peers** view.

Required distinction:

```text
Discovered Peer
≠
Saved Contact

```

Features:

- recently heard identities;
- RSSI / SNR where meaningful;
- last seen;
- destination type/aspects;
- save as contact;
- expire stale discovery records.

---

## M2.5 — Message Composer

Create a watch-friendly composer.

The touchscreen keyboard is a major feature, not a temporary debug keyboard.

Requirements:

- alphabet;
- numbers;
- punctuation;
- backspace;
- shift/case;
- space;
- send/cancel;
- usable 240×240 layout;
- text scrolling/editing.

---

## M2.6 — Quick Replies

Provide configurable canned responses such as:

```text
OK
Yes
No
On my way
Received
Call you later

```

Users should eventually be able to edit these.

Quick replies should permit responding without opening the full keyboard.

---

# M3 — Watch-Native Experience

**Purpose:** Make ReticulumWatch behave like a watch rather than an embedded development board with a screen.

---

## M3.1 — Watch Face

Primary idle screen:

- time;
- date;
- battery;
- Reticulum/radio state;
- unread count;
- connection/interface indicators.

Optional later information:

- signal;
- next event;
- peer count;
- last-message indicator.

---

## M3.2 — Navigation Model

Establish consistent navigation for:

```text
Watch Face
Inbox
Conversations
Contacts
Peers
Compose
Network
Settings

```

Touch gestures/buttons should behave consistently.

Do not let every screen invent its own navigation semantics.

---

## M3.3 — Notifications

Incoming LXMF should:

- trigger haptic pattern;
- wake display where appropriate;
- show sender/preview;
- allow dismiss;
- allow quick reply;
- increment unread state.

Different haptic patterns may eventually distinguish:

- message;
- delivery failure;
- system warning;
- peer/event notification.

---

# M4 — Power Management and Wearable Behaviour

**Purpose:** Make the firmware practical on battery.

The original intended power model has four levels:

```text
1. ACTIVE
2. CONNECTED STANDBY
3. DUTY-CYCLED RADIO
4. DEEP SLEEP

```

---

## M4.1 — Active Mode

Display on, UI interactive, normal networking.

Optimize only after profiling.

---

## M4.2 — Connected Standby

**M4 implementation:** display-only connected standby is now the default everyday mode. Automatic CPU light sleep remains deferred until DIO1/Reticulum wake behavior is proven.


Display off while Reticulum remains able to receive messages.

Wake sources:

- touch/button;
- SX1262 DIO1;
- RTC;
- accelerometer.

This is likely the most important everyday watch mode.

---

## M4.3 — Duty-Cycled Radio

Optional battery-saving profile.

Radio listens periodically rather than continuously.

Must communicate the resulting delivery trade-off clearly.

---

## M4.4 — Deep Offline Sleep

Maximum battery preservation.

Reticulum radio may be unavailable.

Wake from:

- button;
- RTC;
- configured accelerometer event.

---

## M4.5 — Raise to Wake

Use the BMA423.

Requirements:

- sensible wrist-motion detection;
- debounce;
- orientation filtering;
- avoid accidental wake loops;
- must coexist with networking.

Later gestures may be explored only after raise-to-wake is reliable.

---

# M5 — Additional Reticulum Interfaces

**Purpose:** Make the watch useful beyond direct LoRa.

The Reticulum runtime should support multiple interfaces without changing LXMF/application logic.

---

## M5.1 — Wi-Fi / TCP Reticulum

Use cases:

- connect to local Reticulum instance/router;
- high-speed synchronization;
- message transfer without LoRa airtime;
- home-network integration.

Possible modes:

```text
Wi-Fi client
→ TCP Reticulum interface

Wi-Fi AP
→ local Reticulum clients

```

Exact server/client behavior should be specified before implementation.

---

## M5.2 — USB RNode Mode

Allow a connected computer to use the watch's SX1262 as an RNode-style radio interface.

Must not compromise normal standalone mode.

Possible operating choice:

```text
Standalone Watch
USB RNode

```

or controlled simultaneous operation if architecture allows it safely.

---

## M5.3 — BLE Interface / RNode Mode

Allow suitable Reticulum clients to communicate through the watch over BLE.

Potential use cases:

- Sideband;
- laptop/phone client;
- configuration;
- local high-speed data path.

BLE work should remain separate from Android notification mirroring; this firmware's core purpose is Reticulum.

---

## M5.4 — Interface Selection / Multipath Behaviour

Provide network settings that show:

- LoRa;
- Wi-Fi;
- BLE;
- USB;
- enabled/disabled state;
- current activity.

Application code should not care which interface carries an LXMF message.

---

# M6 — Reticulum Routing and Transport Features

**Purpose:** Explore the watch as more than an endpoint.

This comes **after normal endpoint behavior is mature**.

---

## M6.1 — Proper Reticulum Internal Persistence

Replace the temporary no-op microStore backend.

Persist what Reticulum needs for:

- caches;
- known destinations;
- transport identity;
- path data;
- provisioning state where relevant.

The application's permanent identity remains separately protected.

---

## M6.2 — Transport Node Capability

Optional mode allowing the watch to forward Reticulum traffic.

Requirements:

- explicit user setting;
- battery implications shown;
- correct interface modes;
- persistence;
- path behavior;
- forwarding tests.

Do not silently enable Transport Node behavior by default.

---

## M6.3 — Fix Upstream Probe Rate-Limit Issue

The pinned microReticulum version contains:

```cpp
static const uint8_t NEIGHBOR_PROBE_RATELIMIT = 300;

```

which overflows to `44`.

This must be corrected or moved to a fixed upstream revision **before relying on neighbor probing/Transport behavior**.

---


### M6 implementation status — ReticulumWatch M6

Integrated in the M6 source package, pending compile/hardware acceptance:

- internal microReticulum POSIX persistence rooted at `/reticulum/rns/internal`;
- path, known-destination and hashlist persistence enabled;
- application identity/LXMF journal remain separately owned;
- Transport mode is explicitly opt-in, saved in `/app/transport.cfg`, reboot-applied and OFF by default;
- LoRa is switched to `MODE_GATEWAY` only when Transport is active;
- Routing UI displays a forwarding/battery warning;
- pinned 0.5.0 `NEIGHBOR_PROBE_RATELIMIT` 8-bit overflow is corrected by a strict/idempotent pre-build patch;
- M5 LoRa/LXMF/Wi-Fi TCP/power code remains frozen;
- watch clock is slightly enlarged while date font remains unchanged.

Transport forwarding is not considered complete until actual multi-node forwarding is demonstrated on hardware.

# M7 — Identity and Exchange Tools

---

## M7.1 — Identity Screen

Show:

- identity hash;
- LXMF address;
- relevant destination addresses;
- copy/export options where practical.

---

## M7.2 — QR Codes

Generate QR codes for:

- LXMF address;
- identity;
- contact exchange;
- potentially configuration information.

Goal:

Two ReticulumWatch users should eventually be able to exchange contact information by showing/scanning QR codes.

If camera scanning is impossible on-watch, showing codes remains useful for phones or other clients.

---

## M7.3 — Contact Import/Export

Potential transports:

- QR;
- USB;
- BLE;
- file;
- Reticulum message.

Specification should precede implementation.

---

# M8 — Configuration and User Settings

## M8 implementation status — hardware test pending

M8 consolidates persistent user settings without changing the hardware-accepted M6-build3/M7 endpoint core. Implemented: local device label, public identity/address QR tools, editable quick replies, notification toggle, persistent brightness, haptics toggle, connected-standby timeout access, conservative announce policy (Manual / On Start / Every 6 Hours), peer-list visibility, and existing radio/interface configuration. Transport remains endpoint-locked.


Settings should eventually expose:

### Radio

- frequency/profile;
- bandwidth;
- spreading factor;
- coding rate;
- TX power;
- interface enabled state.

### Reticulum

- announce behavior;
- interface selection;
- Transport mode;
- peer discovery.

### Messaging

- display name;
- quick replies;
- notification preferences;
- message retention.

### Watch

- brightness;
- screen timeout;
- raise-to-wake;
- haptics;
- power profile.

Avoid requiring source-code edits for ordinary device configuration.

---

# M9 — Audio and Experimental Communication — FUNCTIONAL; INTEROP FOLLOW-UP OPEN

**Purpose:** Explore the T-Watch S3's microphone and speaker only after text communication is mature.

M9 local audio is hardware-accepted: 16 kHz/16-bit mono capture, four-second PSRAM buffer, speaker playback, and concurrent Reticulum RX/TX all worked on the watch. M9-build5 also builds/flashes and sends standards-shaped LXMF `FIELD_AUDIO` Codec2-700C voice plus bounded hold-to-talk bursts over an existing Link. Sideband on the currently tested phone receives the messages but has not produced audible playback; that is retained as an explicit interoperability issue requiring receiver-side or cross-client evidence before another wire-format change.

Potential experiments:

- recorded voice clips over LXMF;
- compressed audio;
- RatSpeak-style communication;
- LXST or related Reticulum streaming approaches;
- push-to-talk experiments.

This milestone is intentionally late because continuous or near-real-time audio has very different bandwidth and power characteristics from text LXMF.

Audio experiments must not destabilize the core messenger.

---

# M10 — Stabilization and Release — COMPLETE / HARDWARE-ACCEPTED THROUGH BUILD3

M10 began from the working M9-build5 communicator and is hardware-accepted through M10-build3. It established non-destructive FFat mounting, emergency RAM-journal/audio failover, RAM-first peer discovery, independent loop supervision, clock discipline, strong RX/TX haptics, and distinct audible message feedback. The communicator remains functional even when FFat reports zero free bytes.

Before calling ReticulumWatch generally usable:

- watchdog/recovery strategy;
- malformed packet resilience;
- filesystem corruption handling;
- identity backup/export;
- migration strategy;
- OTA/update strategy;
- battery profiling;
- long-duration RX test;
- repeated send/receive stress test;
- memory-leak testing;
- UI soak testing;
- radio recovery after errors;
- sleep/wake cycling;
- interface switching;
- documentation.

A release should include:

```text
firmware
source
build instructions
configuration documentation
recovery procedure
identity backup instructions
radio profile documentation
architecture documentation

```

---

# M11 — Storage Recovery & Identity Backup — ACTIVE

M11 takes the zero-free-space condition from a tolerated runtime degradation into an explicit recovery workflow.

Primary objectives:

- off-device backup of the persistent Reticulum identity and application state;
- per-file integrity verification;
- FAT/logical-allocation diagnostics;
- explicit token-gated format of only the `reticulum` partition;
- verified restore and identity preservation;
- migration metadata/recovery tooling that can be reused by future storage-format changes.

M11 must never auto-format persistent storage. A destructive operation is allowed only after a verified off-device backup and explicit user action.

---

# 6. Current Project State

As of the handoff represented by this document:

### Proven hardware/runtime foundation

```text
T-Watch S3 hardware            WORKING
Display/touch                   WORKING
PMU                             WORKING
BMA423                          WORKING
RTC                             WORKING
Haptics                         WORKING
16 MiB flash                    CONFIRMED
8 MiB PSRAM                     CONFIRMED
Persistent FAT storage          WORKING
Persistent Reticulum identity   WORKING
microReticulum core             WORKING
SX1262 RX                       WORKING
SX1262 TX                       WORKING
RNode framing                   WORKING for normal frames
OTA Reticulum RX                CONFIRMED
OTA Reticulum announce TX       CONFIRMED
External rnpath discovery       CONFIRMED

```

### Current persistent identity baseline

The watch's previous identity changed because a different firmware was intentionally flashed between tests.

The current active identity used during M0.5/M1.0 is:

```text
Identity:
5f688e6e977054934398fac31e721c26

```

The earlier `reticulum_watch.node` destination was:

```text
6f13f6ea1745e2cde0466e48cb250e63

```

M1.0 created LXMF delivery destination:

```text
4d9615ebfb00b614fa6102448e629b0d

```

These values should not be hard-coded into firmware logic.

---

# 7. Exact Current Blocker

Current experimental firmware:

```text
ReticulumWatch-M1.0

```

The Pi sends an opportunistic LXMF message to:

```text
4d9615ebfb00b614fa6102448e629b0d

```

The watch successfully receives RF packets:

```text
[radio:rx] PHY ...
[radio:rx] complete Reticulum payload ...

```

and microReticulum reports:

```text
[rns:rx] accepted packet ...

```

However, there is no:

```text
[lxmf:rx]

```

callback afterward.

Therefore the immediate engineering problem is:

> **Why does an accepted Reticulum packet addressed through the LXMF path not reach the watch's** **`lxmf.delivery`** **packet callback?**

Investigation should begin in:

```text
ReticulumService
LxmfService
Destination construction
Destination registration
packet callback registration
microReticulum Destination/Transport packet-routing behavior

```

Do **not** begin by rewriting:

```text
SX1262 driver
DIO1 handling
RNode framing
radio profile

```

because the log already establishes that those layers are functioning.

---

# 8. Immediate Next Work

The next chat should resume at **M1.1**, not invent another roadmap.

The order should be:

```text
1. Diagnose lxmf.delivery callback routing.

2. Receive one short opportunistic LXMF message.

3. Parse and display:
   "Hello from the othersiiiiiiide"

4. Persist it.

5. Reboot and verify it remains in inbox.

6. Exercise a long LXMF payload to validate split-frame RX.

7. Implement outbound LXMF from the watch.

8. Validate watch → Pi/Sideband message delivery.

9. Harden signature validation.

10. Only then expand into full messenger UI.

```

---

# 9. Regression Tests That Must Survive Every Major Milestone

Every networking milestone should preserve:

```text
Boot without crash
Persistent identity survives reboot
Display/touch still function
Haptics still function
SX1262 initializes
Radio returns to RX after TX
Receive external Reticulum packet
Send Reticulum announce
External node learns watch path
No unexpected identity regeneration
Filesystem remains mountable
No brownout
No reboot loop

```

Once LXMF works, add:

```text
Receive LXMF
Persist LXMF
Restore inbox
Send LXMF
Receive delivery/proof state
No duplicate persistence

```

Once sleep is introduced, repeat networking tests after wake.

---

# 10. Known Mistake Ledger

Future iterations should explicitly check this before changing dependencies or architecture.

### PlatformIO dependencies

Do not pin Git dependencies with abbreviated commit SHAs.

Use:

- release tags; or
- full 40-character immutable commit hashes.

### LilyGo/ESP platform

Known-good platform:

```ini
platform =
https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip

```

This provides the newer Arduino/ESP-IDF environment required by LilyGoLib.

### microStore

`Reticulum::start()` requires a registered filesystem even when internal persistence is disabled.

The no-op microStore backend may return `false` from `init()` and still needs to be registered.

### RNS Identity

Do not do:

```cpp
RNS::Identity identity(RNS::Type::NONE);
identity.load_private_key(...);

```

`Type::NONE` creates a null handle and `load_private_key()` asserts.

Create a real Identity object before loading the key.

### Namespaces

Inside:

```cpp
namespace rw::radio

```

do not refer to LilyGo's global SX1262 object as bare:

```cpp
radio

```

Use:

```cpp
::radio

```

or, preferably in a future cleanup, inject/store an explicit radio reference.

### microReticulum 0.5.0 interface

The pinned version expects:

```cpp
bool send_outgoing(const RNS::Bytes&)

```

not `void`.

### Brownout

Do not disable the ESP32 brownout detector to hide power problems.

One earlier haptic test caused a brownout, but subsequent lighter haptic operation has worked.

### Transport probe constant

Do not forget:

```cpp
uint8_t NEIGHBOR_PROBE_RATELIMIT = 300

```

is broken because 300 overflows an 8-bit value.

Fix before Transport/probing work.

### Versioning

Do not reuse a failed version number.

Each supplied artifact becomes an immutable historical baseline.

---

# 11. Dependency Baseline

Known working hardware dependencies have included:

```ini
xinyuan-lilygo/LilyGoLib @ 0.2.0
lvgl/lvgl @ 9.4.0
jgromes/RadioLib @ 7.4.0
lewisxhe/XPowersLib @ 0.3.1
lewisxhe/SensorLib @ 0.3.3
mikalhart/TinyGPSPlus @ 1.1.0

```

microReticulum baseline:

```text
attermann/microReticulum
0.5.0

```

Do not casually update these while diagnosing an unrelated application-layer problem.

Dependency upgrades should be their own controlled change with regression testing.

---

# 12. Product End State

The project is not complete merely when two devices can exchange a packet.

The intended end product is a watch where a user can:

```text
raise wrist

see time + Reticulum state

receive vibration

read:
"Malcolm: Meet at the station?"

tap Reply

choose:
"On my way"

or type a full response

send

return to watch face

```

while the device independently maintains:

```text
Reticulum identity
LXMF identity
contacts
message history
peer knowledge
radio connectivity
Wi-Fi/BLE/USB connectivity
power state

```

without requiring a phone.

Later, the same hardware may optionally act as:

```text
RNode
Reticulum interface
Transport Node
contact/identity exchange device
voice communicator

```

but those are expansions of the core communicator—not substitutes for completing it.

---

# 13. Rule for Future Chats

This document is the project roadmap.

Do not redefine the overall milestone structure merely because a particular implementation takes several patch releases.

A debugging sequence such as:

```text
M1.1.0
M1.1.1
M1.1.2
M1.1.3

```

should still all mean:

> **M1.1 — make LXMF receive work correctly.**

A bug fix is not a new product milestone.

Before beginning each iteration, answer:

```text
What major milestone are we in?
What acceptance criterion are we trying to satisfy?
What is already proven?
What is the smallest change needed?
What known-good behavior must remain untouched?

```

That discipline should prevent the project from losing the overall roadmap again.

### M5 implementation status

- **M5.1 Wi-Fi / TCP Reticulum:** implemented in M5 as opt-in STA/TCP-client and AP/TCP-server roles using standard Reticulum HDLC TCP framing; compile/hardware acceptance pending.
- **M5.2 USB RNode Mode:** not falsely advertised as complete; requires full host-compatible RNode control/framing behaviour.
- **M5.3 BLE Interface / RNode Mode:** deferred until true host interoperability and mode isolation are implemented.
- **M5.4 Interface Selection / Multipath:** first step implemented: LoRa remains primary/default while Wi-Fi/TCP can be dynamically registered as `MODE_FULL`; deeper policy/metrics remain later work.

## M11-build2 follow-up

The first hardware recovery run proved the M11 backup/format safety gate but exposed missing restore flow control. M11-build2 adds per-chunk ACK pacing and idempotent retry from the verified host backup. It also folds the requested messaging preferences into the M11 settings surface: two user-owned canned reply slots and an explicit confirmed chat-history wipe that preserves identity/settings/peer state.


## M12 — Final UX & Power

M12 is the final feature milestone. The sleeping display no longer wakes from glass taps. The AXP2101 crown short press becomes the deliberate hardware display toggle, incoming-message wake remains, and an opt-in persistent Lift to Wake setting uses low-rate BMA423 observation without changing the accepted Reticulum/radio runtime. User brightness is retained across wake.

Acceptance requires hardware evidence for crown toggle, no tap wake, useful lift wake, persistent setting/brightness, and ordinary Reticulum/LXMF operation during repeated screen-off intervals.

## M13 — Release Candidate — CURRENT

M13 is the last milestone before stable release. Feature freeze applies. The work is soak/stress testing, remaining UI corrections, battery/resource profiling, storage/recovery sanity, documentation, packaging and bug fixes discovered by those tests. If the acceptance matrix passes, the next artifact is the stable release, not M14.


### M12-build2 hardware correction

M12 hardware testing showed the wake-input design was not observable/reliable: neither crown nor lift woke the display. M12-build2 keeps the M12 product behavior but replaces PMU pin-level polling with an ISR-latched AXP2101 event path plus status fallback, replaces manual BMA423 decoding with the exact pinned SensorLib read API, and adds continuous asleep-state motion telemetry for hardware tuning. Crown and lift wake are now hardware accepted in M12-build4; M13 is unblocked.


### M12-build3 sleep-state correction

Do not model lift-to-wake as an application polling loop. Screen-off Reticulum connected standby is explicitly CONNECTED-IDLE (CPU/radio alive), but wake inputs must be hardware-event driven so the design remains valid if a future mode enters true ESP light sleep. Use the T-Watch S3 BMA423 `SENSOR_INT` GPIO14 feature interrupt and consume TILT/WAKEUP status in normal task context.

## v1.0.0 — Stable release

M13 has graduated to the first stable ReticulumWatch release. v1.0.0 is intentionally behavior-preserving: it changes the release identity/package and documentation, not the accepted Reticulum/LXMF/radio, storage, audio, recovery or wake paths.

Post-1.0 feature development must begin on a new development milestone rather than being silently added to stable maintenance builds.

## Post-stable development — v1.1.0

The original milestone program is complete through M13 and stable v1.0.0. Further development uses semantic versions. v1.1.0 begins with a bounded home-face refinement: a modestly larger time display and a 4 px optical left correction, with the frozen communicator and accepted wake path unchanged.
