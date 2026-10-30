# Reticulum Watch mistake ledger

This file records project-process mistakes that should be checked before each new
firmware release. It belongs in the source tree so the lessons survive chat or
context resets.

1. **Do not reuse an issued version number.** Previous ZIPs are immutable
   baselines. A correction gets a new patch version.
2. **Do not use abbreviated Git SHAs in PlatformIO Git dependencies.** Use a full
   40-character immutable commit ID or a stable version/tag.
3. **Audit transitive dependencies whenever changing a fork/library source.** A
   Git checkout may not cause PlatformIO to install libraries referenced by a
   different upstream build configuration. Declare required libraries explicitly.
4. **For RatSpeak microReticulum 290a133, explicitly provide ArduinoJson, MsgPack,
   and Crypto.** Crypto supplies `Cipher.h`, `Ed25519.h`, and `RNG.h`.
5. **Do not call `pio run -t upload` failures flash failures automatically.** The
   command compiles first. Locate the first fatal error and distinguish build,
   link, port/reset, and esptool failures.
6. **Do not claim build verification unless an actual complete build succeeded.**
   Source/static audits must be labelled as such.
7. **When borrowing a proven implementation, compare the whole contract, not one
   code fragment.** Check dependencies, compile flags, interface lifecycle,
   endpoint/proof semantics, persistence, and protocol metadata together.
8. **Match code to the exact pinned library revision, not current upstream docs.**
   RatSpeak microReticulum `290a133...` identifies as 0.2.10; the umbrella
   `<microReticulum.h>` header was added later upstream (0.4.0). M1.1.3 uses a
   local compatibility header with the 0.2.x individual public headers.
9. **Do not assume application helpers are part of the protocol library.**
   `LXMFMessage.h`/`LXMFMessage::unpackFull()` was an unverified rsCardputer-side
   assumption. M1.1.3 removes it and uses the project's bounded MsgPack LXMF
   wire parser instead.
10. **Audit virtual signatures before subclassing a pinned fork.** The older
    InterfaceImpl contract uses `bool send_outgoing(const Bytes&)`; do not copy
    newer/lateral-fork lifecycle overrides without verifying them.
7. **Do not carry compatibility machinery across a library-fork rebase without proving the dependency still exists.**
   The M0.5 microStore generator belonged to upstream microReticulum 0.5.0. The
   RatSpeak 0.2.10 fork resolved for M1.1.x did not provide those headers, so the
   generator itself became a build blocker. M1.1.5 removes it.

8. **Compiler output from the exact pinned dependency outranks inferred API history.**
   M1.1.3 guessed `InterfaceImpl::send_outgoing()` returned `bool`; the pinned
   `Interface.h` compiled by PlatformIO explicitly declares it `void`. M1.1.5 uses
   that exact signature.


## M1.1.4 → M1.1.5: removing required filesystem registration

**Mistake:** During the RatSpeak rebase, the old microStore compatibility layer
was removed after a build-time mismatch, but the pinned microReticulum lineage
still requires *some* filesystem to be registered before `Reticulum::start()`.
The firmware therefore built/flashed but failed at runtime with `FileSystem has
not been registered`.

**Rule:** Distinguish a stale compatibility *generator* from a required runtime
contract. Before removing an integration layer, verify whether its underlying
behavior is still required. For this ReticulumWatch branch, preserve the
compile/boot-proven `microStore 0.1.7 + NoopFileSystem + OS::register_filesystem`
contract unless intentionally replacing it with a tested real filesystem.

## M1.1.5 → M1.1.6: confusing two unrelated FileSystem abstractions

**Mistake:** M1.1.5 saw that the fork required a filesystem registration and
restored the historical `microStore::FileSystem` object without checking the
exact parameter type of `RNS::Utilities::OS::register_filesystem()` in pinned
commit `290a133...`. That commit expects `RNS::FileSystem&`, so the build failed
before runtime.

**Rule:** When an API uses a common abstraction name such as `FileSystem`, verify
the namespace and exact declared type in the pinned dependency. Similar names
across old/new integration layers are not interchangeable. For `290a133...`,
register an `RNS::FileSystem`, not a `microStore::FileSystem`.

### M1.2.1 — do not infer delivery from a successful Link send
- **Observed:** the pinned RatSpeak `PacketReceipt::validate_link_proof()` path
  still contains a hard-coded validation stub.
- **Rule:** a successful `Packet(link,...).send()` means locally transmitted,
  not remotely delivered. Do not display a delivered checkmark until proof
  validation is actually functional and tested.
## M1.1.6 → M1.2.1: treating `Bytes::mid()` like an end-index slice

**Mistake:** the M1.1.6 LXMF parser used `mid(16, 32)` and `mid(32, 96)` as if
the second argument were an exclusive end index. In the pinned RatSpeak fork,
`Bytes::mid(begin, len)` uses a byte length. That produced a 32-byte source hash
and a 96-byte signature slice. The payload still began at byte 96, so message
text appeared correct and hid the bug, while sender lookup and message-ID
calculation were wrong.

**Rule:** before translating Python-style byte slicing to the fork's `Bytes` API,
verify whether the second argument is a length. For LXMF full wire, use explicit
16-byte destination, 16-byte source, 64-byte signature, then payload at offset 96.


## M1.2.0 -> M1.2.1: LVGL setter arity compile failure

- Failure: `lv_obj_set_width(224);` omitted the target `lv_obj_t *` argument.
- Rule: before packaging UI iterations, scan all `lv_obj_set_*` calls for the exact LVGL signature and object argument.
- Fix in M1.2.1: `lv_obj_set_width(footer, 224);`.

## M1.2.1 -> M1.2.2: treating `lv_obj_set_pos()` as absolute after a widget constructor set alignment

- Symptom: the LVGL keyboard was mostly below the 240x240 display even though `y + height` appeared to fit numerically.
- Root cause: `lv_keyboard_create()` installs bottom-middle alignment. The later `lv_obj_set_pos(keyboard, 0, 91)` changed the aligned object's offsets, effectively pushing a bottom-aligned keyboard downward.
- Fix: size the keyboard, then explicitly `lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0)`; do not mix an inherited non-top-left alignment with apparently absolute `set_pos()` coordinates.
- Rule: before using `lv_obj_set_pos()` on specialized LVGL widgets, audit constructor/default alignment. Prefer explicit alignment or explicitly reset alignment first.

