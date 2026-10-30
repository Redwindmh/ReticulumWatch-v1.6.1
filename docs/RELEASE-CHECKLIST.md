# ReticulumWatch release checklist

This checklist is mandatory before issuing a new firmware artifact.

1. Read `docs/MISTAKE-LEDGER.md` and identify any prior failure mode relevant to the changed subsystem.
2. State the exact milestone acceptance criterion and the known-good baseline that must remain untouched.
3. Keep the patch narrow. Any unrelated change requires an explicit reason in the release notes.
4. Run `python3 scripts/release_preflight.py` and require a PASS result.
5. Run `python3 scripts/check_partitions.py` and require both partition maps to pass.
6. Compare all frozen radio/storage/interface files against the immediately preceding known-good baseline.
7. Inspect the complete source diff, not just the edited function.
8. If PlatformIO is available, perform the real target build before packaging. If it is not available, say so explicitly and do not claim build verification.
9. Never treat a successful compile as runtime verification, and never treat a later upload failure as a build failure.
10. For critical startup services, propagate initialization failure to the top-level boot state. Never print an unconditional success banner after a failed subsystem.
11. Package into a new immutable version number, test ZIP integrity, generate SHA-256, and never overwrite an already issued artifact.
12. After the user returns logs, update milestone status from actual evidence only.

For M1.4.x specifically, `Reticulum::start()` must not run before the required microStore filesystem object is initialized and registered, even though RNS persistence itself remains disabled.

### External dependency API gate
- Static preflight protects local invariants only; it must never label external API compatibility as verified.
- Compile against the exact resolved dependency/toolchain before calling an artifact build-verified.
- If that compiler is unavailable in the artifact environment, state `UNVERIFIED` and use the user's target build as the gate.
- Prefer compile-time contracts based on actual expressions (`decltype`, `static_assert`) over hard-coded guesses about concrete adapter classes/namespaces.
