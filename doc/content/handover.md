# Horizon Light Controller — Technical Handover

> Lebendes Dokument — wird überschrieben/ergänzt, nicht angehäuft. Für den
> chronologischen Verlauf siehe `history.md`, für offene Punkte `backlog.md`.

## Projekt-Übersicht

Ein DMX-Lichtcontroller für Moving Heads, basierend auf einem **ESP32-C3
Supermini**. Steuerung über ein reaktives Web-Interface (React 18), das
direkt vom ESP32-Flash (LittleFS) ausgeliefert wird — kein externer Server,
keine App nötig. Zusätzlich Art-Net-Empfang (Universe 0) für DMX von externer
Lichtsoftware, mit Vorrang vor internen Effekten.

Dieses Repository (`Moving_Head_Horizon`) ist das Ergebnis eines Merges
(2026-08-14) aus zwei separaten Vorgänger-Ständen — Details in `history.md`:
- Backend aus `horizon_light_controller` (vollständigste Endpunkt-Sammlung).
- Frontend aus einem Zweig namens "V3" (neuester UI-Stand mit
  `AudioVisualizer`, `useBeatPulse`, Toast-Notifications, Landscape-Hinweis).

## Architektur

### Backend (ESP32 C++)

- **`Moving_Head_Horizon.ino`** — Hauptschleife (`setup()`/`loop()`),
  Hardware-/Kanal-Konfiguration (`CH_*`-Defines, feste 18-Kanal-Personality),
  globaler State, DMX-Ausgabe via Hardware-UART1 (Software-Break über
  `uart_set_line_inverse`), Art-Net-Empfang (`onArtDmx`), Scene-/
  Chaser-Execution (`executePreset`, `executeChaserSlot`, `triggerLoad`),
  Frame-Engine (`updateEngines`). Bindet `WebAPI.h` bewusst erst **nach**
  allen Globals/Hilfsfunktionen ein (Arduino verkettet Übersetzungseinheiten
  textuell — Include-Reihenfolge = Deklarationsreihenfolge).
- **`WebAPI.h`** — REST-artige Endpunkte (`/save`, `/joy_in`, `/set_all`,
  …). Presets/Chaser-Szenen als gepackter Binary-Blob (`SceneData` via
  `prefs.putBytes()`), um NVS-Fragmentierung durch viele Einzelkeys zu
  vermeiden. Vollständige Endpunktliste mit Parametern:
  siehe `functions.md`.
- **`FX_Engine.h`** — `MovementEngine` (Kinematik/Formen fürs Pan-Tilt,
  Phasenversatz pro Fixture für Chase-Muster), `Modulator` (LFO für
  Dimmer/Prisma/Gobo-Rotation), `StepFX` (diskrete Wheel-Schritte für
  Farbrad/statisches Gobo/rotierendes Gobo). Float-Accumulator
  (`exactPan`/`exactTilt` in `updateEngines`) gegen Rundungsdrift bei
  langsamen Joystick-Fahrten.
- **`Audio_Engine.h`** — I2S-Audio-Sampling (Pins 4/5/6) für Beat-Detection
  (Bass/Mid/High über Envelope-Follower + dynamischer Threshold) und
  automatisches BPM-Tracking (Median-Filter über Rolling-History,
  rate-limited Poll alle 40 ms).

### Frontend (React / Babel, `data/index.html`)

- Monolithisches Single-File-Frontend, React 18 + Babel Standalone über CDN
  (`<script type="text/babel">`), kein Build-Schritt — JSX wird im Browser
  transpiliert. In mehrere IIFE-`<script>`-Blöcke gegliedert (Hooks/
  Primitives, geteilte Widgets, Tab-Komponenten).
- **State Management:** zyklisches Polling. `/api/state` alle 500 ms
  (kompakter Live-State: BPM, aktiver Preset, Chaser-Status, Audio-Trigger-
  Flags), `/api/get_dmx` alle 2000 ms (voller State inkl. aller FX-Parameter,
  für UI-Resync z. B. nach Reload). Slider-Änderungen triggern `tFetch`
  (debounced Fetch), um den ESP nicht mit Requests zu fluten.