## M1.2.2 -> M1.2.3: treating a derived count file as authoritative history

- **Risk found:** M1.2.2 read `/lxmf/message_count.bin` as the authoritative next sequence. If that small derived file became missing, truncated, stale or corrupt while history records still existed, resetting the counter could eventually reuse an existing `/lxmf/inbox/NNNNNNNN.lxm` filename.
- **Rule:** durable message records are the source of truth; compact indexes/counters are rebuildable derived metadata. Recover derived indexes from the journal before allocating a new sequence.
- **Related rule:** duplicate suppression must survive reboot. An in-RAM "last message ID" cache is only a fast path, never the durable duplicate database.

## M1.2.3 -> M1.2.4: depending on an input path that the firmware does not own

- **Observed:** the M1.2.3 acceptance instructions required a reboot, but the user could not obtain one from the crown/button. Source inspection showed no explicit crown/button input handler in the current ReticulumWatch firmware.
- **Rule:** do not make an acceptance test depend on a physical input behavior unless that input path is explicitly implemented and hardware-validated in this firmware. Provide a deterministic software control when the test itself only needs a reboot.
- **Architecture rule:** UI code may request a system operation, but the hardware/system primitive belongs in the owning HAL/service (`WatchHardware::restart()`), not directly in the screen callback.

## M1.2.4 -> M1.2.5: persistence integrity must cover the authoritative record and its derived indexes

- **Risk found:** a history file written directly to its final path can be left truncated by a reset/power loss, and a message-ID marker should not be treated as valid merely because its filename exists.
- **Rule:** commit authoritative records through a temporary file and rename only after a complete write; add an integrity check to the record format; validate compact derived metadata before trusting it.
- **Retention rule:** durable storage must have an explicit bound. When pruning an authoritative record, prune its derived message-ID marker in the same maintenance policy.
- **Trust rule:** persist `SUBMITTED` after local `Packet::send()` success on the pinned fork; reserve `DELIVERED` for a genuinely validated remote receipt.


## M1.2.6 -> M1.2.7: copying C enum-initializer syntax into C++ LVGL application code

- Symptom: `WatchUI.cpp` failed with repeated `invalid conversion from 'int' to 'lv_buttonmatrix_ctrl_t'` errors.
- Root cause: bitwise expressions such as `LV_BUTTONMATRIX_CTRL_POPOVER | 1` produce an integer. LVGL's own keyboard tables use this pattern in `lv_keyboard.c`, but ReticulumWatch's `WatchUI.cpp` is C++, which does not implicitly convert that integer expression back to the enum type.
- Fix: convert all custom control-map flag/width expressions through constexpr helpers returning `lv_buttonmatrix_ctrl_t`, including width-only entries.
- Rule: when adapting initializer patterns from a C library's `.c` implementation into C++, verify enum/bitflag conversion rules under the actual C++ compiler. A source-level API match is not enough.

## M1.2.7: macOS PlatformIO auto-selected the Bluetooth pseudo-port

- **Observed:** `pio run --target upload` completed compilation and size checks, then auto-selected `/dev/cu.Bluetooth-Incoming-Port`; esptool consequently reported `No serial data received`.
- **Rule:** distinguish a successful firmware build from a later upload-port failure. On macOS, verify the T-Watch appears as `/dev/cu.usbmodemXXXX` with `pio device list` before flashing, and pass `--upload-port` explicitly when auto-detection chooses the Bluetooth pseudo-port.
- **Do not hard-code a remembered usbmodem number as universally stable.** The suffix can change after reconnect/reboot; use the currently enumerated port.

## M1.3.1 -> M1.3.2: do not bind UI haptics to raw touchscreen activity

- **Requirement:** haptic feedback should confirm actual controls/keys, not every touch on the display.
- **Rule:** attach UI haptics to semantic LVGL control events (`LV_EVENT_CLICKED` for buttons and `LV_EVENT_VALUE_CHANGED` for the keyboard/button-matrix), never to the screen/root pointer event. This avoids buzzing while scrolling, tapping empty space, or merely positioning a finger on the screen.
- **Architecture rule:** `WatchUI` may request a tap effect, but DRV2605 control remains in `WatchHardware`.
- **LVGL 9.4 compatibility note:** if an assigned keyboard textarea has no visible caret, do not assume the theme is at fault. The exact LVGL 9.4.0 keyboard implementation must be audited; ReticulumWatch explicitly focuses the textarea and styles `LV_PART_CURSOR` rather than depending on implicit keyboard focus management.


## M1.3.2 -> M1.3.3: a theoretically valid haptic event is not enough if the physical feedback is not perceptible

- **Observed:** DRV2605 initialization succeeded and M1.3.2 otherwise worked, but the user could not feel the intended button haptic feedback on the physical T-Watch S3.
- **Cause/risk:** the UI used DRV2605 effect 6 (Sharp Click, 30%), the weakest sharp-click waveform, and delayed normal-button feedback until `LV_EVENT_CLICKED`. Either can make confirmation feel absent on a small watch enclosure.
- **Rule:** wearable haptics must be hardware-validated for perceptibility, not merely API-valid. Use a short but clearly perceptible effect and trigger tactile acknowledgment on semantic control press-down; keep the action itself on click/release.
- **Scope rule:** never attach haptics to raw touchscreen activity. Disabled controls, background taps, and scrolling remain silent.

## M1.3.3 -> M1.4.0: cryptographic trust must be re-established, not inherited

- **Known defect:** the historical RatSpeak microReticulum lineage used through M1.3 contains an `Identity::validate()` path that could report success without enforcing the Ed25519 verification result.
- **Rule:** after replacing a security-critical dependency, do not trust historical `VERIFIED` metadata merely because it was persisted by an earlier build. Re-establish trust under the corrected runtime.
- **Rule:** a security dependency upgrade needs both a positive and a negative test against the exact linked implementation. M1.4.0 therefore validates a correct signature and requires one-bit-tampered signature/message variants to be rejected at boot.
- **Fail-closed rule:** if that self-test fails, do not expose the LXMF endpoint. A stale dependency must be noisy and unusable rather than silently insecure.
- **Migration rule:** use the new dependency's documented public include/API boundary and adapt the compatibility layer once. Do not scatter version-specific API workarounds through application code.
- **Scope rule:** do not combine the M1.4 security rebase with microReticulum internal-persistence/provisioning rollout. Those architectural changes belong to later milestones.

