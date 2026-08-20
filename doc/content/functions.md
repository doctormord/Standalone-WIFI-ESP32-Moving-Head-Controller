# Function & API Reference

English reference for every non-trivial function/method in the firmware and
every HTTP endpoint exposed by `WebAPI.h`. Kept in sync with the code — when
a function's signature or parameters change, update its entry here in the
same change. This file is a living reference, not append-only (unlike
`history.md`).

Slot numbering note used throughout: **presets are 1-indexed** in their
public API (`/save?slot=1..10`, `/recall?slot=1..10`), **chaser slots are
0-indexed** internally (`chaserScenes[0..9]`). See `backlog.md` for the
tracked inconsistency between `executePreset` (1-based access) and
`executeChaserSlot` (0-based access).

---

## `Moving_Head_Horizon.ino`

### `void loadAllChaserScenes()`
No parameters. Loads all 10 preset/chaser slots from NVS (`Preferences`,
namespaces `"sc1"`..`"sc10"`) into `presetNames[]` and `chaserScenes[]`.
Tries the packed `SceneData` blob (`prefs.getBytes("data", ...)`) first;
falls back to reading legacy individual keys (per-channel bytes, per-effect
fields) if no blob is present, so older NVS contents remain loadable. Called
once from `setup()` and again after every `/save`.

### `void triggerSceneFX(int slot)`
- `slot` — **0-indexed** index into `chaserScenes[]` (0–9).

Copies the FX configuration (movement, dimmer, gobo-rotation,
prism-rotation, color, static gobo, rotating gobo) from `chaserScenes[slot]`
into the live FX engine objects (`moveFX`, `dimFX`, `gRotFX`, `pRotFX`,
`colFX`, `sgobFX`, `rgobFX`) and starts/stops each engine based on its saved
`active` flag. Does **not** touch `dmxData[]` or pan/tilt — that is the
caller's responsibility (see `executeChaserSlot`). Used by both chaser
step-advance and `executeChaserSlot`.

### `void executePreset(int slot)`
- `slot` — **1-indexed** preset number (1–10) as used by the public API.

Recalls preset `slot` (reads `chaserScenes[slot - 1]`): stops the chaser,
copies all 18 DMX channels into `dmxData[]`, reconstructs 16-bit
`centerPan16`/`centerTilt16` from the coarse+fine channel pairs, restores
every FX engine's full parameter set (via direct field assignment, not
`triggerSceneFX`), resets joystick smoothing state, and starts/stops each FX
engine per its saved `active` flag. Sets `activePresetSlot = slot`.

### `void executeChaserSlot(int slot)`
- `slot` — **0-indexed** index into `chaserScenes[]` (0–9).

Recalls chaser slot `slot`: copies DMX channels 2–18 directly and routes
channel 1 (dimmer) through `dimSmoothTarget` instead, reconstructs
`centerPan16`/`centerTilt16`, resets joystick smoothing, then delegates FX
restoration to `triggerSceneFX(slot)`.

### `void triggerLoad(int type, int param)`
- `type` — `1` = preset, `2` = chaser slot.
- `param` — slot number, indexed per `type` (1-based for presets, 0-based
  for chaser slots — matches `executePreset`/`executeChaserSlot`).

Entry point used by `/recall` and internal chaser step-advance to switch to
a new preset/chaser slot. If `dipToBlack` is enabled, stores the pending
load in `pendingLoadType`/`pendingLoadParam` and starts a fade-to-black
(`isDipping = true`); the actual `executePreset`/`executeChaserSlot` call
happens later in `updateEngines()` once the fade reaches black. If
`dipToBlack` is disabled, calls `executePreset`/`executeChaserSlot`
immediately.

### `void setupDMX()`
No parameters. Configures UART1 for DMX512 output (250000 baud, 8N2,
no parity, no hardware flow control) on `transmitPin` and clears
`dmxBuffer[]`. Called once from `setup()`.