- **Joystick:** eigener Hook `useKeyboardJoystick` für Tastatur/Maus mit
  mehrstufigem Ramping (Shift/Alt-Modifier), sendet `joyInputX/Y` an
  `/joy_in`.

## Performance & Skalierung

- **Kein FPU am ESP32-C3** (RV32IMC, 1 Core @ 160 MHz). Jedes
  `sinf/cosf/powf/expf` in `FX_Engine.h` ist Software-Emulation
  (libgcc soft-float) — das ist der einzige nennenswerte CPU-Kostenpunkt im
  System, skaliert mit *Fixture-Anzahl × aktive FX*.
- **Aktueller Stand (2 Fixtures): unkritisch.** `getValues()` macht ~4
  Form-Trig + 2 Rotations-Trig pro Fixture, bei 2 Fixtures ≈ 12
  soft-float-Trig-Calls pro Loop-Durchlauf — einstelliger Prozentbereich der
  Loop-Zeit. DMX-Frame bei 2 Fixtures ~1,6 ms gegen 30-ms-Sendetakt
  (~33 Hz) — nie das Nadelöhr. Headroom bis ~8 Fixtures gut.
- **Hauptsächlicher Optimierungs-Hebel (noch nicht implementiert):**
  `updateEngines()` baut den kompletten Output-Buffer inkl. `getValues()`
  und `memset`/`memcpy(513)` in *jedem* Loop-Durchlauf (Hunderte Hz),
  gesendet wird aber nur alle 30 ms (~15× Overhead). Output-Assemblierung
  (und ggf. die ganze FX-Engine-Verarbeitung) in den
  `if (now - lastDmxOut >= 30)`-Block ziehen → Movement-Soft-Float-Last
  sinkt um ~Faktor 15. Gefahrlos, weil alle `.process()`-Methoden dt-basiert
  integrieren und `getValues()` zustandslos ist (33 Hz reicht für einen
  Moving Head locker). Macht 8 Fixtures + Hardware-Joystick entspannt —
  siehe `backlog.md`.
- **Kleinere Wins:** Rotationsmatrix (`cosf/sinf(rRad)`) ist über alle
  Fixtures identisch, wird aber pro Fixture neu berechnet — einmal pro
  Frame reicht. JSON-String-Bauen in `/api/get_dmx` churnt Heap, läuft aber
  nur bei 0,5 Hz Polling → kein CPU-Thema (WebSocket-Migration steht im
  Backlog).

### Hardware-Alternativen (bewertet, keine Migration geplant)

FPU-Status: **C3** (RV32IMC) keine FPU, 1 Core 160 MHz · **S2** (Xtensa LX7)
keine FPU, 1 Core 240 MHz, kein Bluetooth · **S3** (Xtensa LX7)
**Hardware-FPU**, Dual-Core 240 MHz · **C5** (RV32IMAC) keine FPU, 1 HP-Core
240 MHz + LP-Core, Dualband-Wi-Fi 6 (2,4 + 5 GHz).

- **S3** wäre das einzige echte Compute-Upgrade (Hardware-FPU + zweiter
  Core, der DMX/Engine von WiFi/HTTP entkoppeln könnte).
- **C5** bringt keinen FPU-Gewinn, aber sauberes 5-GHz-Band für Art-Net in
  vollen Venues — adressiert Funkstabilität, nicht FX-Performance.
- **S2** ist für dieses Projekt ohne Mehrwert.

Entscheidung (siehe `history.md`, 2026-06-14): **beim ESP32-C3 Supermini
bleiben.** Der Output-Build-Hebel (siehe oben) gibt genug Headroom für die
aktuell absehbare Skalierung, ohne Hardware-Wechsel.

## Geplante Erweiterungen (Design-Notizen)

### Hardware-Joystick via ADS1115 (I²C)

- Schreibt direkt `joyInputX/Y` in der Firmware → spart HTTP-Roundtrip,
  entlastet die CPU zusätzlich.