## M1.3.3 haptic result — defer unresolved tactile-strength tuning

- **Observed:** the DRV2605 initializes and both 30% and 100% sharp-click waveform attempts were still little-to-no perceptible feedback on the physical watch.
- **Decision:** do not hold the protocol roadmap hostage to repeated waveform-ID guesses. Carry the haptic-strength issue forward into later M2/M3 UX/hardware work, where actuator mode, drive strength, calibration and waveform sequencing can be measured deliberately.
- **Rule:** when a non-blocking UX defect resists two narrow fixes and the core acceptance criteria are otherwise met, document/defer it rather than destabilizing unrelated milestone work.



## M1.4.0 -> M1.4.1: do not code against dependency `master` when the build uses a released transitive package

- **Observed:** the M1.4.0 build resolved `microReticulum @ 0.5.0` and `microStore @ 0.1.7`, then reported `microStore::Adapters has not been declared` when ReticulumWatch referenced `microStore::Adapters::NoopFileSystem()`.
- **Correction established in M1.4.4:** the original conclusion that the released package lacked the `Adapters` namespace was wrong. The exact 0.1.7 header contains `microStore::Adapters::NoopFileSystem`, but gates the entire adapter definition behind `USTORE_USE_NOOPFS`. That macro was missing from our build.
- **Rule:** for dependency migrations, inspect the exact resolved release *including its preprocessor guards*. A namespace-not-declared diagnostic can mean a feature-gated header body compiled to nothing; it does not by itself prove the API is absent.
- **Historical M1.4.1 decision (superseded by hardware evidence):** M1.4.1 removed the filesystem adapter because `RNS_USE_FS` was disabled. The subsequent runtime test proved that decision wrong: microReticulum 0.5.0 still requires a registered filesystem abstraction for startup. See the M1.4.1 -> M1.4.2 entry below.

## M1.4.1 -> M1.4.2: disabled persistence does not remove a runtime lifecycle dependency

- **Observed:** M1.4.1 compiled, but `Reticulum::start()` threw `FileSystem has not been registered`. The radio ISR/PHY path continued receiving frames, while the Reticulum loop never became ready to drain them; this caused the completed-packet slot to remain occupied and subsequent frames to be dropped.
- **Root cause:** M1.4.1 removed the microStore filesystem object because `RNS_USE_FS` and provisioning were disabled. That confused *persistence policy* with a *required runtime abstraction*.
- **Rule:** for a dependency migration, disabling a feature does not imply that all of its lifecycle objects disappear. Verify required startup dependencies from the exact runtime behavior/API before removing them.
- **Exact 0.5.0 contract used here:** create a valid `microStore::FileSystem` abstraction, register it through `RNS::Utilities::OS::register_filesystem(...)`, then call `Reticulum::start()`. With persistence disabled, use a no-op adapter rather than omitting the filesystem object.
- **Boot-status rule:** critical subsystem failure must propagate to top-level boot state. Never print an unconditional `initialization complete` after Reticulum/LXMF startup failed.
- **Release-process rule:** every issued build must run `scripts/release_preflight.py`, partition validation, a complete diff audit, frozen-baseline checks, and archive integrity checks before packaging. Actual PlatformIO verification must still be reported separately when the assistant environment cannot perform it.

## M1.4.2 -> M1.4.3: static preflight cannot verify a guessed external API
- **Failure:** M1.4.2 guessed a root-level `microStore::NoopFileSystem` type. The target compiler correctly reported that no such root-level type exists.
- **Why the preflight missed it:** the checker only verified that our guessed symbol spelling appeared in source text. It therefore validated our assumption, not the dependency API.
- **M1.4.3 follow-on mistake:** replacing that guessed type with a guessed root-level `microStore::NoopFileSystem()` factory repeated the same error in a different syntactic form.
- **Permanent rule:** never report an external library API as verified from textual/source-pattern checks. External API compatibility requires a compile against the exact resolved dependency/toolchain or direct inspection of that exact version's headers, including feature guards.

## M1.4.3 -> M1.4.4: mistaking a preprocessor-disabled adapter for a missing API

- **Failure:** M1.4.3 called a nonexistent root-level `microStore::NoopFileSystem()` and failed to compile.
- **Actual cause behind the earlier `microStore::Adapters has not been declared` error:** microStore 0.1.7 wraps `Adapters::NoopFileSystem` in `#if defined(USTORE_USE_NOOPFS)`. We included the header without defining that feature macro, so the namespace/type was preprocessor-disabled.
- **Second latent bug:** microStore 0.1.7's no-op adapter intentionally returns `false` from `init()`. Treating that result as a fatal initialization failure would have broken startup even after the compile fix.
- **Permanent rule:** before concluding that a dependency namespace/type is absent, inspect the exact pinned header for feature macros and conditional compilation. Do not infer API shape from a compiler suggestion alone.
- **Permanent rule:** distinguish object validity from operation return semantics. For a deliberate no-op adapter, verify its contract before using `init()` as a success gate.
- **Release rule:** static preflight now requires `USTORE_USE_NOOPFS`, the exact `microStore::Adapters::NoopFileSystem` type, no root-level factory guess, no `rnsFilesystem.init()` gate, and filesystem registration before `Reticulum::start()`.

## M1.4.4 -> M2.6.0: local transmit success is not announce interoperability