### `void onArtDmx(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)`
- `universe` — Art-Net universe of the received packet; only `0` is handled.
- `length` — number of channel bytes in `data`.
- `sequence` — Art-Net sequence number (unused).
- `data` — raw DMX channel bytes, `data[0]` = channel 1.

`ArtnetWifi` callback registered via `artnet.setArtDmxCallback`. On universe
0, force-stops the chaser and every FX engine (external DMX always takes
priority over internal effects) and copies up to `NUM_CHANNELS` (18) bytes
from `data` into `dmxData[1..]`.

### `void updateEngines(unsigned long now)`
- `now` — current time in milliseconds (`millis()`), passed in from `loop()`.

The per-frame engine tick, called every `loop()` iteration. Responsibilities,
in order:
1. Computes `dt` since the last call; returns early if `dt <= 0`.
2. Joystick-driven pan/tilt: applies curve response (`joyCurve`) and
   momentum smoothing to `joyInputX/Y`, integrates into float accumulators
   `exactPan`/`exactTilt` (to avoid rounding drift at slow speeds), clamps
   to `panMinLimit/panMaxLimit`/`tiltMinLimit/tiltMaxLimit`, writes back to
   `centerPan16`/`centerTilt16` and (if `moveFX` inactive) directly into the
   pan/tilt DMX channels.
3. "Map go" mode (`mapIsMoving`): eases `exactPan/Tilt` toward
   `mapTargetPan/Tilt` instead of following joystick deltas; clears
   `mapIsMoving` once within 5 units of the target.
4. Beat/tempo bookkeeping: advances `lastBeatTime` on the internal BPM
   clock, applies manual tap (`manualTap`) and audio-trigger
   (`triggerBass/Mid/High`) phase resets to any FX engine configured with
   that trigger mode.
5. Processes active FX engines (`moveFX.process`, `gRotFX.process` →
   channel 9, `pRotFX.process` → channel 11, `dimFX.process`).
6. Dimmer smoothing (`dimSmoothVal`-driven blend) and auto-fade
   (`autoFading`/`fadeStateOut`/`fadeCurve`) combine into `fadeMultiplier`,
   applied together with `masterBrightness` to produce the final dimmer
   byte (`dmxData[CH_DIMMER]`).
7. Dip-to-black state machine (`isDipping`): once the fade reaches black,
   performs the deferred `executePreset`/`executeChaserSlot` from
   `pendingLoadType/Param` and starts the fade back in.
8. Step-FX advance (`runStep` lambda) for `colFX`/`sgobFX`/`rgobFX`
   against `wheelMap`/`sGoboMap`/`rGoboMap`, gated by each effect's trigger
   mode (time/BPM-sync/audio).
9. Chaser auto-advance (if `chaserActive`): hold/fade timing between
   `currentSlot`/`nextSlot`, including per-channel interpolation during
   `inFade` and pan/tilt interpolation from the 16-bit coarse+fine pairs.
10. Assembles the final per-fixture output frame `outDmx[513]` from
    `dmxData[]` at each patched fixture's DMX address (`fixtures[]`), with
    per-fixture pan/tilt (via `moveFX.getValues()` if movement is active,
    otherwise direct inversion) and bump overrides (blackout/blinder/
    strobe).
11. Every ~30 ms (`lastDmxOut` gate): sends the DMX break
    (`uart_set_line_inverse` for 120 µs, mark-after-break 12 µs) and writes
    `dmxBuffer[0..maxDmxChannel]` over UART1.

### `void setup()`
No parameters. Arduino entry point, runs once. Initializes `Serial` and
`LittleFS`, loads all persisted settings from `Preferences` (WiFi
credentials, dimmer smoothing, master brightness, dip-to-black, joystick
tuning/limits, chaser config, fixture patch), connects to WiFi in station
mode (falls back to AP mode `Moving_Head_Ctrl`/`12345678` after ~20s if no
connection), starts mDNS (`movinghead.local`), Art-Net, the web server
(`setupAPI()` + `server.begin()`), the DMX UART (`setupDMX()`), loads all
scenes (`loadAllChaserScenes()`), and initializes the audio engine
(`initAudioEngine()`).

