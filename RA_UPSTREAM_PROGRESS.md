# Native RetroAchievements Progress

## Repository

- Upstream: `https://github.com/samyost1/tmc-android.git`
- Upstream branch: `dual-screen-native`
- Fork: `https://github.com/joeblack2k/tmc-android.git`
- Integration branch: `feature/native-retroachievements-port`
- Draft PR: `https://github.com/samyost1/tmc-android/pull/16`
- Android RA package: `dev.picori.tmc.ra`

Only the `TMC/tmc-android` repository is writable for this port. ROMs, saves,
credentials, device captures, and local parity artifacts stay ignored and are
never part of the PR.

## Product Contract

- The TMC product exposes RetroAchievements in **Casual** mode only.
- There is no Spectator option, fallback, toggle, or product UI.
- The TMC adapter rejects every non-Casual mode request.
- Achievement evaluation uses the native `rcheevos` runtime.
- The normal dual-screen, input, launcher, and save paths remain in use.
- Achievement unlocks enqueue a four-second badge toast on the lower display.
- RetroAchievements HTTP requests use the recognized `SkyEmu/4.0` identity.
- Submission remains fail-closed unless the canonical ROM, game 559, GBA
  console, memory manifest, and checked-in memory attestation all match.

The reusable native RA library still defines its generic Spectator enum/API for
other consumers. TMC does not admit or expose it.

## Casual Attestation

`tools/ra/generate_tmc_ra_casual_attestation.py` generates the checked-in
`port/ra/tmc_ra_casual_attestation.generated.h` from a strict synchronized S0
native/mGBA parity result.

Current evidence:

- strict result: PASS
- selected and matched bytes: `141830 / 141830`
- mismatch, unknown, and provenance mismatch ranges: none
- attested ranges: 126
- official active set memory references checked: 94 unique addresses
- uncovered official references: none
- previously missing figurine bytes are now packed at their reviewed GBA
  offsets

At runtime, reads outside the attested ranges are rejected before reaching the
memory bridge and the destination buffer is zeroed. Wrong ROM, wrong game,
wrong console, stale manifest, malformed ranges, invalid current-frame bytes,
and non-Casual mode all fail closed.

## Verification

Verified on 2026-08-08:

| Gate | Result |
|---|---|
| Attestation generator self-test and checked output | PASS |
| Python RA tool suite, 20 tests | PASS |
| Strict synchronized S0 comparison | PASS, `141830 / 141830` |
| Native memory and adapter tests | PASS |
| Native policy, runtime, UI bridge, capture contract, and toast tests | PASS |
| Native RA lifecycle suite | PASS |
| Focused host RA runtime, UI bridge, and toast tests | PASS under ASan/UBSan |
| Complete host `tmc_pc` rebuild | BLOCKED locally by missing `png.h`; Android sources compile for both ABIs |
| Android Java unit tests | PASS, 21 tasks |
| Android native `arm64-v8a` release build | PASS |
| Android native `x86_64` release build | PASS |
| RA APK assemble and lint vital | PASS |
| APK package/manifest inspection | PASS |
| APK signature verification | PASS, v1 and v2 |
| TMC product source/UI Spectator search | PASS, no product path |

The complete macOS rebuild reaches `port/port_prelaunch_logo.cpp` and then
stops because the local host environment has no libpng development header.
The inherited `include/region.h` parentheses warnings, one inherited offset
macro redefinition, and Gradle's inability to strip `libmain.so` remain
non-fatal baseline warnings.

## Thor Evidence

Device: AYN Thor, arm64.

The current APK is installed as the only `dev.picori.tmc*` package. The local
and installed APK SHA-256 match:
`e025db900fd514b14b04afb9d80c92f09ce5da3b81b5e96b222366a8b0b70ded`.

Physical checks:

- install and cold start: PASS
- canonical ROM load and native asset startup: PASS
- normal Link save load: PASS
- primary game display: PASS
- lower map/HUD display: PASS
- controller/keyboard input through title and file select: PASS
- credential restore after APK update: PASS
- authenticated account status: ONLINE
- mode shown by the product: CASUAL
- Minish Cap set and achievement list sync: PASS
- recognized client identity: `SkyEmu/4.0`
- active client set on device: `0/66`, `0/705` points
- conditional `Warning: Unknown Emulator` entry: absent
- account, game, rich presence, totals, and achievement rows: no overlap
- `A New Quest` is visible and still locked for the user's normal-play test
- unlock toast presentation and UI bridge contracts: PASS in focused tests

The user chose to perform the real achievement unlock personally. Until that
normal-play test is completed, local toast display, counter increment, and
server-side unlock history are the only remaining physical acceptance gate.
No debug unlock, cheat, save-state, or synthetic submission is used.

## Security

- No ROM, save, credential, token, keystore, or private capture is tracked.
- The generator stores only public identity and deterministic attestation
  metadata in source.
- Local Android signing uses the existing debug signer; no release signing
  material was supplied.
- Git diff checks, ignored-file review, and secret scanning are required
  immediately before publication.

## User Test

1. Open the installed Project Picori RA build on the Thor.
2. Keep the lower display on the `RA` tab and confirm `ONLINE` and `CASUAL`.
3. Play normally from the prepared Link save.
4. Obtain the Broken Picori Blade for `A New Quest`.
5. Confirm the four-second badge toast, the local count increment, and the
   unlock on the RetroAchievements website.