- **Observed:** the watch emitted two `lxmf.delivery` announces; Reticulum handed 187-byte packets to the SX1262 and RadioLib reported both 188-byte PHY transmissions as successful, but the peer did not surface the watch announce.
- **Mistake in diagnostics:** the application printed `interoperable ... announce sent` after the local `Destination::announce()` call. That wording converted a local submission result into an unproven interoperability claim.
- **Permanent rule:** distinguish **constructed**, **submitted**, **radio-transmitted**, **peer-received**, **cryptographically accepted**, and **application-displayed** states. Never collapse them into a generic "sent" or "working" claim.
- **Announce investigation rule:** compare the complete signed announce envelope against the reference implementation before modifying LXMF application data. The app-data shape can be valid while the Reticulum announce freshness/signature envelope is rejected.
- **Scope rule:** a non-blocking discovery/announce regression should not destabilize a hardware-proven direct messaging path. Track it explicitly and fix it before the M2.4 discovered-peer milestone, while continuing unrelated M2 inbox work.

## M2.6.0: read/unread UI state must not rewrite authoritative message history

- **Requirement:** read/unread state must survive reboot without changing the existing LMX3 CRC/history format.
- **Rule:** persist mutable presentation state as rebuildable/derived metadata when the underlying message body is immutable. M2.1 uses per-sequence `.unr` markers under `/lxmf/metadata/unread` rather than rewriting CRC-protected `.lxm` records.
- **Migration rule:** absence of an unread marker on pre-M2.1 history means `read`, so an upgrade never marks all historical messages as new.

## M2.6.0 -> M2.6.1: static UI audit missed malformed LVGL setter arity

M2.6.0 failed its first real PlatformIO compile in `WatchUI::showConversations()` because
`lv_obj_set_width(224)` omitted the required `lv_obj_t *` first argument. Both compiler errors
were the same source typo. M2.6.1 corrects this to `lv_obj_set_width(footer, 224)`.

Permanent rule: release preflight must parse calls to common LVGL object setters and reject
incorrect top-level argument counts before packaging. A lexical delimiter check is not enough
to catch well-formed C++ syntax that calls an API with the wrong arity.


## M2.6.0 -> M2.6.1: static syntax checks must validate fixed-arity UI APIs

- **Observed:** M2.6.0 reached the real compiler with `lv_obj_set_width(224)`, omitting LVGL's required object argument.
- **Rule:** release preflight must validate argument counts for common fixed-arity LVGL calls, not merely balanced delimiters and symbol presence.
- **Artifact rule:** preserve the failed package; correct it under a new immutable build identifier rather than overwriting it.

## M2.6.1 -> M3 build 1: verify the pinned release before patching dependency source

- **Observed:** M2.6.1 emitted complete 187-byte announce packets and the SX1262 reported successful PHY transmission, yet the Python Reticulum peer did not surface the announce.
- **Mistake:** the first M3 package assumed microReticulum 0.5.0 still used an older all-random announce freshness blob and installed a fail-closed source patch. The actual released 0.5.0 source had already changed to the timestamp-form encoding, so the build correctly aborted before compilation.
- **Root compatibility concern that remains:** the built-in announce timestamp encoder still needs a sane wall clock. An ESP that starts near the Unix epoch can repeatedly emit timestamps that peers consider stale after a previous path has been learned.
- **Rule:** before patching third-party source, inspect the exact pinned release actually resolved by the package manager, including release date/source shape. A historical bug report or older source citation is not enough.
- **Fix policy:** do not rewrite microReticulum 0.5.0. Seed ESP wall-clock time before Reticulum starts, prefer the battery-backed RTC, keep a bounded compile-time fallback, and log the announce epoch immediately before transmission.

### M3 UI stack usage
- Do not allocate `StorageService::kMaxPeers` worth of `PeerRecord` objects on Arduino `loopTask` stack from a UI event. Use bounded scans or heap/storage-owned buffers.

### RTC fallback display
- If the hardware RTC is invalid but system time has already been seeded, UI time formatting must fall back to the system clock rather than rendering `--:--`.


## M4 wearable input / standby

- Wake touches must not activate hidden controls. When waking a sleeping display from touch, suppress LVGL input until the initiating touch is released.
- Do not equate display standby with network sleep. Preserve the proven Reticulum receive loop by default; duty-cycled/deep sleep requires separate measured acceptance.
- Use mature Reticulum handhelds (Ratspeak/rsDeck/rsCardputer) as product/IA references, but adapt layouts to the T-Watch 240x240 touch form factor instead of copying keyboard-device UI literally.


## M4 -> M5: validate the test peer before declaring a firmware radio regression

- An apparent M4 send/receive/announce failure was ultimately caused by a dead battery in the remote test radio.
- **Rule:** when a previously working local radio still logs valid TX/RX PHY activity, validate remote peer power/configuration before changing the proven networking stack.

## Protocol clocks must be Unix clocks

- `__DATE__` / `__TIME__` do not carry a timezone and must not be blindly interpreted as UTC. M5 injects a timezone-independent UTC build epoch at build time.
- **Rule:** never use `millis()`/seconds-since-boot as an LXMF or Reticulum wall-clock timestamp. If the RTC is invalid, use a seeded sane Unix system clock or fail closed.


## M5 -> M6: routing features must preserve endpoint safety and dependency patches must remain surgical

- **Baseline rule:** M5 hardware logs are the accepted LoRa/LXMF baseline. M6 routing work must not change `RadioService`, `WatchLoRaInterface`, `LxmfService`, M5 TCP interface code, power manager, radio defaults or partitions without concrete evidence.
- **Transport rule:** a battery-powered watch must never become a Transport Node by default. Transport is an explicit user choice, persisted separately and applied only after reboot, with an airtime/battery warning.
- **Persistence rule:** microReticulum routing/path persistence gets its own subtree (`/rns/internal`) and must not take ownership of the application's permanent identity or immutable LXMF journal.
- **Dependency-patch rule:** a third-party source patch is acceptable only when grounded in an exact observed pinned-source defect, narrowly matched, idempotent and fail-closed. The M6 neighbor-probe patch targets the compiler-proven `uint8_t NEIGHBOR_PROBE_RATELIMIT = 300` overflow and nothing else.
- **Font/API rule:** verify that a requested LVGL built-in font size actually exists before enabling it. LVGL 9.4's built-in Montserrat set ends at 48 px; M6 enlarges the clock via supported transform scaling instead of inventing `lv_font_montserrat_56`.


## M6 build1 -> build2: optional persistence must never gate Reticulum startup