### `void loop()`
No parameters. Arduino main loop. Calls `server.handleClient()`,
`ArduinoOTA.handle()`, `artnet.read()`, `pollAudioEngine()`,
`updateEngines(millis())`, then `delay(2)`.

---

## `FX_Engine.h`

### `struct StepFX`
Discrete wheel-position stepper (used for color wheel / static gobo /
rotating gobo). Fields: `active`, `startVal`/`endVal` (index range into the
associated map array), `step` (increment per trigger, 1 or 2), `trigger`
(0=time, 1=BPM-sync, 2/3/4=audio bass/mid/high), `sync` (index into
`syncBeats[]`, 0–6), `holdTime` (ms, used when `trigger == 0`),
`lastStepTime`, `currentIdx` (current position), `scratch` (if true, offsets
the mapped output value by +183, wrapped to 0–255 — a "scratch" look for
gobo effects).

### `class Modulator`
LFO used for dimmer, prism-rotation, and gobo-rotation channels.

- **`Modulator(int minV, int maxV)`** — constructor; sets `startVal`/`endVal`
  (the output range the LFO oscillates between).
- **`void start()`** — sets `active = true` and resets `lastUpdate = millis()`.
- **`void stop()`** — sets `active = false`.
- **`float getLFO(float p, int m, int c)`**
  - `p` — phase, 0.0–1.0.
  - `m` — waveform mode: `0` = forward saw, `1` = ping-pong (triangle),
    `2` = reverse (decay).
  - `c` — response curve applied to the waveform value: `0` = linear,
    `1` = quadratic, `2` = cubic, `3` = sine ease, `4` = gaussian,
    `5` = random (re-rolled every call).
  - Returns the shaped 0.0–1.0 value for the given phase/mode/curve.
