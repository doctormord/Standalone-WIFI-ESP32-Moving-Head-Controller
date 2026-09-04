# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware + web UI for an ESP32-based DMX moving-head light controller ("Moving Head Horizon"). A single Arduino sketch drives DMX512 output over UART, listens for Art-Net on WiFi, runs beat-reactive/LFO-driven lighting effects, and serves a React control panel from the ESP32's own flash filesystem — no external server or app required.

This is a merged codebase (see `README.md`): backend (`Moving_Head_Horizon.ino`, `WebAPI.h`, `FX_Engine.h`, `Audio_Engine.h`) came from one project variant, the frontend (`data/index.html`) from another. They were reconciled by checking that every frontend `fetch()` call matches a backend route/parameter name — keep that invariant when editing either side.

## Repo layout

- `Moving_Head_Horizon.ino` — entry point: hardware config, global state, DMX engine (`updateEngines()`), scene/preset execution, `setup()`/`loop()`. `#include "WebAPI.h"` happens near the bottom, deliberately *after* the globals and helper functions it references (Arduino concatenates translation units, so include order = declaration order here).
- `FX_Engine.h` — effect primitives: `StepFX` (discrete wheel stepper), `Modulator` (LFO), `MovementEngine` (pan/tilt pattern generator). No global instances live here — they're declared in the `.ino`.
- `Audio_Engine.h` — I2S mic sampling, envelope followers, and audio-reactive/BPM beat detection (`pollAudioEngine()`, `initAudioEngine()`).
- `WebAPI.h` — all HTTP routes (`setupAPI()`), included from the `.ino`. Routes are thin: parse `server.arg(...)`, mutate the global FX/state objects, optionally persist to `Preferences`.
- `data/index.html` — the entire web UI: a single-file React app loaded via UMD `<script>` tags with in-browser Babel (`type="text/babel"`), split into several `<script type="text/babel">` IIFE blocks (hooks/primitives, shared widgets, tab components). No JS build step — edit this file directly and it's transpiled client-side on load. **Each `<script type="text/babel">` block is its own JS scope** — a top-level `const` declared in one block is not visible in another (bit us twice already: two same-named but differently-shaped `COLORS` arrays in different blocks, see `doc/content/history.md` 2026-08-15). When adding a shared constant, check which block actually needs it rather than assuming it's globally visible.
- `data/vendor/*.gz` — React/ReactDOM (production, minified) and Babel Standalone (minified), gzip-compressed, served from LittleFS via dedicated routes in `WebAPI.h` (`/vendor/react.js` etc., each setting `Content-Encoding: gzip` explicitly). Replaces a former CDN dependency (`unpkg.com`) that left the UI blank whenever the device had no internet uplink — notably the WiFi AP fallback (`Moving_Head_Ctrl`) used at venues with no WLAN. Babel Standalone alone is 2.4MB unminified/minified — only fits the 896KB LittleFS partition gzip-compressed (~548KB). If you ever need to update these vendor files, re-verify they still fit with `pio run -t buildfs` (fails loudly with `LFS_ERR_NOSPC` on overflow — verified by deliberately overflowing it once, see history).

## Build / deploy

No build system in-repo (no Makefile, no arduino-cli project file, no package.json). This is a standard Arduino sketch:

- Target board: **ESP32-C3 Supermini** (Arduino board "ESP32C3 Dev Module"). I2S mic on pins 4/5/6, DMX TX on pin 7, UART1 in inverted-signal mode.
- When flashing: set `USB CDC On Boot` to **Disabled** (otherwise the hardware reset after upload doesn't work) and drop upload speed to **115200** to avoid timeouts.
- **If `pio run -t upload` can't auto-reset the board into bootloader mode** (native-USB ESP32-C3 boards can be finicky about this — happened 2026-08-20, see `doc/content/history.md`), do **not** work around it by writing `.pio/build/*/firmware.factory.bin` directly with `esptool` as one blob at offset `0x0`. That merged image is contiguous from `0x0` through past `0x10000` and overwrites everything in that byte range at the flash level — including the `nvs` partition at `0x9000` (WiFi credentials, fixture patch, master brightness, every preset/chaser slot) and `otadata` at `0xe000`, silently wiping the device's saved config even though the firmware itself flashes fine. This actually happened once (2026-08-20) and required reconfiguring the device from scratch via its AP fallback. Use `scripts/flash_esptool.sh` instead — it writes `bootloader.bin`/`partitions.bin`/`boot_app0.bin`/`firmware.bin` at their own individual offsets (matching what `pio run -t upload` does when it works), leaving the `nvs` gap between them untouched. Manually enter bootloader mode first (unplug, hold BOOT, plug in, wait ~2s, release), then run it with `--before no-reset` semantics (already baked into the script) so esptool doesn't reset the board out of that state before connecting.
- The C3 has **no hardware FPU** (RV32IMC) — every `sinf`/`cosf`/`powf`/`expf` in `FX_Engine.h` is software-emulated. This is the system's only notable CPU cost, and it scales with fixture count × active effects. See `doc/content/handover.md` → "Performance & Skalierung" before adding more soft-float work to the per-loop path (`updateEngines()` currently rebuilds the output buffer every loop iteration, ~15× more often than the 30ms DMX send cadence — a known, not-yet-applied optimization lever, see `doc/content/backlog.md`).
- Library dependencies (beyond ESP32 core): `ArtnetWifi`. Everything else (`WiFi`, `WebServer`, `Preferences`, `ArduinoOTA`, `ESPmDNS`, `Update`, `LittleFS`, `driver/uart.h`, `driver/i2s.h`) ships with the ESP32 board package.
- `data/index.html` must be uploaded separately to the device's LittleFS (e.g. via an Arduino LittleFS data-upload tool, or the fallback `/upload_gui` HTTP endpoint in `WebAPI.h` that appears automatically when no `index.html` is present on the filesystem yet).
- OTA updates are supported via `ArduinoOTA` once the device is on WiFi.
- There is no automated test suite; verification is manual (flash + observe DMX/web behavior).

**Compiling from Claude Code / the command line:** `platformio.ini` in the repo root lets you compile-check the sketch without the Arduino IDE — run `pio run` from this directory. It targets board `nologo_esp32c3_super_mini` (PlatformIO's exact profile for the ESP32-C3 Supermini) via the `espressif32` platform, with `src_dir = .` so it builds the `.ino`/`.h` files in place (no need to move them into a `src/` folder — that would break opening this folder as an Arduino sketch). `lib_deps` pulls in `rstephan/ArtnetWifi` automatically. First run downloads the RISC-V toolchain (`toolchain-riscv32-esp`) if not already cached; expect it to take a few minutes then, seconds afterward. This is compile-only verification — it does not flash hardware and does not replace testing on the actual device. `.pio/` is PlatformIO's build cache; safe to delete if it needs a clean rebuild.

**Testing the beat detector without hardware:** `sim/` compiles the real `Audio_Engine.h` natively against a fake Arduino and a fake I2S driver, and drives it from a synthesised track whose beat positions are known exactly -- see `sim/README.md`. Use it before touching any detector parameter. Three tuning sessions on hardware were wasted measuring against music that changed mid-sweep, and one produced a false "breakthrough" (a free-running oscillator that looked like a working detector because its lockout had been tuned to the beat interval). The simulator gives exact ground truth, deterministic runs, and F-measure/timing numbers in seconds. It found three real bugs on its first run. It does not model a microphone in a room, so a result there is necessary, not sufficient -- hardware still decides. Note `build_src_filter` in `platformio.ini` excludes `sim/` from the firmware build; it defines `main()` and would otherwise break `pio run`.

**Checking the UI actually parses:** run `scripts/check_ui.sh` after editing `data/index.html`. There is no build step -- Babel runs in the browser at load time -- so nothing else catches a syntax error before it is on the device, where it shows up as a blank page. The script transpiles each `<script type="text/babel">` block with the Babel already vendored in `data/vendor`, needs only `node`, and reports blocks separately because each is its own scope. It has already caught a duplicate identifier that would have blanked the header (2026-09-01), and the same class of bug blanked the AUDIO tab once before that.

**Partition table (changed 2026-09-03):** the project no longer uses the stock `default.csv`. `partitions_horizon.csv` in the repo root moves 512KB from the filesystem to the two OTA app slots — app slots **1.25MB → 1.5MB each (+20%)**, filesystem **1408KB → 896KB**. `nvs` stays at `0x9000` with its original size, so the saved configuration survives the switch; the filesystem offset moves from `0x290000` to `0x310000`, so **it must be re-flashed** and **OTA cannot make this change** (the table lives at `0x8000`). One USB flash with `scripts/flash_esptool.sh --fs`. Read the CSV before editing it — the comments say which numbers are load-bearing.

**Checking `data/` still fits on the device:** the LittleFS partition (`spiffs` in the partition table) is **896KB** since 2026-09-03, not the "~85% full" this file claimed until then (that figure was stale and had been discouraging UI additions for no reason). Current contents are ~810KB across four files (`index.html` ~216KB + `data/vendor/*.gz` 594KB, of which Babel alone is 548KB) — so ~86KB free, and shrinking as the UI grows. This is now the binding budget, not the app partition. Measured, not estimated: `pio run -t buildfs` builds the image, then count the 4096-byte blocks in `.pio/build/supermini/littlefs.bin` that are not entirely `0xFF`/`0x00` — 199 of 352 were in use. Still run `pio run -t buildfs` after touching anything under `data/`; it fails loudly (`LFS_ERR_NOSPC`) if the contents don't fit, rather than silently truncating, and it's fast (~1s). **The budget is now roughly balanced:** as of 2026-09-04 `pio run` reports Flash at **78.6%** (1,236,511 of 1,572,864 bytes) — ~336KB of headroom, up from ~78KB before the partition change. The filesystem is the tighter of the two now, and getting tight: ~810KB of 896KB used. `index.html` is served uncompressed; gzipping it the way the vendor files already are would free well over 100KB if it ever comes to that. Re-read the figure from `pio run` rather than trusting this line; it has been stale before. Note that deleting unreferenced code does not move it — the linker already drops what nothing calls, so removing dead code buys readability, not space.

## Architecture

**DMX pipeline.** `dmxData[1..18]` holds one fixture's channel values (a fixed 18-channel personality — see `CH_*` defines and `NUM_CHANNELS` at the top of the `.ino`). Every ~30ms, `updateEngines()` composes the final frame: it copies `dmxData` into `outDmx[513]` at each patched fixture's DMX address (`fixtures[]`, up to 8), computes that fixture's pan/tilt (with per-fixture invert + phase offset for chase patterns), applies bump overrides (blackout/blinder/strobe), then bit-bangs the frame out over UART1 as DMX512 (manual break via `uart_set_line_inverse`). `wheelMap`/`sGoboMap`/`rGoboMap` translate logical wheel positions to this specific fixture's color/gobo DMX values — swapping fixture models means updating the `CH_*` map and these tables.

**Art-Net takeover.** `onArtDmx()` (universe 0 only) writes straight into `dmxData` and force-disables all FX/chaser state — external DMX always wins over internal effects while active.

**Effects engines** (`FX_Engine.h`), each independently active/triggerable (manual/BPM-sync/audio-bass/mid/high):
- `MovementEngine` (`moveFX`) — parametric pan/tilt shapes (circle, figure-8, etc.), speed/size modulated by its own LFO.
- `Modulator` (`dimFX`, `gRotFX`, `pRotFX`) — LFO driving dimmer, gobo-rotation, and prism-rotation channels.
- `StepFX` (`colFX`, `sgobFX`, `rgobFX`) — steps through discrete wheel positions (color wheel, static gobo, rotating gobo) on each trigger. All seven FX globals (`moveFX`, `dimFX`, `gRotFX`, `pRotFX`, `colFX`, `sgobFX`, `rgobFX`) are declared together near the top of the `.ino`, right after `masterBrightness`.

**Known stub (documented in `README.md`):** `jogBend` is set by the `/jog` endpoint but never read in `FX_Engine.h` or the DMX output path — the jog wheel currently has no visible effect on the fixture.

User-supplied route parameters (`sync` indices, chaser/preset slot numbers, StepFX `st`/`en`, fixture-patch count, dimmer/audio-sensitivity percentages, joystick pan/tilt limits) are validated/clamped at their `WebAPI.h` entry points, with defense-in-depth clamps at the actual array-access sites (`runStep`'s map index, `Modulator`/`MovementEngine`'s `syncBeats[]` index) for values that can also arrive via NVS-persisted state. Keep new endpoints consistent with this — validate at the boundary, not deep in the engine. The full, current list of open issues lives in `doc/content/backlog.md`, not here — check there before re-deriving this from scratch.

**Scenes.** 10 preset slots and 10 chaser scenes share one `SceneData` struct (raw DMX channel snapshot + every FX engine's full parameter set) persisted via `Preferences` (NVS) under namespaces `"sc1"`..`"sc10"`. `executePreset()` / `executeChaserSlot()` apply a snapshot instantly and restart the relevant FX engines; `triggerSceneFX()` is the shared helper that re-arms FX state from a `SceneData`. Chaser mode auto-advances through a configurable slot range with fade/hold timing (`chaserTrigger`/`chaserSync` support BPM-synced steps), and `triggerLoad()` optionally inserts a fade-to-black ("dip") before switching if `dipToBlack` is enabled.

**Tempo/beat.** `globalBPM` can be driven by manual tap (`/beat`), or by the I2S mic pipeline in `Audio_Engine.h`: fast/mid/slow envelope followers over raw samples feed a dynamic threshold; bass beats update a median-filtered rolling BPM history, mid/high crossings become independent audio triggers usable by any FX engine's `trigger` mode.

**Joystick/pan-tilt.** Pan/tilt are 16-bit values centered at 32767, smoothed each frame using a curve+momentum response (`updateEngines()`), clamped to configurable limits, and split into coarse + fine DMX channels (`CH_PAN`/`CH_PAN_FINE`, `CH_TILT`/`CH_TILT_FINE`) for 16-bit resolution. A separate "map go" mode (`/map_go`) eases the head toward an absolute target instead of joystick deltas — used by the frontend's position-map UI.

**Web layer.** `WebServer` on port 80 serves the SPA from LittleFS and exposes query-param-style routes per subsystem (DMX/state polling, joystick input, per-effect config, chaser config, preset save/recall, fixture patch, WiFi setup, beat/sync/jog). Almost all state is persisted via `Preferences` (NVS): general settings under `"sys"`, scenes under `"sc1"`–`"sc10"`, fixture patch under `"patch"`. WiFi falls back to a local AP (`Moving_Head_Ctrl` / `12345678`) if no stored station credentials connect; mDNS name is `movinghead.local`.

## Anything crossing the frontend/backend boundary: check BOTH ends, both directions

Before changing a value that travels between `data/index.html` and the device, write down its
full round trip and confirm every leg carries it:

    control -> outgoing change detector -> URL -> route -> engine field
                                                              |
    control state <- poll merge <- /api/get_dmx <- ------------

**A value is only as good as its weakest leg, and a missing leg never announces itself.** Miss
the outgoing detector and the edit is never sent. Miss the route's `hasArg` and an older client
wipes it. Miss `/api/get_dmx` and the control cannot show what is running. Miss the poll merge
and the control shows a stale value. Miss the baseline fingerprint and the next edit is misread
as already-sent. None of these produce a compile error, a warning, or a log line — they produce
a control that does nothing, or a value that springs back about a second later.

So: **for every field, name the sender and name the receiver, in both directions.** If you cannot
point at both, the work is not finished. This applies to route parameters, telemetry fields,
persisted settings — anything with two ends. The concrete list below is this principle applied
to effect parameters, where the round trip happens to be eleven steps long.

## Adding an effect parameter — the complete checklist

This used to say "four places". It is eleven, and **every single omission fails silently** — no
compile error, no console warning, just a control that does nothing or a value that reverts a
second later. Work down the list; do not trust memory.

**Firmware**

1. **The engine field** in `FX_Engine.h` (`Modulator` / `MovementEngine` / `StepFX`) and the code
   that reads it.
2. **`SceneData`** in the `.ino` — **append at the END, never insert.** Scenes are stored as a raw
   struct blob (`prefs.putBytes`) and the load checks the exact byte count, so changing `sizeof`
   makes every saved scene fail that check *silently*. Keep the previous layout as
   `SceneDataV1`; its `sizeof` is what recognises an old blob, whose prefix is byte-identical so
   the read still lands correctly and only the new tail needs defaults. Defaults must be the
   values that reproduce the OLD behaviour.
3. **Both migration branches** in `loadAllChaserScenes()` — the `SceneDataV1` one *and* the older
   per-key fallback below it. Note an `inline void migrate(SceneData&)` helper will not compile:
   the Arduino build hoists function prototypes above the struct definitions. Write it out inline.
4. **Scene application** — `triggerSceneFX()` / `executePreset()` / `executeChaserSlot()`.
5. **Scene save** — the `/save` handler in `WebAPI.h`.
6. **The route** (`/fx`, `/modfx`, …) — behind `server.hasArg()` so an older client that omits it
   leaves the value alone, and `constrain()`d, because the same value also arrives from NVS.
7. **`/api/get_dmx`** so the frontend can read it back. **Mind the commas:** those `json +=` lines
   already end with a separating comma, so append without a leading one and *with* a trailing
   one. Getting this wrong produces invalid JSON and a blank web UI. Verify by piping the
   endpoint through `json.load`, not by looking at it.

**Frontend — `data/index.html`, five places, all of them required**

8. **State default** in the `useState` initialiser (~`fxTr: 0, fxSy: 3, …`).
9. **The poll merge**, inside that group's `if (!xDirty) { … }` block. Omitted: the control never
   learns what the device is actually running.
10. **The `pr2.<group>State` fingerprint** in the merge. Omitted: the baseline and the state drift
    apart and the next edit is misread as already-sent.
11. **The `syncFx(...)` change-detector array AND the URL builder** — both, at the same call.
    `syncFx` only transmits when that array differs from the last sent copy, so **a field that is
    in the URL but not in the array can never be sent**: the edit is dropped and the next poll
    writes the device's old value back over it. That is the failure reported live on 2026-09-03
    as "die werte werden immer wieder überschrieben" — the parameter was in all ten other places.

Then the control itself. Where several effects share a component (`TriggerBlock`), adding it
there covers all of them at once — prefer that over four copies.

**Verify it end to end before believing it:** set the value over `curl`, then poll
`/api/get_dmx` several times and check it *stays*. A single readback proves nothing; the revert
takes about a second.

## Working practices & token efficiency

### Git workflow
- Never `git commit` or `git push` automatically after a fix or feature — work stays local until I explicitly say "commit" or "push".
- `git status` / `git diff` are fine anytime to check state — read-only, doesn't change anything.

### Bash output
- Filter build/test output before it hits context: `| tail -5` to `| tail -20`, not the full log.
  Example: `pio run 2>&1 | tail -5` instead of `pio run 2>&1 | tail -40`.
- `curl` debugging: use `-s` (silent) by default, not `-v`, unless HTTP headers themselves are the thing being debugged.
- Long logs: `| grep -E "ERROR|FAIL|SUCCESS"` instead of dumping full text.
- Large directories: `find . -maxdepth 2` instead of recursing everywhere.

### Reading files
- Search first with `grep -n "pattern" file` instead of reading whole files blind.
- When reading, pull only the relevant line range, not entire large files.
- Don't re-read a file already seen earlier in the same session — reuse that context.

### Multi-fix sessions
- When several independent fixes are needed, sketch the plan first, then implement in one pass rather than lots of small steps with heavy intermediate output.

## Debugging discipline

Written after a session that took a whole evening to find a fault that two minutes of the right
measurement would have shown. Every rule below is the generalisation of a specific mistake made
that day; the details are in `doc/content/history.md` (2026-09-02).

**Measure the input before theorising about the output.** When a reported value is wrong, count
the raw quantity that feeds it before reasoning about the code that publishes it. A tempo readout
bouncing between 122 and 61 was blamed in turn on the band, the anchor and the phase lock — three
real bugs, none of them the cause. Counting the onsets themselves settled it immediately: 96% of
kicks detected, 80% of the gaps correct. The detector was fine and the estimator was at fault.
Distinguish first: **is the input distribution clean?** If yes, the fault is downstream. If no,
it is level, sensitivity or detection. That one question orders everything after it.

**Set what you measure; never measure against a value you only read.** A setting can change
between the read and the measurement — twice in one day it did (`dSy` mid-run, and an FX that a
reboot had switched off), and both times the conclusion drawn was wrong. Measurement scripts
establish their own state.

**Ground truth is collected at the moment of measurement, not remembered.** Comparing against a
tempo from an earlier message is worthless; the music moved on. `tAnchor` against `tBPM` on
`/api/state` gives truth and measurement in the same instant.

**Carry level and onset rate in every audio measurement.** Without them a stable reading cannot be
distinguished from silence — which is exactly what happened once.

**A/B against the subsystem switched off.** Disabling the mic isolated the phase lock as the sole
source of the effect jitter in a single measurement (7% with audio, 3% without), after a lot of
reasoning had failed to.

**Suspect the measurement before the device.** Polling two endpoints in one loop halves the
sampling rate; the resulting "34% jitter" was entirely artefact, and gave itself away because the
outliers sat at near-exact multiples of the cycle. Long gaps in a sampled periodic signal usually
mean missed samples, not a stall.

**Verify the deployment, always.** File size is not proof, and a failed OTA has been mistaken for
a successful one. `/api/state` reports `bld` (build date and time) for exactly this.

### What the faults actually were

None of that day's bugs was a tuning value, and every attempt to tune one made things worse. All
five were **a rule compared against the wrong reference, or running on the wrong clock**:

- a counter incremented once per audio block (31/s) where it meant once per evaluation (1/s), so
  "20 evaluations" was 0.64 seconds;
- a band checked against the last accepted value and then reassigning it — a step limiter, not a
  band, so 15% per second walked the tempo anywhere;
- an error measured as "time since the last event minus the interval", which could only ever come
  out negative, so the correction worked in one direction only;
- a gate that rejected scattered windows and accepted tight ones, thereby preferring a consistent
  minority over an inconsistent majority;
- a clamp added against integer underflow that turned a deliberate quarter-of-the-error correction
  into a full snap.

So when something drifts, sticks or oscillates, ask **"what is this compared against, and on which
clock?"** before touching a parameter. Prefer correcting one rule to adding a compensating one.

**Check the other cases before trading one for another.** Disabling a path to fix drum & bass
removed the only way an anchor could expire, and broke track changes an hour later. If a change
makes one case better, name the case it makes worse and test that too.

**The simulator is necessary, not sufficient.** `sim/` gives exact truth and repeatable runs, and
parameter and algorithm work belongs there. But a method that scored 81% on synthesised patterns
scored 7% on the real track it was built for. Hardware decides.

**Separate what is established from what you invented.** "Standard practice in beat trackers" was
said of a three-line simplification; the underlying principle is standard, the specific form was
not. State which is which, unprompted.

## Documentation & language policy

- **Code, comments, commit messages, UI strings — always English.** No exceptions, regardless of the language a request comes in.
- **Project docs under `doc/content/` — always German**, except `doc/content/functions.md` (function/HTTP-API reference), which is English because it mirrors the English code/param names directly.
- **`README.md` (repo root) — always English**, and must be kept up to date as the project changes (features, hardware target, build steps, known issues). It's the pre-existing, richer project README (UI walkthrough, architecture diagram, channel map) — extend it in place rather than replacing it with something thinner.
- `doc/content/backlog.md` and `doc/content/handover.md` are living documents — edit/overwrite in place as state changes, don't accumulate dated copies.
- `doc/content/handoff.md` is a single current snapshot with a `NEXT CHAT STARTS HERE` banner — **replace it** each session (banner + status), don't append a new one alongside the old.
- `doc/content/history.md` is **append-only**: add a new dated section at the end for each session/milestone; never edit, reorder, or delete existing entries — if something turns out to be wrong or superseded, add a new entry that says so instead of correcting the old one in place.
- **Don't update `doc/content/` files after every intermediate debug step or micro-fix.** Write documentation once a task or session is actually done, or when explicitly asked to ("update docs", "write history"). If a session involves several fixes/iterations, collect them into one consolidated entry rather than one entry per fix.