- Do not use process-wide `chdir()` as a prerequisite for the ESP32 Reticulum stack.
- A persistence enhancement is subordinate to core packet RX/TX availability. If persistence cannot initialise, fall back to the proven non-persistent filesystem lifecycle and keep Reticulum online.
- Release preflight must reject any M6 endpoint path where a persistence failure can return before LoRa interface registration / `Reticulum::start()`.
- A raw radio RX log is not proof the protocol stack is receiving: verify `[rns:rx] injecting` / `Transport accepted` as separate stages.

## M6 build2 -> build3: reference implementation gate before protocol changes

- Do not modify microReticulum dependency source or Reticulum interface/transport behavior from a hardware-accepted endpoint baseline without first checking the official Reticulum manual, the pinned microReticulum API/examples, and at least one working embedded implementation.
- Transport/routing features must be introduced behind a separately testable boundary. Endpoint messaging/announces must remain source-identical to the last hardware-accepted baseline until transport itself is hardware-accepted.
- A packet reaching `Transport accepted` is not sufficient acceptance; verify application-layer announce discovery, incoming LXMF decode, outgoing peer receipt/proof, and Link establishment.

## M7 -> M8: configuration must not become a new protocol regression vector

- **Accepted baseline:** M6-build3/M7 hardware tests restored reliable announces, Link RX/TX and LXMF messaging.
- **Rule:** settings work must be layered above the accepted endpoint boundary. Do not alter `ReticulumService`, `WatchLoRaInterface`, `LxmfService`, `RadioService`, RNS build configuration or partition maps merely to expose a UI preference.
- **Persistence rule:** user preferences live in an independent CRC-protected sidecar. A corrupt/missing settings file must fall back to defaults and must never gate Reticulum startup.
- **Reference rule:** before exposing Reticulum behavior as a user setting, check the official Reticulum manual, the exact pinned microReticulum source/API and at least one working firmware implementation.
- **Announce-rate rule:** do not expose aggressive periodic announce intervals. Manual is the default; startup and six-hour scheduling are sufficient for M8 and align with official guidance.
- **Scope rule:** if a desired setting would require changing a frozen protocol-critical file, either defer it or isolate it behind a separately testable milestone rather than silently modifying the accepted path.

### M8 -> M8-build2: UI helper visibility must be compile-audited
- `contactShareEvent()` referenced `fullHash()` without a visible declaration, causing the only compiler error in M8.
- Keep hex-format helpers in the same anonymous namespace as `shortHash()` or declare them before use.
- Release preflight must verify that contact-share helper references resolve before packaging.


## M8 announce propagation: local TX is not remote propagation

- A successful `Destination::announce()` call and successful SX1262 PHY transmission prove only local emission. They do not prove that a neighbouring Transport Node re-propagated or an application surfaced the announce.
- Before changing announce bytes, compare the protocol-critical source against the last hardware-accepted build. If it is identical, investigate network announce queues/rate control first.
- Reticulum deliberately down-prioritises rapidly re-announcing destinations. Never respond to an unseen announce by repeatedly hammering the ANNOUNCE button.
- Manual announce requests must suppress an immediately pending startup announce and reset the periodic scheduler.
- Preserve the accepted wire format unless packet-level evidence demonstrates a format defect.

## M9: audio work must not become a communicator-loop regression

- **Risk:** microphone reads and speaker writes can block while the I2S/DMA path waits for data or space.
- **Rule:** never perform capture/playback I/O from the Arduino loop that services radio, Reticulum and LXMF. M9 keeps I2S I/O on a dedicated low-priority worker task; `AudioService::loop()` is non-blocking.
- **Rule:** audio startup is optional. Microphone, amplifier, buffer or task failure must degrade only Audio Lab and must never gate Reticulum/LXMF startup.
- **Rule:** large audio buffers belong in PSRAM. Do not silently fall back to scarce internal heap if the bounded PSRAM allocation fails.
- **Rule:** recorded LXMF voice messages and live LXST/PTT are different transport problems. Hardware audio acceptance comes first; interoperable wire semantics come from LXMF/LXST references, not a private packet format invented for convenience.
- **Rule:** preserve the M8-build3 frozen endpoint hashes while developing audio above the boundary.
- **Hardware rule:** an audible local record/play test is not evidence that voice-over-Reticulum works. Network audio requires its own explicit interoperability acceptance later in M9.

## M9 -> M9-build2: budget the complete signed LXMF wire, not only the codec payload

- **Observed during release audit:** a first internal M9-build2 design allowed 384 bytes of Codec2 payload because the audio buffer itself was bounded, but Reticulum's encrypted single-packet MDU is only 383 bytes. LXMF's destination hash, source hash and signature already consume 96 bytes before the packed payload.
- **Rule:** attachment/field sizing must be calculated against the complete signed LXMF wire and the actual `Link::get_mdu()`, never against codec bytes alone.
- **Fix:** standard Codec2-450 voice is capped at 240 encoded bytes; the conversation recorder is runtime-bounded from codec frame geometry and capped at 3 seconds; the final signed wire is checked against the live Link MDU immediately before send.
- **Transport rule:** do not introduce private fragmentation to evade a standard Reticulum size boundary. If larger audio is required, deliberately validate the standard Resource path against the exact pinned microReticulum implementation first.
- **Interoperability rule:** keep LXMF's wire codec identifier (`AM_CODEC2_450 = 0x02`) conceptually separate from the Codec2 library's internal `CODEC2_MODE_450` enum/value.
- **Persistence rule:** standard LXMF fields may add sidecar metadata, but must not silently mutate the accepted LMX1/LMX2/LMX3 journal layouts.


## M9-build2 -> M9-build3: compile-audit exact pinned MsgPack predicates