- **Pflicht: nicht-blockierend lesen.** Default 128 SPS = ~8 ms/Conversion;
  ein blockierender `Wire`-Read stallt den Loop und zerstört DMX-Timing.
  Lösung: rate-limited Poll (20–40 ms, wie `pollAudioEngine()`), Datenrate
  hoch (bis 860 SPS ≈ 1,2 ms) oder Single-Shot-Statemachine.
- I²C ist frei (Audio nutzt I²S). Pin-Budget C3 beachten: 4/5/6 = I²S,
  7 = DMX-TX → 2 freie GPIOs für SDA/SCL wählen.

### Preset-Engine-Split (complete / movement / color / effects)

- Ziel: Movement live aus einem Slot recallen, ohne Color/Dimmer/Effekte zu
  verstellen.
- Performance-Impact ~null (reine Architektur).
- `SceneData` trennt Gruppen bereits per Präfix: `f*` = Movement,
  `c*/sg*/rg*` = Color/Gobo, `d*/gr*/pr*` = Modulatoren. Partial-Recall
  heißt also v. a. „welche Feldgruppe kopiere ich".
- **Knackpunkt:** `executePreset` resettet aktuell global (`centerPan16`,
  `centerTilt16`, `dimSmoothTarget`, `joySmoothX/Y`, `mapIsMoving`). Ein
  Movement-only-Recall darf statische DMX-/Color-Werte und Dimmer NICHT
  anfassen — dort liegt die eigentliche Refactoring-Arbeit. Partielle
  Recall-Endpunkte oder eine Feldgruppen-Maske statt eines atomaren Apply.

## Setup & Flashen

1. Arduino IDE (oder `arduino-cli`): Board **"ESP32C3 Dev Module"** wählen.
2. **Wichtig:** `USB CDC On Boot` auf **Disabled**, damit der Hardware-Reset
   nach dem Flashen funktioniert.
3. Upload Speed auf **115200** drosseln, um Timeouts zu vermeiden.
4. `ArtnetWifi`-Library installieren (einzige Nicht-Core-Abhängigkeit).
5. Den kompletten `data/`-Ordner (inkl. `data/vendor/`!) auf LittleFS
   hochladen (Filesystem-Uploader-Plugin, oder `pio run -t uploadfs` bei
   PlatformIO). **Der `/upload_gui`-Fallback-Endpunkt reicht seit
   2026-08-15 nicht mehr als alleiniger Upload-Weg** — er ersetzt nur
   `index.html` selbst, nicht `data/vendor/`. Eine so installierte UI würde
   auf `/vendor/react.js` mit 404 laufen und nicht laden. Er ist nur als
   Erstinbetriebnahme-/Recovery-Pfad für die HTML-Datei gedacht, solange
   noch gar kein `index.html` im Flash liegt.

### Kompilieren ohne Arduino IDE (PlatformIO)

`platformio.ini` im Repo-Root erlaubt einen reinen Kompilier-Check per
Kommandozeile (`pio run`), z. B. für Claude Code zur Selbstverifikation nach
Codeänderungen — **ersetzt nicht** das Flashen/Testen auf echter Hardware.
Board-ID `nologo_esp32c3_super_mini` (PlatformIO-Profil, das exakt dem
ESP32-C3 Supermini entspricht), `src_dir = .` lässt PlatformIO direkt auf
den bestehenden `.ino`/`.h`-Dateien im Root bauen (keine Kopie nach `src/`
nötig — das Sketch bleibt für die Arduino IDE unverändert nutzbar).
`.pio/` ist reiner Build-Cache, gefahrlos löschbar.

**Verifiziert am 2026-08-15:** Nach dem Fix der `colFX`/`sgobFX`/`rgobFX`-
Deklarationen kompiliert das Sketch sauber. Flash-Auslastung dabei: **90,3 %**
(1.183.197 von 1.310.720 Bytes der App-Partition) — wenig Puffer für neue
Features, siehe `backlog.md`. Eine harmlose Compiler-Warnung bleibt: die
I2S-API in `Audio_Engine.h` nutzt das von ESP-IDF als deprecated markierte
`driver/i2s.h` statt `driver/i2s_std.h` — kompiliert noch, aber Migration
vor einem künftigen ESP-IDF-/arduino-esp32-Versionssprung sinnvoll.