- **`void process(unsigned long now, float beatsElapsedTotal, const float* syncBeats, float &outVal)`**
  - `now` — current `millis()`.
  - `beatsElapsedTotal` — the shared beat clock's continuous "how many real
    beats have elapsed" reference (`beatCount` + fractional progress within
    the current beat, computed once per `updateEngines()` tick in the
    `.ino`) — **not** `masterSyncTime`/`globalBPM` directly (see 2026-08-20
    in `history.md` for why: `masterSyncTime`/a naive `% interval` both
    broke every multi-beat `sync` divisor, since they get re-anchored on
    every single detected beat).
  - `syncBeats` — pointer to the 7-entry beat-division table (`syncBeats[]`
    in the `.ino`); indexed by `sync` — **not bounds-checked here**, see
    `backlog.md`.
  - `outVal` — **output parameter**: receives `startVal + (endVal - startVal) * getLFO(...)`.

  Advances `phase` either freely (time-based, `trigger == 0` or `>= 2`,
  `speed` treated as the LFO's full cycle duration in ms) or, for
  `trigger == 1` (BPM-synced), sets it directly from
  `(beatsElapsedTotal / syncBeats[sync])`'s fractional part — phase-exact,
  independent of how often the beat clock itself gets re-anchored — then
  wraps `phase` into 0–1 and computes `outVal` via `getLFO`.

### `class MovementEngine`
Parametric pan/tilt pattern generator (circles, figure-8s, etc.).

- **`void start()`** / **`void stop()`** — same semantics as `Modulator`.
- **`void process(unsigned long now, float beatsElapsedTotal, const float* syncBeats)`**
  Same parameter meanings as `Modulator::process`, except `syncBeats` here
  is `moveSyncBeats[8] = {1,2,4,8,16,32,64,128}` (beats/revolution — a
  separate table from `Modulator`'s short sub-beat divisors, since a
  movement pattern takes real time to trace and can't lock to a sub-beat
  divisor). Advances `modPhase` (time- or BPM-synced — `modSp` is a period
  in ms, same units/formula as `Modulator::speed`, not a raw rate
  multiplier), shapes it into `mVal` via `modMo`/`modCu`, derives
  `currentSize`/`currentSpeed` from `szSt/szEn`/`spdSt/spdEn` blended by
  `mVal`. For `trigger == 1` (BPM-synced), `enginePhase` is set directly
  from `modPhase * 2π` (phase-exact, one revolution starts exactly on a
  beat and ends exactly at the end of the `sync` beat count); otherwise
  it's integrated by `currentSpeed * dt * 5.0` (wrapped to 0–2π). This is
  the shape's animation clock, independent of the size/speed modulation
  phase.
- **`void getValues(int centerP, int centerT, int fixturePhase, bool invP, bool invT, int &outP, int &outT)`**
  - `centerP`/`centerT` — center pan/tilt (16-bit, 0–65535) to offset from.
  - `fixturePhase` — per-fixture phase offset in degrees (0–360), lets
    multiple fixtures trace the same shape out of sync for chase effects.
  - `invP`/`invT` — per-fixture pan/tilt inversion (from `fixtures[]` patch).
  - `outP`/`outT` — **output parameters**: resulting absolute pan/tilt
    (16-bit, clamped 0–65535).

  Evaluates the shape selected by `type` (1–12: circle, figure-8, various
  Lissajous-style curves, square, line, etc.) at phase `enginePhase +
  fixturePhase`, scales by `currentSize`, applies the `rot` rotation matrix,
  applies inversion, and offsets from `centerP`/`centerT`.

  Before that final offset, the tilt axis gets one more adjustment: a
  confirmed hardware defect on the deployed fixture unit (its tilt
  response is non-monotonic below 16-bit DMX 32768, ~8-bit tilt 127/128 —
  see `mapping_sheds_160w_3in1_gobo.md` for the full diagnosis, ruled out
  as a calibration/projection/software issue) means any pattern whose full
  tilt excursion would dip below that point comes out as a self-crossing
  figure-8 on the real fixture instead of the shape actually requested.
  `centerT` is shifted up (never down) by just enough that the pattern's
  conservative worst-case tilt swing (`currentSize * 32767 * (|sin(rot)| +
  |cos(rot)|)`) never needs to cross 32768 — the shape comes out correct,
  just centered a bit higher than the raw `centerT` argument when that
  argument alone would have crossed the fold. Only applied here (Movement
  FX), not to manual/joystick tilt.

---

## `Audio_Engine.h`

### `void pollAudioEngine()`
No parameters. Called every `loop()` iteration; internally rate-limited to
once per `AUDIO_POLL_INTERVAL_MS` (40 ms) and no-ops entirely if
`hwAudioEnabled` is false. Reads `SAMPLES` (64) 32-bit I2S samples, updates
three envelope followers (`envFast`/`envMid`/`envSlow` — leaky integrators
at different attack/decay bit-shift speeds over the *same* unfiltered
signal, not a real frequency-domain split; this project's "fake FFT"),
derives bass/mid/high energy bands (`lastBassEnergy`/`lastMidEnergy`/
`lastHighEnergy`, held for `/api/audio_debug` to read), and:
- Detects bass beats against a dynamically smoothed threshold
  (`dynThreshold`, adjusted by `hwAudioSensitivity`, held as
  `lastThBass`), enforcing a minimum gap of `MIN_BEAT_INTERVAL_MS`. On
  detection, updates a rolling median-filtered BPM estimate
  (`beatIntervals[]`, `BPM_HISTORY_SIZE` = 16 samples, needs
  `BPM_MIN_VALID_SAMPLES` = 6 to compute), smooths it into `globalBPM`,
  advances the shared `beatCount`/`lastBeatTime`/`masterSyncTime` beat
  clock incrementally. Deliberately does **not** set `manualTap` (that's
  reserved for an actual tap-tempo gesture via `/beat` — see 2026-08-20 in
  `history.md` for why reusing it here broke every multi-beat BPM-sync
  divisor: `manualTap`'s handler in the `.ino` unconditionally zeroes
  `beatCount`, which happened on every single detected beat, one loop()
  iteration after `beatCount` had just been incremented).
- Falls back to `BPM_DEFAULT_FALLBACK` (120) after `SILENCE_TIMEOUT_MS`
  (2500 ms) of no detected beats.
- Sets `triggerMid`/`triggerHigh` (and the sticky `guiMid`/`guiHigh` flags
  for the UI, latched-until-read by `/api/state`) when mid/high energy
  crosses their thresholds, each with its own debounce
  (`MID_DEBOUNCE_MS`/`HIGH_DEBOUNCE_MS`).
- `triggerBass`/`triggerMid`/`triggerHigh` are cleared at the start of
  *every* call (far more often than the 40ms audio-processing throttle
  lets them actually change) and only set again if a new event fires this
  cycle — they're one-shot pulses meant only for the same-tick
  `updateEngines()` consumer, **not** pollable from outside (an HTTP
  request can't realistically catch a value that survives microseconds).
  `dbgBassHit`/`dbgMidHit`/`dbgHighHit` are the separate, actually-latched
  equivalents `/api/audio_debug` uses instead.
- All of the above tuning (attack/decay shifts, threshold divisors, noise
  floor) lives in runtime `inline int tune*` variables (not `#define`s),
  settable live via `/audio_tune` — see below.

### `void initAudioEngine()`
No parameters. Configures and installs the I2S driver (master/RX, 8 kHz,
32-bit samples, mono/left channel) on the pins defined by
`I2S_WS`/`I2S_SCK`/`I2S_SD`, and clears the BPM history buffer. Called once
from `setup()`.

---

## `WebAPI.h`

### `void setupAPI()`
No parameters. Registers every HTTP route below on the global `WebServer
server` and either serves `data/index.html` from LittleFS directly, or (if
no `index.html` is present yet) serves a minimal setup page at `/` with an
upload form posting to `/upload_gui` — the first-boot GUI provisioning path.
Called once from `setup()`.

### HTTP endpoints

All endpoints respond `200 OK`/`text/plain "OK"` unless noted otherwise.
Query parameters are read via `server.arg(name)`; parameters marked
"optional" only update state when present (`server.hasArg`).

| Endpoint | Method | Parameters | Description |
|---|---|---|---|
| `/` | GET | — | Serves the SPA (`index.html` from LittleFS), or the setup-mode upload page if no GUI has been installed yet. |
| `/upload_gui` | POST (multipart) | `update` (file) | Uploads a new `index.html` to LittleFS (first-boot provisioning path); reboots the device. |
| `/api/get_dmx` | GET | — | Full state snapshot as JSON: all 18 DMX channel values, pan/tilt (`cp`/`ct`), BPM, active preset, chaser-active flag, preset names, dimmer-smoothing/fade/master-brightness/dip/hw-audio state, and every FX engine's complete parameter set. Polled by the frontend every ~2s. Also includes `op`/`ot`: fixture 0's actual live Movement FX output (post-`getValues()`, what really goes out over DMX) — unlike `cp`/`ct`, which are only the pattern's *center* and stay static while an FX animates around them. Debug fields, added 2026-08-20 to diagnose the tilt fold defect (see `MovementEngine::getValues()` above) by sampling the real output trajectory instead of guessing at it. |
| `/api/state` | GET | — | Compact live state as JSON: firmware version (`fw`, from `FW_VERSION`), active preset, BPM, chaser-active, hw-audio-enabled, fade-out flag, one-shot audio trigger flags (`trB`/`trM`/`trH`, cleared after read), preset names. Polled by the frontend every ~500ms. |
| `/api/patch` | GET | — | JSON array of the current fixture patch (`addr`, pan/tilt invert, phase) for each patched fixture. |
| `/save` | GET | `slot` (1–10), `n` (preset name) | Snapshots the current live DMX channels + all FX engine parameters into `chaserScenes[slot-1]` and persists it (plus `n` as the preset name) to NVS namespace `"sc<slot>"`. Reloads all scenes afterward. |
| `/chaser_cfg` | GET | `st`, `en` (start/end slot), `fade`, `hold` (ms), `tr` (trigger mode), `sy` (sync index), `o` (order: 0=forward, 1=random), `ftr` (fade trigger mode), `fsy` (fade sync index) — all optional | Updates chaser timing/config and persists it to NVS namespace `"sys"`. |
| `/joy_cfg` | GET | `spd` (max speed), `crv` (response curve exponent), `mom` (momentum, 0–100 → stored as 0–1), `pr`/`tr` (`"1"` = pan/tilt reversed), `pmin`/`pmax`/`tmin`/`tmax` (8-bit coarse pan/tilt limits, expanded to 16-bit) | Updates joystick tuning/limits and persists to NVS `"sys"`. |
| `/joy_in` | GET | `x`, `y` (float, -1..1) | Sets live joystick input (`joyInputX/Y`), consumed by `updateEngines()`. |
| `/set_all` | GET | `c1`..`c18` (any subset, byte values) | Directly sets raw DMX channels, disabling the chaser and clearing `activePresetSlot`; channel 1 also stops `dimFX` and sets `dimSmoothTarget`, channels 3/15/4/16 also update `centerPan16`/`centerTilt16`. |
| `/fx` | GET | `a` (`"1"`=active), `t` (shape type 1–12), `r` (rotation degrees), `ss`/`se` (speed start/end), `zs`/`ze` (size start/end), `mm`/`mc` (modulation mode/curve), `ms` (modulation speed), `tr` (trigger mode), `sy` (sync index) | Configures `moveFX` (`MovementEngine`); starts it fresh if it was inactive and `a=1`. |
| `/modfx` | GET | `pfx` (`"dim"`/`"gr"`/`"pr"` — selects `dimFX`/`gRotFX`/`pRotFX`), `a`, `st`/`en` (range, clamped 0–255), `sp` (speed), `mo` (mode), `cu` (curve), `tr`, `sy`, `mv` (manual value to restore on stop; dim writes `dimSmoothTarget`, gr/pr write CH9/CH11 directly) | Configures the selected `Modulator`. |
| `/chaser` | GET | `act` (`"1"`=active/`"0"`=stop), `start`/`end`/`fade`/`hold`/`trg`/`sync`/`ord`/`f_trg`/`f_sync` — same meaning as `/chaser_cfg`, all optional | Toggles the chaser. On activation, resets to `chaserStartSlot` and calls `executeChaserSlot` immediately. |
| `/recall` | GET | `slot` (1-based preset number) | Calls `triggerLoad(1, slot)` — recalls a preset (subject to dip-to-black if enabled). |
| `/kill_fx` | GET | — | Stops every FX engine and the chaser, clears `activePresetSlot`. |
| `/bump` | GET | `t` (`"blinder"`/`"strobeF"`/`"strobe50"`/`"blackout"`), `s` (`"1"`=on/`"0"`=off) | Momentary override toggle for a bump effect, applied per-fixture in the output stage. |
| `/masterdim` | GET | `v` (0–100) | Sets `masterBrightness` (stored as 0.0–1.0) and persists to NVS `"sys"`. |
| `/smooth` | GET | `v` (0–100) | Sets `dimSmoothVal` (dimmer smoothing amount) and persists to NVS `"sys"`. |
| `/autofade` | GET | `t` (fade duration ms), `c` (fade curve: 0=linear/1=quad/3=cosine) | Toggles `fadeStateOut` and starts an auto-fade (mute/unmute) with the given duration and curve. |
| `/unmute` | GET | — | Immediately cancels any active fade and forces full brightness (`fadeMultiplier = 1.0`). |
| `/trans` | GET | `dip` (`"1"`=enable) | Sets `dipToBlack` (fade-to-black before preset/chaser loads) and persists to NVS `"sys"`. |
| `/hwaudio` | GET | `en` (`"1"`=enable), `sens` (0–100 sensitivity) | Enables/disables the I2S audio-reactive engine and sets its sensitivity. |
| `/api/audio_debug` | GET | — | Returns live band energies (`lo`/`mi`/`hi`), the live bass threshold (`th`), one-shot latched beat-hit flags (`xb`/`xm`/`xh`, cleared on read), and every current `tune*` value (`nf`/`fa`/`fd`/`ma`/`md`/`sa`/`sd`/`mtd`/`htd`) plus `sens`. Built with a fixed-buffer `snprintf` (not this file's usual sequential `String +=`) since the AUDIO DEBUG tab polls it at ~15Hz. Added 2026-08-20 for the AUDIO DEBUG tab's scrolling graph. |
| `/audio_tune` | GET | `nf` (0–2000), `fa`/`fd`/`ma`/`md`/`sa`/`sd` (0–10, attack/decay bit-shifts), `mtd`/`htd` (0–10, threshold divisor shifts) — all optional, each only applied if present | Sets the runtime-tunable envelope-follower parameters in `Audio_Engine.h` (`tuneFastAttackShift` etc.) that used to be compile-time `#define`s. Added 2026-08-20 alongside `/api/audio_debug`. |
| `/colfx` | GET | `a`, `st`/`en` (wheel index range), `ho` (hold time ms), `tr`, `sy`, `mv` (manual CH6 value to restore on stop) | Configures `colFX` (color wheel `StepFX`); auto-derives `step` (1 or 2) from start/end parity so odd/even wheel positions aren't mixed. |
| `/sgobfx` | GET | `a`, `st`/`en`, `ho`, `tr`, `sy`, `sc` (`"1"`=scratch), `spd`/`rng` (shake rate/intensity), `mv` (manual CH7 value to restore on stop) | Configures `sgobFX` (static gobo `StepFX`). |
| `/rgobfx` | GET | `a`, `st`/`en`, `ho`, `tr`, `sy`, `sc` (`"1"`=scratch), `spd` (shake stage 1–5), `mv` (manual CH8 value to restore on stop) | Configures `rgobFX` (rotating gobo `StepFX`). |
| `/save_patch` | POST | `n` (fixture count), `a0..a(n-1)` (DMX start address), `ip0..`/`it0..` (`"1"`=invert pan/tilt), `ph0..` (phase degrees) | Replaces the fixture patch (`fixtures[]`, up to 8), persists to NVS `"patch"`, recomputes `maxDmxChannel`. |
| `/map_go` | GET | `p`, `t` (target pan/tilt, 16-bit float) | Sets `mapTargetPan/Tilt` and enables "map go" easing mode (`mapIsMoving = true`) — used by the frontend's 2D position-map UI. |
| `/save_map` | POST | request body = raw file content | Writes the POST body verbatim to `/map.json` on LittleFS (stage-map image/config for the position-map UI). |
| `/load_map` | GET | — | Streams `/map.json` from LittleFS (`application/json`), or `{}` if it doesn't exist. |
| `/set_wifi` | GET | `s` (SSID), `p` (password) | Persists station-mode WiFi credentials to NVS `"sys"` and reboots. |
| `/beat` | GET | — | Manual tap-tempo hit: rewinds `lastBeatTime` by one beat interval and sets `manualTap = true` (phase-resets any manually-triggered FX on the next `updateEngines()` pass). |
| `/sync` | GET | — | Hard-syncs FX phases to zero and resets `masterSyncTime = millis()`. For trigger==1 (BPM-sync) FX, phase is derived every tick from the shared `beatCount`/`lastBeatTime` clock (not the FX's own `.phase`/`.modPhase` field), so this also resets `beatCount = 0` and `lastBeatTime = millis()` — a per-FX `.phase = 0` write alone is a no-op for that trigger mode. |
| `/jog` | GET | `v` (signed int) | Sets `jogBend = v` (or `0` if `v == 0`). **Currently has no effect** — `jogBend` is never read elsewhere; see `backlog.md`. |