- **Observed:** the first real M9-build2 PlatformIO compile failed in `LxmfService.cpp` because `MsgPack::Unpacker::isFloat()` does not exist in the resolved `hideakitai/MsgPack @ 0.4.2` API. The compiler suggested the actual width-specific predicate `isFloat32()`.
- **Cause:** static review verified the generic field-skipping algorithm but did not verify every newly used MsgPack method against the exact pinned library API.
- **Rule:** when adding protocol parser helpers, check every dependency-facing method against the pinned release actually used by PlatformIO; do not infer convenience predicates from a newer library version or from API symmetry.
- **Fix:** use `isFloat32()` / `isFloat64()` and the matching `unpackFloat<float>()` / `unpackFloat<double>()` calls.
- **Regression gate:** release preflight rejects `unpacker.isFloat()` and requires the two width-specific predicates.
- **Artifact rule:** M9-build2 remains immutable; the correction is M9-build3.

## M9-build3 -> M9-build4: never construct Codec2 on Arduino loopTask

- **Observed hardware failure:** M9-build3 compiled and flashed successfully, but the watch display remained blank because boot repeatedly panicked before UI initialisation.
- **Backtrace:** `make_analysis_window()` -> `codec2_create()` -> `VoiceService::begin()` -> `setup()` -> `loopTask()` with `Stack canary watchpoint triggered (loopTask)`.
- **Root cause:** `codec2_create(CODEC2_MODE_450)` has substantial temporary stack demand and was called synchronously from Arduino `setup()`, despite later encode/decode work having a dedicated worker.
- **Rule:** Codec2 construction, encode and decode all belong on the dedicated codec task. Do not call `codec2_create()` from `setup()`/`loopTask`.
- **Stack rule:** Keep the voice worker at a minimum 32 KiB stack unless measured hardware high-water data proves a smaller bound safe.
- **Reference pattern:** ESP32 Codec2 examples initialise the codec inside a dedicated audio/codec task with a large stack rather than on the Arduino loop task.
- **Storage observation:** The same hardware log showed FFat `free=0 bytes`. Under storage pressure, only rebuildable message-ID `.idx` accelerators may be purged automatically; identity, journals, contacts, mutable message state, settings and voice sidecars must be preserved.
- **Artifact rule:** M9-build3 remains immutable; the correction is M9-build4.


## M9-build4 -> M9-build5: a standard codec mode is not automatically client-playable

- **Observed:** M9-build4 successfully sent and persisted a standards-shaped LXMF `FIELD_AUDIO=[0x02, bytes]` Codec2-450 message, but Sideband did not appear able to play it.
- **Reference check:** Sideband's public playback path accepts Codec2 modes beginning at `AM_CODEC2_700C` and ending at `AM_CODEC2_3200`; mode `AM_CODEC2_450` falls outside that range.
- **Rule:** protocol-valid identifiers are not sufficient for interoperability. Before selecting an optional codec/profile, verify the receiving client's actual supported range.
- **Fix:** outbound watch audio uses `AM_CODEC2_700C=0x03`; mode `0x02` remains only for playback of already-persisted M9-build4 clips.
- **PTT rule:** do not label bounded LXMF audio bursts as LXST. Full LXST call/PTT signalling must be implemented only from exact current reference behaviour, because the public LXST project explicitly marks APIs unstable and under-documented.
- **Artifact rule:** M9-build4 remains immutable; the interoperability/PTT follow-up is M9-build5.

## M10: persistent storage recovery must never auto-format user identity/history

- **Observed risk:** earlier builds mounted the dedicated Reticulum FFat volume with `FFat.begin(true, ...)`, which allows an automatic format when mounting fails.
- **Why this is unacceptable:** the same volume contains the persistent Reticulum identity and immutable LXMF journal. A transient/corrupt mount failure must not silently become permanent identity/history loss.
- **Rule:** release-oriented builds mount the persistent volume with automatic formatting disabled. Formatting or destructive repair always requires an explicit user-directed recovery action.
- **Low-space rule:** preserve an emergency free-space reserve and fail closed on new growth writes before partial journal/audio writes consume the final filesystem space.
- **Supervisor rule:** a watchdog/supervisor may restart a wedged device, but it must not attempt FFat repair or mutate protocol state from its background task.

## M9 -> M10: successful delivery is not proof of receiver audio playback

- **Observed:** watch-originated standard LXMF audio reaches the Sideband peer, but audible playback is still not confirmed even after moving outbound audio from Codec2-450 to Codec2-700C.
- **Rule:** do not keep changing standards-shaped LXMF fields based only on silence at one receiver. Capture receiver-side diagnostics or cross-test another LXMF implementation before another wire change.
- **Project rule:** an unresolved optional-client audio interoperability issue does not justify destabilising the working text communicator or blocking stabilization work.

## M10 -> M10-build2: persistence failure must never become a communications outage

- **Observed hardware failure:** the radio and Reticulum layers continued accepting announces and incoming Link packets, but FFat reached zero free bytes. M10's preservation-first journal guard then returned failure from `saveLxmfMessage()`. Incoming messages were dropped after successful decode, Link sends were reported as failed after physical TX, and opportunistic sends were aborted before packet submission.
- **Root cause:** application durability was incorrectly coupled to transport availability. Protecting the flash journal is correct; treating persistence as a prerequisite for live communication is not.
- **Rule:** a mounted but unwritable persistence layer must degrade to an explicitly volatile, bounded RAM state. It may lose new history on reboot, but it must not disable otherwise healthy Reticulum/LXMF RX or TX.
- **Fix:** M10-build2 adds a 16-message RAM journal plus bounded RAM audio sidecars. Once activated, new sequences remain RAM-only for the rest of that boot to avoid mixed persistent/volatile sequence collisions.

## M10 -> M10-build2: announce discovery must not depend on peer metadata writes

- **Observed:** `[m2:peer] stored announce peer` appeared repeatedly while the Peers screen reported `count=0` on the same boot.
- **Root cause:** discovered peers existed only as derived FFat metadata. A full/damaged FAT volume could accept the announce callback but leave nothing enumerable by the UI.
- **Rule:** transient network discovery belongs in RAM first. Persistent peer/contact metadata is an optional durability layer, not the live source of truth.
- **Fix:** M10-build2 maintains a bounded 48-peer RAM cache, unions it with readable persisted peers, and refreshes an open Peers screen when announce count changes.

## M10 -> M10-build2: distinguish local RTC wall time from Unix UTC

- **Observed:** the watch face remained roughly eleven minutes behind real time even though firmware had been freshly built and Reticulum traffic was active.
- **Root cause:** the old clock path preferred any year-plausible PCF8563 value over a newer build epoch and manually interpreted local RTC fields as UTC seconds.
- **Rule:** PCF8563 calendar fields are local watch time; protocol timestamps are Unix UTC. Convert through the configured timezone, use a freshly compiled epoch as a lower-bound correction for stale RTC state, and accept network clock discipline only under a bounded trust/skew policy.
- **Fix:** M10-build2 converts RTC local time with `mktime()`, advances materially stale RTC state to the build epoch, and permits +/-30-minute discipline from incoming Link/verified LXMF timestamps while writing corrected local time back to the PCF8563.

## M10 -> M10-build2: message feedback must be asynchronous and distinct

- **Requirement:** message receipt and successful transmission need obvious watch-native feedback.
- **Rule:** notification sound and strong haptics must not block `loopTask`, radio, RNS, or LXMF. RX and TX should have distinguishable patterns.
- **Fix:** M10-build2 schedules multi-pulse DRV2605 patterns from `WatchHardware::loop()` and generates RX/TX alert tones on the existing dedicated audio worker.


## M10-build2 -> M10-build3: notification identity should be cadence-driven, not copied audio

- **Lesson:** A recognizable UI alert request does not require embedding or reproducing a sampled source sound.
- **Rule:** Prefer an original synthesized cadence that captures the requested interaction pattern, and keep it asynchronous on the audio worker.
- **Fix:** M10-build3 uses six synthesized 1400 Hz beeps in a 3 + pause + 3 cadence; no sampled asset is included.

## M11: never back up a failing filesystem onto itself

- **Observed condition:** the Reticulum FFat can report zero free space while traversable user data is only tens of KiB.
- **Rule:** recovery backups must leave the failing volume. Stream the authoritative data off-device and verify it before any format/rebuild action.
- **Rule:** formatting is never automatic. Require a same-session successfully completed backup plus an explicit confirmation token before formatting the Reticulum partition.
- **Rule:** skip rebuildable `.idx` accelerators and interrupted `.tmp` files in the authoritative recovery bundle.
- **Rule:** after formatting, normal application storage writes must be disabled until restore completes and the watch reboots.
- **Rule:** a restore is not successful until every file CRC passes and the persistent 64-byte Reticulum identity is present.

## M11 -> M11-build2: restore chunks must be acknowledged

- **Observed recovery failure:** the first real M11 `repair` captured and CRC-verified the complete backup, formatted the named `reticulum` partition, then timed out during restore after beginning the second file.
- **Root cause:** restore DATA chunks were sent as a host-side burst with no application-level flow control. USB CDC buffering is not a substitute for a protocol acknowledgement from the firmware loop that consumes the lines.
- **Rule:** restore chunks must be acknowledged before the host sends the next chunk. Destructive recovery protocols require explicit application-level pacing, not just serial `flush()` calls.
- **Retry rule:** interrupted restores must be idempotently restartable from the already-verified host backup. Re-running from file one with explicit `FORCE` is safe because each destination is recreated and CRC-verified.
- **Artifact rule:** M11 remains immutable; the recovery correction is M11-build2.


## M12: wake inputs must be explicit and hardware-correct

- **Do not assume a watch crown is a GPIO.** On T-Watch S3 it is the AXP2101 power key. Use the documented PMIC short-click IRQ and leave the long-press hardware shutdown path alone.
- **Sleeping touch is no longer a wake source by product choice.** A wake event must not also become a hidden LVGL tap. M12 removes touch wake rather than suppressing/replaying it.
- **Do not reconfigure a proven sensor stack late in the release without need.** Lift-to-wake uses read-only low-rate BMA423 acceleration sampling in connected standby instead of speculative feature-engine changes.
- **Motion wake is opt-in until hardware tuned.** Default OFF, persistent user setting, bounded polling, rotation + settle + cooldown guards.
- **Display wake must preserve user brightness.** Never restore a compiled brightness constant after sleep if a user-selected value exists.


## M12 -> M12-build2: wake inputs need observable event paths

- **Observed:** M12 built/flashed, Lift to Wake was saved ON, and the loop remained healthy in connected standby, but neither crown nor lift could wake the display and no input diagnostics appeared.
- **Crown mistake:** do not gate a latched PMIC event on the interrupt pin still being asserted at a later loop poll. Latch the GPIO edge in ISR context, inspect/clear AXP2101 status in normal context, and retain a bounded status-poll fallback.
- **Sensor mistake:** do not duplicate register decoding when the exact pinned sensor library already exposes the required read API. Use SensorLib 0.3.3 `getAccelerometer()` and log failures.
- **Observability rule:** any release-candidate wake source must have explicit hardware telemetry before disabling the previous wake path. Crown edges/status and asleep accelerometer samples must be visible in serial logs.
- **Hardware fact:** T-Watch S3 has a 3-axis BMA423, not a 6-axis IMU; never promise gyro telemetry that does not exist.
- **Artifact rule:** failed M12 remains immutable; correction is M12-build2.


## M12-build2 -> M12-build3: wake detection must be power-state aware

**Mistake:** lift-to-wake was implemented as periodic XYZ polling while the display was off. That couples wake behavior to continued application-loop execution and repeats a previously encountered watch-power error.

**Rule:** wake inputs are hardware events, not UI-loop features. For Reticulum connected standby, consume the BMA423 GPIO14 feature interrupt directly. For any future true light/deep sleep, explicitly arm that same physical line as an ESP wake source. Never remove the previous wake path until the replacement has produced hardware-observable events in the intended power state.


## M12-build3 -> M12-build4: feature/IRQ success is not sensor-liveness proof

- **Observed hardware failure:** connected-idle CPU remained alive, crown wake worked, BMA423 feature/IRQ calls reported success, but every accelerometer sample was exactly `(0,0,0)` and GPIO14 produced zero edges for many minutes.
- **Rule:** a successful sensor API return does not prove meaningful acquisition. Persistent all-zero acceleration on a stationary Earth-bound watch is a failed liveness test, because gravity must appear on at least one axis.
- **Rule:** before arming a motion wake feature, explicitly enable the measurement engine using the exact pinned library API and prove fresh non-zero samples.
- **Rule:** feature-engine and interrupt configuration may only be considered armed after the underlying sensor acquisition path passes liveness.
- **Architecture rule:** XYZ samples may qualify/diagnose the sensor, but GPIO14 BMA423 TILT/WAKEUP remains the authoritative wake event; do not regress to application polling as the wake detector.
- **Artifact rule:** M12-build3 remains immutable; this correction is M12-build4.


## M13: release-candidate feature freeze means do not invent fixes without evidence

- **Accepted baseline:** M12-build4 lift-to-wake now works on hardware.
- **Rule:** M13 changes are validation, packaging, remaining UI corrections and evidence-backed bug fixes only. Do not alter protocol, storage layout, radio framing or the accepted wake interrupt path merely because a theoretically nicer implementation exists.
- **Lift sensitivity:** pinned SensorLib 0.3.3 does not expose a public BMA423 wake-sensitivity setter. Do not fake a lighter threshold by changing unrelated accelerometer settings, adding application-loop motion polling, or patching the dependency from memory.
- **Release rule:** if M13 acceptance passes, publish stable instead of opening M14 by default.

## v1.0.0: stable means no surprise features

- Do not introduce a new runtime feature during RC-to-stable graduation.
- Stable release packaging/version changes must not modify the frozen Reticulum/LXMF/radio boundary.
- Every delivered build must state plainly what changed and give the user an observable hardware test for each changed behavior.
- If a build is packaging/documentation-only, say so explicitly rather than implying a new watch feature exists.

## Post-stable releases: distinguish visible changes from regression checks

- **Rule:** after v1.0.0, do not invent an M-number merely to continue development; use semantic versions unless a new formal milestone program is explicitly created.
- **Rule:** every release handoff must say what changed, where the user can see it, exactly how to test it, and what PASS looks like.
- **UI rule:** when correcting optical alignment, keep the adjustment local to the affected element instead of shifting unrelated controls.

## Post-1.0 messaging UX: never confuse submission with delivery

- A send-state popup must describe what the firmware actually knows. **"Sending" / "Submitting to Reticulum"** is valid while the local LXMF path is running; **"Delivered"** requires the existing proof-backed evidence.
- If a synchronous communication call can visibly stall the UI, render/defer through the UI loop before invoking it rather than showing a popup after the blocking work has already happened.
- All text-send entry points should share the same bounded send-state path so composer, thread quick replies and notification replies cannot drift into inconsistent behavior.

## v1.2.0 packaging failure — malformed generated C++ string literal

The original v1.2.0 source split the `Serial.printf("[m4:compose] send requested chars=%u\\n", ...)` literal across physical source lines. PlatformIO correctly failed with `missing terminating \" character` and a secondary `expected ')'` error.

Permanent rule:

- Never treat static substring/structure checks as a substitute for C/C++ lexical validity.
- Generated logging strings that need a newline must contain the escaped sequence `\\n`, not an actual newline inside the quoted literal.
- Keep failed versioned artifacts immutable. The correction is `v1.2.0-build2`, not a replacement v1.2.0 ZIP.
- Release preflight must guard the corrected composer diagnostic string form.


## v1.3: expose state before changing protocol

When deeper Reticulum functionality is requested, prefer surfacing already-known path/peer/link state in the UI before changing the hardware-accepted packet/radio boundary. Read-only diagnostics are lower-risk than protocol rewrites and provide evidence for later changes.

## v1.3 navigation: visible screen is not enough; preserve origin explicitly
- List-item callbacks must resolve the callback object reliably; for LVGL 9 use `lv_event_get_current_target_obj()` when matching the object whose handler is executing.
- Do not hard-code Thread Back to Messages/Conversations when a thread can be entered from Peer Details, Inbox, notifications, or other contexts.
- Peer Details must preserve whether it was entered from Peers or Contacts.
- A local favorite/pin must never be described as changing Reticulum routing, delivery, or link priority.

## v1.4 power work: do not spend energy proving hardware wake every second

- **Observed:** the accepted M12 wake path is hardware-interrupt driven, but release builds still performed one diagnostic BMA423 XYZ read and one serial heartbeat every second throughout connected standby.
- **Rule:** once hardware wake has been proven, diagnostic sampling must not become permanent release-mode polling. The authoritative wake path remains GPIO14/BMA423 feature IRQ; standby diagnostics must be sparse and must not drive wake decisions.
- **Rule:** power optimization must preserve Reticulum receive unless a lower-connectivity mode is explicitly user-selectable and clearly labelled.
- **Measurement rule:** expose session active/standby/wake counters so subsequent endurance work is based on observed behavior rather than intuition.

## v1.5: voice/PTT state must be visible, not serial-only

- A hold-to-talk control is ambiguous if the screen does not say when recording begins, what release does, or why the control is unavailable.
- Voice UI must expose the active-Link prerequisite explicitly; do not make a disabled/failed PTT control look broken.
- Long-running local operations (recording/playback/encoding) need visible progress/state while keeping radio/RNS processing non-blocking.
- Continue to call this a recorded LXMF voice message/PTT burst, not live telephony, unless a separately verified streaming protocol is implemented.


## v1.6 RC1: 240px UI must be designed for physical fit, not source-string intent

- Long page titles must use the full safe header width and reduce font size before clipping.
- Conversation headers should not duplicate peer-management controls already available on Peer Details; preserve space for the peer name and one compact Info action.
- Tiny action buttons must use stable short labels (for example canned replies `1` / `2`) instead of truncating arbitrary user text.
- Release checks should assert button coordinates and widths remain inside the 240px display.
- Keyboard haptics must be independently debounced from general UI taps so the first typed character still has tactile confirmation.

## v1.6.1 final cleanup — do not confuse recorded LXMF voice bursts with interoperable PTT
- The retired PTT button was a local hold/release UX over recorded LXMF `FIELD_AUDIO`, not LXST PTT.
- Real interoperable Reticulum PTT belongs to LXST-capable implementations and must not be represented by a private or UI-only approximation.
- Keep the interoperable recorded VOICE workflow; future live/PTT work requires a separate LXST-based milestone/version.
