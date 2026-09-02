# Horizon Light Controller — Verlauf

> **Regel: Diese Datei ist Append-only.** Neue Einträge werden unten mit Datum
> angehängt. Bestehende Einträge werden nicht verändert, umformuliert oder
> gelöscht — auch nicht, wenn sich ihr Inhalt später als überholt herausstellt
> (dann kommt ein neuer Eintrag, der das richtigstellt). Siehe `CLAUDE.md`.

---

## 2026-06-14 — Code-Review & Tech-Debt-Fixes (Vorgänger-Projekt `horizon_light_controller`)

Chat-Review-Session (Claude Opus 4.8) auf dem damaligen Stand des Backends
(`Moving_Head.ino`, `WebAPI.h`, `FX_Engine.h`, `Audio_Engine.h`), Frontend war
zu dem Zeitpunkt nicht Teil der Session. Damals 2 Fixtures (18-Channel Pro
Beam 280) im Live-Einsatz.

**Review-Ergebnis:** Architektur solide bewertet (Engines sauber in Header
getrennt, WebAPI getrennt, Polling-Modell mit debounced `tFetch` pragmatisch).
Sechs konkrete, verifizierte Schwachstellen gefunden:
1. `syncBeats[]` (7 Felder, Index 0–6) wurde an mehreren Stellen roh indiziert
   (`FX_Engine.h` `Modulator::process` / `MovementEngine::process`, `runStep`
   und Chaser-Step in `Moving_Head.ino`) ohne Clamp → Out-of-Bounds-Read bei
   `sync > 6` vom UI.
2. `/save` und die Recall-Pfade griffen ohne Bounds-Check auf
   `presetNames[s-1]` / `chaserScenes[s-1]` zu → `s=0` hätte Index -1 erzeugt.
3. Index-Konvention inkonsistent: `executePreset` 1-basiert
   (`chaserScenes[slot-1]`), `executeChaserSlot` 0-basiert
   (`chaserScenes[slot]`).
4. `jogBend` wird im `/jog`-Handler gesetzt, aber nirgends gelesen — toter
   Code / unfertiges Feature.
5. `fadeDuration` wird global zwischen Mute-Fade (`/autofade`) und
   Dip-to-Black-Load (`triggerLoad`) geteilt — fragile Kopplung.
6. DMX-TX ohne `uart_wait_tx_done` vor dem nächsten Break — bei vielen
   Fixtures (~22 ms Frame bei 512 Kanälen) könnte der nächste Break einen
   laufenden Frame anschneiden. Bei 1–2 Fixtures unkritisch.

**Umgesetzt in dieser Session:** Punkte 1 und 2 (die echten OOB-Risiken)
wurden direkt gefixt — `syncBeats`-Index überall geclampt (`constrain(...,0,6)`)
plus Div-by-Zero-Guard in beiden `process()`-Funktionen, sowie Slot-Bounds-
Guards in `executePreset` (1–10), `executeChaserSlot` (0–9) und `/save`.
Punkte 3–6 blieben als Tech Debt offen.

**Zusätzlich verifiziert:** Preset-Save/Recall wurde Feld für Feld geprüft —
alle 57 `SceneData`-Felder plus alle 18 DMX-Kanäle round-trippen korrekt in
beiden Recall-Pfaden (`executePreset` und `triggerSceneFX`/`executeChaserSlot`).
Einzige Nuance: Step-FX (Color/Gobo) springen beim Recall auf `startVal`
zurück, während LFO-Modulatoren und Movement ihre laufende Phase behalten
(`start()` setzt nur `lastUpdate`, nicht `phase`/`modPhase`/`enginePhase`).
Bewusst so gewollt (User-Feedback: "ist doch organischer, wenn die Bewegung
einfach weiterläuft") — kein Bug, kein Handlungsbedarf.

**Hardware-Diskussion (ESP32-C3 vs. S2/S3/C5):** C3 (RV32IMC) hat keine FPU —
`sinf/cosf/powf/expf` laufen als Software-Emulation (libgcc soft-float), das
ist der einzige nennenswerte CPU-Kostenpunkt der FX-Engines. Bei 2 Fixtures
unkritisch (~12 Trig-Calls/Loop), Headroom bis ~8 Fixtures. Vergleich:
S3 (Xtensa LX7, Dual-Core, **Hardware-FPU**) wäre das einzige echte
Compute-Upgrade; C5 (RV32IMAC) bringt keinen FPU-Gewinn, aber 5-GHz-Wi-Fi-6
für saubere Funkbedingungen im Live-Einsatz; S2 wurde als für dieses Projekt
sinnlos eingestuft (keine FPU, kein Bluetooth). Entscheidung: **beim C3
bleiben** — der größte verfügbare Hebel ("Output-Build-Kadenz": Output-Buffer
+ `getValues()` laufen aktuell pro Loop statt nur alle 30 ms beim DMX-Send,
~15× Overhead) wurde als kostenloser Performance-Puffer identifiziert, aber
in dieser Session **nicht implementiert** (nur bewertet).

**Nicht umgesetzt, nur dokumentiert (für spätere Sessions):**
- Hardware-Joystick via ADS1115 (I²C) — Pflicht: nicht-blockierend lesen
  (rate-limited Poll wie Audio-Engine), sonst stallt der Loop.
- Preset-Engine-Split (complete/movement/color/effects) — Knackpunkt:
  `executePreset` resettet aktuell global; ein Movement-only-Recall darf
  statische DMX-/Color-Werte nicht anfassen.

Ergebnisse wurden damals in `handover.md`, `backlog.md` und `handoff.md` des
Vorgänger-Projekts (`horizon_light_controller`) festgehalten.

---

## 2026-08-14 — Merge zu `Moving_Head_Horizon`

Backend aus `horizon_light_controller` (einziger Stand mit vollständigen
Endpunkten: `/jog`, `/beat`, `/sync`, `/chaser` als Toggle) und Frontend aus
einem separaten Projektzweig "V3" (neuester UI-Stand: `AudioVisualizer`,
`useBeatPulse`, Toast-Notifications, Landscape-Hinweis, netzwerkoptimierter
Joystick) wurden zu diesem Repository (`Moving_Head_Horizon`) zusammengeführt.
Alle Fetch-Aufrufe im V3-Frontend wurden gegen die Handler in `WebAPI.h`
geprüft — Parameter stimmen überein. Bekannter offener Punkt (bereits aus dem
Vorgänger übernommen): `jogBend` wird gesetzt, aber nirgends gelesen — Jog-Wheel
hat aktuell keine sichtbare Wirkung. Siehe `Readme.md`.

---

## 2026-08-15 — CLAUDE.md, Doku-Struktur (`doc/content`) und Regressions-Fund

`CLAUDE.md` für dieses Repository erstellt (Architekturüberblick, Build-Hinweise
für zukünftige Claude-Code-Sessions).

Beim Durchlesen des Backends fielen zwei Dinge auf, die den Stand aus der
Session vom 2026-06-14 betreffen:

1. **Neuer, compile-blockierender Bug:** `colFX`, `sgobFX` und `rgobFX`
   (die drei `StepFX`-Instanzen für Farbrad, statisches Gobo und rotierendes
   Gobo) werden in `Moving_Head_Horizon.ino` und `WebAPI.h` durchgängig
   verwendet, sind aber **nirgends als globale Objekte deklariert** — anders
   als `moveFX`/`dimFX`/`gRotFX`/`pRotFX`, die oben im `.ino` stehen. Ohne
   diese drei Deklarationen kompiliert das Sketch nicht. Vermutlich beim
   Merge vom 2026-08-14 verlorengegangen oder nie in den gemergten
   Backend-Stand übernommen worden.
2. **Regression gegenüber der Session vom 2026-06-14:** Die damals gefixten
   Punkte — `syncBeats[]`-Clamping (`FX_Engine.h:68`/`:117`,
   `Moving_Head_Horizon.ino:314`/`:329`) und die Slot-Bounds-Guards in
   `executePreset`/`executeChaserSlot`/`/save` — sind im aktuellen,
   gemergten Stand **nicht vorhanden**. Der für den Merge verwendete
   Backend-Snapshot war offenbar älter als der gefixte Stand aus der
   Juni-Session, oder der Fix wurde beim Zusammenführen nicht mitgenommen.
   Beide Punkte wurden als offene Tech-Debt-Einträge wieder in
   `backlog.md` aufgenommen.

Auf Wunsch des Users wurde außerdem die Projekt-Doku neu strukturiert:
`doc/content/{backlog,handover,handoff,history}.md` (Deutsch) und
`doc/content/functions.md` (Englisch, vollständige Funktions-/API-Referenz
mit allen Parametern) angelegt, `Readme.md` auf Englisch als lebendes
Projekt-Dokument aktualisiert. Ausgangsmaterial war u. a. ein Export des
Chats vom 2026-06-14 (`claude-exports-ESP32_ArtnetDMX.zip`, bereitgestellt
vom User), aus dem der obige Verlaufseintrag für den 2026-06-14 rekonstruiert
wurde.

**Zielhardware bestätigt:** ESP32-C3 Supermini (RV32IMC, kein Hardware-FPU,
1 Core @ 160 MHz) — siehe Kapitel "Hardware-Diskussion" oben. Kein
Umstieg auf S2/S3/C5 geplant.

---

## 2026-08-15 (Fortsetzung) — `Moving_Head_redesign.zip` ausgewertet, Build-Blocker gefixt

User stellte `/Users/christian/Documents/Arduino/Moving_Head_redesign.zip`
bereit (bereits entpackt liegend unter `/Users/christian/Documents/Arduino/
Moving_Head/`) zum Vergleich mit diesem Repo. Enthielt einen älteren
Projektstand (Dateien vom 8.–9. Mai 2026) mit drei relevanten Fundstücken:

1. **Backend** (`Moving_Head.ino`, `WebAPI.h`, `FX_Engine.h`,
   `Audio_Engine.h`) — nahezu textidentisch mit dem hier gemergten Backend
   (nur Kommentare unterscheiden sich), aber mit einem entscheidenden
   Unterschied: **`StepFX colFX, sgobFX, rgobFX;` ist hier korrekt als
   globale Instanz deklariert.** Das bestätigt zweifelsfrei den am
   2026-08-15 (erster Eintrag oben) gefundenen Build-Blocker und liefert den
   exakten, verifizierten Fix. Diese Version hat allerdings noch **nicht**
   die Endpunkte `/beat`, `/sync`, `/jog` und den `/chaser`-Toggle — die
   wurden offenbar später ergänzt (Zwischenstand zwischen diesem
   Redesign-Snapshot und dem für den Merge verwendeten
   `horizon_light_controller`-Stand), vermutlich genau bei dieser Ergänzung
   ist die `StepFX`-Deklarationszeile verlorengegangen.
2. **`data/index.html`** ("PRO FIXTURE CONSOLE") — die ursprüngliche,
   Vanilla-JS-basierte Oberfläche (kein React), Vorläufer der heutigen
   React-UI.
3. **`data/index_claude.html`** ("Light Controller · Horizon", datiert
   24.05.2026) — eine React-Neugestaltung derselben Oberfläche, laut
   Dateiname von einer früheren Claude-Session erstellt. Nahezu identisches
   Komponenten-Set wie das aktuelle `data/index.html` dieses Repos, aber:
   `useFakeTelemetry` statt echtem `/api/state`-Polling, `WifiPanel`-Buttons
   ganz ohne `onClick`-Handler (rein visueller Mockup), `AudioVisualizer` mit
   simulierter Fallback-Animation statt echter Trigger-Daten. Kein
   `useKeyboardJoystick`, kein `FirmwareInfo`/OTA-Panel.

**Fix angewendet:** `StepFX colFX, sgobFX, rgobFX;` in
`Moving_Head_Horizon.ino` ergänzt (neben `moveFX`/`dimFX`/`gRotFX`/`pRotFX`).
Build-Blocker damit behoben.

**Frontend-Vergleich (detailliert, siehe Antwort im Chat für Beispiele):**
kein Merge nötig. Stichprobenartiger Line-Count-Vergleich mehrerer
Komponenten (`AudioVisualizer`, `WifiPanel`, `StageMap`, `ProgrammerTab`, …)
zeigte anfangs große Größenunterschiede, die sich bei genauerem Hinsehen
durchgängig als eine von zwei harmlosen Ursachen herausstellten: (a) reiner
Formatierungsunterschied — `index_claude.html` ist im für Claude typischen,
mehrzeiligen JSX-Stil geschrieben, das aktuelle `data/index.html` im dichten
Einzeiler-Stil des restlichen Projekts (ca. 1,75× Zeilendichte); oder (b)
`index_claude.html` ist tatsächlich ein reiner Design-Prototyp ohne
Funktions-Anbindung, während das aktuelle Frontend dieselbe Optik voll
funktional ans Backend anbindet (Beispiel `WifiPanel`: Prototyp-Buttons ohne
`onClick`, aktuelle Version mit echtem `/set_wifi`, `/upload_gui`-OTA,
Toast-Feedback). Keine Funktion identifiziert, die im aktuellen Frontend
gegenüber `index_claude.html` verlorengegangen wäre — kein Handlungsbedarf.

---

## 2026-08-15 (Fortsetzung 2) — PlatformIO-Setup, Fix verifiziert

User bat darum herauszufinden, wie das Sketch selbst (per PlatformIO)
kompiliert werden kann, statt nur auf manuelles Gegenkompilieren in der
Arduino IDE zu verweisen. Umgebung hatte bereits eine funktionierende
`espressif32`-Platform (pioarduino-Fork, Version 55.3.311) samt
Xtensa-Toolchain im PlatformIO-Cache liegen (Herkunft: frühere Session,
Ordner `_disabled_espressif32_by_claude`); der RISC-V-Toolchain
(`toolchain-riscv32-esp`, für den C3 nötig) wurde beim ersten Build
automatisch nachgeladen.

**Board-ID gefunden:** `nologo_esp32c3_super_mini` — PlatformIO hat ein
exaktes Profil für den "Nologo ESP32C3 SuperMini" (Vendor-Bezeichnung der
Zielhardware).

**`platformio.ini` im Repo-Root angelegt** (`src_dir = .`, Board
`nologo_esp32c3_super_mini`, `framework = arduino`, `lib_deps =
rstephan/ArtnetWifi`). Erlaubt `pio run` direkt im Projektordner, ohne die
`.ino`/`.h`-Dateien zu verschieben — das Sketch bleibt für die Arduino IDE
unverändert nutzbar.

**Ergebnis:** `pio run` läuft **erfolgreich durch** — der am selben Tag
(erster Eintrag oben) angewendete Fix (`StepFX colFX, sgobFX, rgobFX;`)
ist damit als korrekt verifiziert, nicht nur durch Quellenvergleich. Zwei
Nebenbefunde aus dem Build-Log, beide nach `backlog.md` übernommen:
- Flash-Auslastung 90,3 % (1.183.197 / 1.310.720 Bytes) — wenig Puffer für
  künftige Features.
- Compiler-Warnung: `driver/i2s.h` (Legacy-I2S-API in `Audio_Engine.h`) ist
  laut ESP-IDF deprecated, zugunsten von `driver/i2s_std.h`. Nur Warnung,
  kein Fehler — Migration vor künftigem Toolchain-Sprung vormerken.

---

## 2026-08-15 (Fortsetzung 3) — `/code-review max`, 15 Findings, 14 gefixt

User bat um `/code-review` mit `ultrathink` (max. Reasoning-Effort). Da
dieses Repo kein Git hat, lief der Review gegen den kompletten aktuellen
Code-Stand (alle vier Backend-Dateien vollständig gelesen, zwei parallele
Hintergrund-Reviewer für Backend-Pitfalls bzw. Frontend/Routen-Abgleich,
danach persönliche Verifikation jedes Frontend-Fundes gegen die zitierten
Zeilen, abschließend ein Gap-Sweep über `Audio_Engine.h` und ungelesene
Frontend-Bereiche). Ergebnis: 15 Findings, grob nach Schwere sortiert.

Mehrere Findings bestätigten die bereits in `backlog.md` dokumentierten
Regressionen (`syncBeats[]`, Slot-Bounds) mit vollständiger Liste aller
Fundstellen. Mehrere andere waren **neu und bis dahin unbekannt**:
`/save_patch`-Buffer-Overflow, StepFX-Map-OOB-Read bei UI-gesetztem
`endVal`, `dimSmoothVal`-NaN-Pfad, sowie drei Frontend-Bugs (Movement-FX
Mode/Curve syncen nie, Farb-Chaser-Dropdown im falschen Scope, Fixture-Count
springt auf 1 zurück).

**User-Reaktion:** Sorge geäußert, dass Fixes das Projekt "kaputt fixen"
könnten — explizit um eine sichere Vorgehensweise gebeten, dann grünes
Licht gegeben. Vorgehen daraufhin bewusst konservativ gewählt: viele
kleine, chirurgische Einzel-Edits statt große Refactors, nach jedem
größeren Batch `pio run` zur Kompilier-Verifikation, kein Anfassen von Code
ohne ihn vorher frisch zu lesen (Zeilennummern aus dem Review-Report waren
teils schon durch vorherige Session-Edits verschoben).

**Gefixt (14 von 15 Findings), alle per `pio run` gegenkompiliert:**
- `/save_patch`: Fixture-Anzahl `n` auf 1–8 geklammert (Buffer-Overflow-Fix).
- `syncBeats[]`-Clamping an **allen** rohen Zugriffsstellen (Lesepfad in
  `FX_Engine.h`/`Moving_Head_Horizon.ino`) *und* an allen Setzstellen in
  `WebAPI.h` (`/fx`, `/modfx`, `/colfx`, `/sgobfx`, `/rgobfx`, `/chaser`,
  `/chaser_cfg`) — diesmal umfassender als der Juni-Fix.
- Slot-Bounds-Guards erneut in `executePreset`/`executeChaserSlot`/`/save`.
- `runStep` bekam einen `mapLen`-Parameter und klammert `currentIdx` sowie
  `startVal`/`endVal` gegen die echte Array-Größe (statt nur gegen das
  UI-gesetzte `endVal`); zusätzlich klammern `/colfx`/`/sgobfx`/`/rgobfx`
  `st`/`en` schon am Eingang.
- `/smooth` und der NVS-Load in `setup()` klammern `dimSmoothVal` auf
  0–100 (NaN/Infinity-Fix).
- `/hwaudio` klammert `sens` auf 0–100 (Fix gegen negative Beat-Schwelle).
- `/joy_cfg` vertauscht `min`/`max`, falls invertiert übergeben.
- `/modfx` validiert `pfx` jetzt explizit statt still auf `pRotFX`
  zurückzufallen.
- `executePreset` setzt jetzt `lastStepTime` für `colFX`/`sgobFX`/`rgobFX`
  zurück (Konsistenz mit dem Chaser-Recall-Pfad).
- Frontend: `fxMM`-Typo → `fxMode` korrigiert, `fxCurve`-Sync ergänzt.
- Frontend: `ChaserFx`s Farb-Dropdown auf ein neues, scope-korrektes,
  1:1-zu-`wheelMap[20]` passendes `COLOR_STEPS` umgestellt (statt des
  falsch-scoped 10-Einträge-`COLORS`).
- Frontend: fehlerhafte `setFixtureCount(1)`-Zeile im Poll-Handler entfernt.
- Frontend: Followspot-„Color"-Dropdown auf `colorBase`/`colorOff`
  umgestellt (vorher wirkungsloses `state.color`); dabei auffällig, dass
  `COLORS` im Followspot-Scope (eigenes `<script>`-Babel-Block) gar nicht
  sichtbar war — jeder `<script type="text/babel">`-Block ist sein eigener
  Scope. Eigene, lokal deklarierte `FOLLOWSPOT_COLORS`-Liste angelegt statt
  eines kaputten Cross-Scope-Zugriffs (der sonst selbst zum Bug geworden
  wäre).
- Alle verbliebenen deutschen Kommentare (`FX_Engine.h`, `Audio_Engine.h`,
  `data/index.html`) ins Englische übersetzt (CLAUDE.md-Regel).

**Bewusst nicht gefixt:** Die dreifache Duplizierung der FX-Engine-
Feldkopien (`executePreset`/`triggerSceneFX`/Chaser-Inline-Fade) — das
strukturelle 15. Finding. Einordnung: echtes Refactoring mit höherem
Risiko als die übrigen, punktuell fixbaren Findings; genau dieses Muster
ist vermutlich die Ursache dafür, dass die Juni-Fixes beim Merge nur in
einer Kopie überlebt haben. Bleibt offen in `backlog.md`, vorgeschlagen als
eigene, isolierte Session.

**Verifikation:** `pio run` nach jedem Backend-Batch und einmal final —
jedes Mal `[SUCCESS]`, Flash-Auslastung stieg minimal von 90,3 % auf
90,6 % (zusätzliche Validierungs-/Clamp-Logik). Wie immer bei `pio run`:
Kompilier-Check, **kein** Flash-/Hardware-Test — echte Verifikation auf dem
Gerät steht noch aus.

---

## 2026-08-15 (Fortsetzung 4) — FX-Engine-Feldkopien dedupliziert (`executePreset`)

User äußerte erneut Sorge vor "kaputt fixen" bei dem einen bewusst
zurückgestellten Finding aus dem `/code-review`-Durchlauf (Duplikation der
FX-Engine-Feldkopien) und bat auf Deutsch um eine Erklärung des geplanten
Vorgehens, *bevor* etwas geändert wird — dann grünes Licht ("ja mit
ultrathink").

**Beim erneuten Nachlesen des Codes korrigierte sich der eigene
Review-Befund von vorhin:** Es waren nicht drei duplizierte Stellen,
sondern nur zwei. `executeChaserSlot()` und der Chaser-Fade-Ende-Block in
`updateEngines()` riefen beide schon `triggerSceneFX(slot)` auf (0-basiert,
liest direkt aus `chaserScenes[]`) — nur `executePreset()` hatte eine
eigene, unabhängige ~28-Zeilen-Parallel-Implementierung derselben
Feldzuweisungen, die stattdessen aus einer lokalen `SceneData sd`-Kopie
las. Erst dieser Doppel-Check machte den eigentlichen Fix trivial und
risikoarm: keine neue Abstraktion einführen, sondern `executePreset()`
einfach `triggerSceneFX(slot - 1)` aufrufen lassen — Wiederverwendung
von Code, der über die beiden anderen Pfade schon produktiv läuft, statt
neuer, ungetesteter Logik.

**Umgesetzt:** In `executePreset()` (Moving_Head_Horizon.ino) die
komplette FX-Feld-Kopie inkl. Start/Stop, `colFX.step`-Berechnung und
`currentIdx`/`lastStepTime`-Reset (~28 Zeilen) entfernt und durch einen
einzigen Aufruf `triggerSceneFX(slot - 1);` ersetzt. Alles preset-
spezifische (DMX-Kanal-Kopie, Pan/Tilt-Rekonstruktion, Joystick-Smoothing-
Reset) bleibt unverändert in `executePreset()`. Dokumentierte, folgenlose
Verhaltens-Nuance: der `currentIdx`/`lastStepTime`-Reset für inaktive
Color-/Gobo-StepFX passiert jetzt nur noch bedingt (`if (fx.active)`, wie
in `triggerSceneFX` seit je), vorher in `executePreset` unbedingt — ohne
Auswirkung, da `runStep()` inaktive StepFX komplett ignoriert.

**Verifikation:** `pio run` → `[SUCCESS]`, Flash-Nutzung sank minimal von
90,6 % auf 90,5 % (weniger Code). Alle drei Aufrufstellen von
`executePreset`/`executeChaserSlot`/Chaser-Fade-Ende per `grep` gegen-
geprüft, laufen jetzt konsistent über die eine `triggerSceneFX`-
Implementierung. Wie immer: Kompilier-Check, kein Hardware-Test.

---

## 2026-08-15 (Fortsetzung 5) — React/Babel lokal gebündelt, Stage-Map-Bild geprüft

Anlass war die vorherige exploratorische Frage nach Nutzen von "React
Code-Splitting + Vite". Dabei kam ein konkreter, bis dahin unbekannter
Fund ans Licht: `data/index.html` lädt React/ReactDOM/Babel per
`<script src="https://unpkg.com/...">` von einem CDN. Läuft der Controller
im WiFi-AP-Fallback (`Moving_Head_Ctrl`, typischer Live-Einsatz ohne
Venue-WLAN), hat das verbundene Gerät kein Internet — die UI bliebe leer.
User bestätigte, dass das relevant ist, und bat um den kleinen,
gezielten Fix statt der großen Vite-Migration, plus im selben Zug eine
Prüfung des Stage-Map-Bild-Speicherns/-Ladens (Follower-Modus,
Foto-Alignment).

### Teil 1: React/Babel lokal statt CDN

**Erste Hürde:** `@babel/standalone` (unminifiziert *und* minifiziert
`babel.min.js`) ist mit **2,4 MB** viel zu groß für die LittleFS-Partition
dieses Boards. Partitionstabelle des letzten Builds ausgelesen
(`.pio/build/supermini/partitions.bin`, per Python geparst): `spiffs`-
Partition (= LittleFS) hat **1.408 KB**. `data/index.html` allein belegt
schon ~129 KB davon. Unkomprimiert passt Babel also nicht annähernd rein.

**Lösung:** React (production, minified, 10,7 KB), ReactDOM (production,
minified, 132 KB) und Babel Standalone (minified, 2,4 MB) heruntergeladen,
gzip-komprimiert (`gzip -9`): 4,3 KB / 43 KB / 548 KB — macht zusammen mit
`index.html` ~720 KB von 1.408 KB, ~688 KB frei fürs Stage-Map-Bild u. Ä.
Dateien liegen jetzt unter `data/vendor/*.gz`. Drei neue Routen in
`WebAPI.h` (`/vendor/react.js`, `/vendor/react-dom.js`, `/vendor/babel.js`)
öffnen die jeweilige `.gz`-Datei von LittleFS, setzen explizit
`Content-Encoding: gzip` und streamen sie mit `application/javascript` —
bewusst explizit statt sich auf implizites `.gz`-Verhalten von
`serveStatic` zu verlassen (das ohnehin nicht generisch für einen ganzen
Ordner konfiguriert ist, siehe unten). `data/index.html`s CDN-`<script>`-
Tags auf `/vendor/react.js` usw. umgestellt.

**Wichtiger Nebenfund beim Umsetzen:** `setupAPI()` mountet bisher *nur*
`server.serveStatic("/", LittleFS, "/index.html")` — eine einzelne Datei
auf einen einzelnen Pfad, **kein** generisches Static-File-Serving für
einen ganzen LittleFS-Ordner. Neue Dateien unter `data/` werden also nicht
automatisch erreichbar; jede zusätzliche Datei braucht ihre eigene Route
(wie hier für `/vendor/*` gemacht).

**Trade-off, transparent gemacht:** React läuft jetzt im *production*- statt
*development*-Build. Der `development`-Build wäre allein bei ReactDOM schon
über 1 MB (passt gar nicht erst gzip-komprimiert bequem rein) und liefert
ausführlichere Warnungen in der Browser-Konsole (z. B. fehlende `key`-Props)
— die fallen jetzt weg. Kein Blocker, aber macht künftiges Frontend-Debugging
in der Browser-Konsole etwas stiller.

**Verifikation — zwei Ebenen:**
1. `pio run` (Firmware) → `[SUCCESS]`, Flash minimal auf 90,7 % gestiegen
   (die drei neuen Routen).
2. `pio run -t buildfs` (LittleFS-Image aus `data/`) → `[SUCCESS]`. Um
   sicherzustellen, dass dieser Schritt Overflow überhaupt erkennt und
   nicht still abschneidet, wurde eine Gegenprobe gemacht: eine
   künstliche 800-KB-Testdatei in `data/vendor/` gelegt → `buildfs`
   schlägt zuverlässig fehl (`LFS_ERR_NOSPC`, `[FAILED]`). Testdatei
   wieder entfernt, danach erneut `[SUCCESS]`. Damit ist die reale
   Passform in die Partition bestätigt, nicht nur überschlagen gerechnet.

### Teil 2: Stage-Map-Bild (Speichern/Laden/Alignment) geprüft

Kompletten Pfad nachvollzogen: Upload (`<input type="file">` →
`handleUpload`) → `FileReader.readAsDataURL` → Canvas-Resize auf max.
250 px Breite → `canvas.toDataURL('image/jpeg', 0.3)` → POST `/save_map`
mit `{points, image}` als JSON-Body → Backend schreibt den rohen Body 1:1
als `/map.json` auf LittleFS (`server.hasArg("plain")`) → beim nächsten
Laden liest `/load_map` dieselbe Datei zurück, Frontend übernimmt
`json.image`/`json.points` in den State. **Technisch korrekt, keine
Diskrepanz zwischen Senden/Speichern/Lesen gefunden.**

Realistische Bildgröße nachgemessen (Python/Pillow, gleiche Parameter
250 px/Q30): künstliches Testbild ergab ~2,4 KB roh / ~3,2 KB Base64 —
selbst ein detailreiches echtes Bühnenfoto bliebe im niedrigen bis
mittleren zehn-KB-Bereich. Unkritisch sowohl für die ~688 KB freie
LittleFS-Partition (nach dem Vendor-Update) als auch für den RAM-Puffer,
den der ESP32-`WebServer` beim Empfangen des POST-Bodies braucht (im
Framework-Quellcode nachgesehen: `HTTP_MAX_POST_WAIT` ist ein 5s-Timeout,
kein Größenlimit — begrenzt effektiv nur durch verfügbaren Heap).

Die bilineare Interpolation für Tap-to-Move (`handleMapTap`, App-Root-
Komponente) von Hand nachgerechnet: `mapPoints`-Reihenfolge ist
TL(0)/TR(1)/BR(2)/BL(3) (bestätigt durch das Kalibrierungs-UI-Label
`['Top-Left','Top-Right','Bottom-Right','Bottom-Left']`), und die
Interpolationsformel (obere Kante TL→TR, untere Kante BL→BR, dann
zwischen beiden mit `ny` interpoliert) ist dazu konsistent — mathematisch
korrektes Standard-Bilinear-Patch. Eingeordnet als bewusste Vereinfachung
(keine perspektivische Homographie), nicht als Bug.

**Drei kleine, neue Robustheits-/UX-Punkte gefunden** (kein Fix, nur
dokumentiert in `backlog.md`, da nicht als "kaputt" angefragt): `/save_map`
gibt bei Schreibfehlern trotzdem `200 OK` zurück und der initiale
Foto-Upload hat gar kein `.catch()`; der geschriebene Byte-Count von
`f.print()` wird nicht geprüft (bei aktueller Bildgröße praktisch nie
relevant); und "Replace photo" übernimmt die alten Kalibrierpunkt-
Positionen unverändert, obwohl sie sich aufs alte Foto beziehen.

**Wie immer:** alles hier ist Code-Lese-Verifikation + `pio run`/
`pio run -t buildfs`-Kompilier-Checks, kein Test auf echter Hardware im
Browser.

---

## 2026-08-15 (Fortsetzung 6) — die drei Stage-Map-Robustheitspunkte gefixt

User bat direkt darum, die drei zuvor nur dokumentierten (nicht gefixten)
Punkte aus dem Stage-Map-Check jetzt zu beheben.

**`WebAPI.h`, `/save_map`:** Von einem Einzeiler ohne jede Fehlerbehandlung
auf einen Handler umgestellt, der (1) einen fehlenden Body mit `400`
quittiert, (2) einen fehlgeschlagenen `LittleFS.open()` mit `500`
quittiert statt stillschweigend nichts zu tun, und (3) den Rückgabewert von
`f.print()` (tatsächlich geschriebene Bytes) gegen die erwartete Body-Länge
prüft — bei Mismatch wird die unvollständige `/map.json` per
`LittleFS.remove()` wieder gelöscht (verhindert eine korrupte Datei, die
beim nächsten `/load_map` nur still fehlschlagen würde) und ebenfalls `500`
gesendet.

**`data/index.html`:** Beide Stellen, die `/save_map` aufrufen, wurden auf
den neuen Statuscode reagierend gemacht:
- Der initiale Foto-Upload (`handleUpload`) hatte bisher überhaupt kein
  `.then()`/`.catch()` am `fetch(...)` — jetzt prüft `.then((r) => { if
  (!r.ok) throw ...; showToast(...) })` explizit `r.ok` (ein `fetch()`
  wirft nur bei Netzwerkfehlern, nicht bei HTTP-4xx/5xx — das musste also
  explizit geprüft werden, sonst hätte selbst ein `500` als Erfolg
  angezeigt), mit `.catch()` für den Fehlerfall.
- Der "SAVE POINT"-Button hatte schon `.then()/.catch()`, aber ohne
  `r.ok`-Check — hätte bei einem serverseitigen Fehler trotzdem "✓ Point
  saved" angezeigt. Gleiche Korrektur angewendet.

**"Replace photo" behält alte Kalibrierpunkte:** `nextPoints` in
`handleUpload` fiel vorher auf `s.mapPoints || [...]` zurück — da
`mapPoints` im State-Default aber nie `undefined` ist, griff der
Fallback nie, jedes neue Foto übernahm also immer die *alten*
Eckpositionen. Geändert auf: `nextPoints` ist bei jedem neuen Upload
immer der feste Default-Punktesatz (4 Ecken bei 10 %/90 %, Pan/Tilt auf
Center). Nebeneffekt der Änderung: da `nextPoints` jetzt nicht mehr vom
vorherigen State abhängt, konnte der `fetch()`-Aufruf aus dem
`setState`-Updater-Callback herausgezogen werden (war vorher ein
Seiteneffekt in einer State-Updater-Funktion) — kleine, durch den Fix
selbst bedingte Vereinfachung, kein zusätzlicher Umbau. Erfolgs-Toast
weist den Nutzer jetzt explizit auf die nötige Neukalibrierung hin
("✓ Photo saved — recalibrate the corners").

**Verifikation:** `pio run` (Firmware, wegen `WebAPI.h`-Änderung) →
`[SUCCESS]`, Flash minimal auf 90,7 % (kaum verändert, nur etwas mehr
Fehlerbehandlungs-Code). `pio run -t buildfs` (LittleFS-Image, wegen
`data/index.html`-Änderung) → `[SUCCESS]`, passt weiterhin in die
Partition. Wie immer: Kompilier-/Größen-Check, kein Hardware-/Browser-Test.

---

## 2026-08-15 (Fortsetzung 7) — `/code-review` (Vollcodebase), 9 neue Findings

User rief `/code-review` per lokalem Kommando auf (kein Level angegeben).
Skill blockierte zunächst: dieses Verzeichnis hat kein Git — die Skill ist
für Diff-Reviews gebaut, nicht für einen Scan des kompletten Stands. Agent
bot drei Optionen an; per `SendMessage` an den laufenden Hintergrund-Agent
angewiesen, Option 2 zu nehmen (Vollcodebase-Review), analog zum
`/code-review max`-Durchlauf vom selben Tag (erster Eintrag oben), diesmal
gegen den Stand nach allen seitherigen Fixes (Validierung, Slot-Bounds,
syncBeats-Clamping, `executePreset`/`triggerSceneFX`-Dedup, lokales
Vendor-Bundling, Stage-Map-Fehlerbehandlung).

**9 Findings, alle vom Review-Agenten selbst als CONFIRMED verifiziert.**
Vier davon zusätzlich von mir selbst gegengeprüft (Code frisch gelesen,
nicht nur dem Report vertraut) — alle vier hielten stand:

1. **Blackout-Panic-Button fadet statt zu schneiden.** `onBlackout` nutzt
   `/set_all?c1=0` (läuft durch `dimSmoothTarget`/Smoothing), statt den
   bereits vorhandenen Instant-Pfad `bumpBlackout` (über `/bump`) zu
   nutzen, den die anderen Panic-Buttons (Blinder, Strobe) verwenden.
   Selbst nachvollzogen: `/bump?t=blackout` existiert im Backend
   (`WebAPI.h`) und wirkt ungeklammert in der finalen DMX-Komposition —
   wird von diesem Button schlicht nicht aufgerufen.
2. **„SYSTEM RESET"-Dialogtext irreführend.** Verspricht „clear all
   presets and settings", `/set_wifi?s=&p=` löscht aber nur WLAN-
   Zugangsdaten. Selbst nachvollzogen (`grep` auf beide Stellen).
3. **`triggerSceneFX`/`/save`: ungeklammerter, NVS-geladener Wheel-Index.**
   Lücke im selben Härtungs-Kontext wie die StepFX-Bounds-Fixes vom
   selben Tag — deckt den Pfad „NVS → `triggerSceneFX` → `/save`" nicht
   ab. Selbst nachvollzogen.
4. **Chaser bleibt hängen bei Start-Slot > End-Slot.** Weder `/chaser`
   noch `/chaser_cfg` validiert die Reihenfolge; `nextSlot`-Vorschub-Logik
   springt dann jeden Zyklus sofort zurück. Selbst nachvollzogen
   (`nextSlot++`/`if (nextSlot > chaserEndSlot) nextSlot = chaserStartSlot`
   in `Moving_Head_Horizon.ino`).
5. `/joy_cfg` klammert Pan/Tilt-Limits nicht (nur Vertauschen bei
   Inversion, kein Wertebereich-Check).
6. `/save_patch` klammert die Fixture-DMX-Adresse nicht (nur durch
   Downstream-Guard in `updateEngines()` ungefährlich).
7. `/chaser_cfg` ist toter Code, keine Frontend-Referenz mehr.
8. `colFX.step`-Paritätsberechnung doppelt (`triggerSceneFX` + `/colfx`-
   Handler) — dieselbe Duplikations-Klasse wie die frühere
   `syncBeats`-Regression.
9. `/load_map` sendet ungültigen Content-Type (`"json"` statt
   `"application/json"`) im Leer-Fallback — aktuell folgenlos.

**Noch nicht gefixt** — nur dokumentiert (in `backlog.md` unter Tech Debt
eingetragen), auf Rückmeldung/Priorisierung wartend, wie bei den bisherigen
Review-Runden auch schon gehandhabt.

---

## 2026-08-15 (Fortsetzung 8) — alle 9 Findings gefixt, plus Chaser-Persistierungs-Bug entdeckt

User fragte zuerst kritisch nach, bevor er grünes Licht gab: ist der
„Blackout"-Button überhaupt als harter Notaus gedacht, oder ist ein
sanftes Fade beim Ausblenden der Stimmung erwünscht? Am Code beantwortet
statt geraten: Der Button-Wrapper heißt `className="panic-row"`, und
direkt daneben in derselben Zeile sitzt bereits ein separater „Fade
Out"/„Fade In"-Button, der an `/autofade`+`/unmute` hängt (mit
einstellbarer `fadeTime`/`fadeCurve`) — das ist eindeutig der bewusste,
sanfte Weg. „Blackout" ist als zweite, eigenständige Aktion daneben
gedacht. Damit bestätigt: Panic-Button, sollte hart schneiden. User gab
daraufhin grünes Licht für alle 9 Findings.

**1. Blackout-Panic-Button** (`data/index.html`, `handleBlackoutOn`/
`handleBlackoutOff`): von `/set_all?c1=0` (gesmoothter Pfad) auf
`/bump?t=blackout&s=1`/`s=0` umgestellt — denselben Instant-Override-
Mechanismus, den Blinder/Strobe schon nutzen. Nebeneffekt: der
`blackoutPrevDimmer`-Ref und die `dimmer`-Zustandsmanipulation wurden
überflüssig und entfernt, da `bumpBlackout` nur die DMX-Ausgabe überlagert,
ohne den eigentlichen Dimmer-Wert zu verändern — beim Ausschalten muss
nichts wiederhergestellt werden.

**2. „SYSTEM RESET"-Text** (`data/index.html`): Button und
Bestätigungsdialog umbenannt/umformuliert („RESET WIFI", ehrlicher Text:
„Presets, chaser scenes and fixture patch are kept"), statt die
Funktionalität destruktiv zu erweitern, damit sie das ursprüngliche
Versprechen einlöst. Bewusste Entscheidung für die risikoärmere Variante —
eine echte Factory-Reset-Funktion (die tatsächlich alle NVS-Namespaces
löscht) wäre eine neue, destruktive Funktion gewesen, die ohne expliziten
Wunsch nicht eigenmächtig eingeführt werden sollte.

**3. `triggerSceneFX`/`/save` wheelMap-Index** (`Moving_Head_Horizon.ino`,
`WebAPI.h`): `colFX`/`sgobFX`/`rgobFX` `startVal`/`endVal` werden jetzt
auch beim Laden aus `chaserScenes[slot]` in `triggerSceneFX` geklammert
(0–19/0–9/0–6, dieselben Grenzen wie an den `/colfx` etc. HTTP-Einstiegen).
Zusätzlich klammert `/save` jetzt auch den direkten
`wheelMap[colFX.currentIdx]`-Zugriff defensiv — zwei Verteidigungsschichten
statt einer Lücke.

**4. `/joy_cfg`** (`WebAPI.h`): `pmin`/`pmax`/`tmin`/`tmax` werden jetzt vor
dem `<<8`-Shift auf 0–255 geklammert (vorher nur bei Inversion vertauscht,
nie auf einen gültigen Bereich geprüft).

**5. Chaser Start>End-Slot** (`WebAPI.h`, `/chaser`): `chaserStartSlot`/
`chaserEndSlot` werden jetzt vertauscht, falls invertiert übergeben —
gleiches Muster wie bei den Pan/Tilt-Limits.

**6. `/save_patch`** (`WebAPI.h`): Fixture-DMX-Adresse wird jetzt auf
1–495 geklammert (495 = 512 − 17, lässt Platz für die 18 Kanäle der
Fixture innerhalb des 512-Kanal-Universums).

**7. `/chaser_cfg` entfernt — mit wichtigem Zusatzfund unterwegs:** Beim
Entfernen des toten Handlers fiel auf, dass er nicht nur unreferenzierter
Code war, sondern auch der **einzige** Pfad, der Chaser-Konfiguration
(`fadeTime`, `holdTime`, `chaserTrigger`, `chaserSync`, `chaserOrder`,
Fade-Trigger/-Sync) in NVS (`prefs`, Namespace `"sys"`) persistierte. Der
tatsächlich vom Frontend genutzte `/chaser`-Endpunkt setzte dieselben
globalen Variablen, hatte aber **kein** `prefs.begin/putInt/end` —
Chaser-Einstellungen haben also nie einen Neustart überlebt, seit
`/chaser_cfg` (aus welchem Grund auch immer) aufhörte, vom Frontend
aufgerufen zu werden. Das stand nicht im ursprünglichen Review-Finding
(der nur „toter Code" sah, nicht die Persistierungs-Konsequenz). Fix:
die komplette `prefs`-Persistierungslogik aus `/chaser_cfg` nach `/chaser`
verschoben, dann erst den jetzt wirklich redundanten `/chaser_cfg`-Handler
gelöscht.

**8. `colFX.step`-Duplikation** (`Moving_Head_Horizon.ino`): neue
Hilfsfunktion `updateColFXStep()` (direkt nach `loadAllChaserScenes()`,
vor `triggerSceneFX`, damit sie auch für das später inkludierte
`WebAPI.h` sichtbar ist) — ersetzt die Paritätsberechnung in
`triggerSceneFX` und im `/colfx`-Handler.

**9. `/load_map` Content-Type** (`WebAPI.h`): `"json"` → `"application/json"`
im Leer-Fallback.

**Verifikation:** `pio run` (Firmware) und `pio run -t buildfs`
(LittleFS-Image) → beide `[SUCCESS]`. Flash-Nutzung minimal von 90,7 % auf
90,6 % gesunken (der entfernte tote `/chaser_cfg`-Handler überwog die
neuen Guards). Vollständige Referenzsuche (`grep`) bestätigt: keine Reste
von `chaser_cfg` oder `blackoutPrevDimmer` im Code. Wie immer: Kompilier-/
Größen-Check, kein Hardware-/Browser-Test — insbesondere der Blackout-Fix
und die Chaser-Persistierung sollten auf echter Hardware verifiziert
werden, sobald möglich.

---

## 2026-08-15 (Fortsetzung 9) — Projekt nach GitHub gepusht (`future`-Branch), Repo aufgeräumt

User bat darum, das Projekt auf GitHub zu bringen
(`github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller`,
Branch `future`) — vorher dort `V1`/`V2`/`V3`/`firmware` löschen,
`images` behalten, dann den aktuellen Code pushen.

**Vor dem Löschen erst den bestehenden Stand angesehen** (Klon in
Scratch-Verzeichnis, nicht blind gelöscht): `V1`/`V2`/`V3` enthalten genau
die historischen Vorläufer-Codestände dieses Projekts — `V3` insbesondere
ist nahezu identisch mit dem, was hier als „horizon_light_controller"-
Backend bzw. Frontend-Quelle bekannt war (`V3/Moving_Head.ino`,
`V3/WebAPI.h`, `V3/FX_Engine.h`, `V3/Readme.md`, `V3/data/index.html` —
dieselbe Codebasis, ein Stück früher in der Kette). `firmware/` enthielt
vorkompilierte Binaries (`firmware.bin`, `bootloader.bin`, `littlefs.bin`,
`partitions.bin`, `manifest.json`) für den One-Click-Web-Installer, den
`install.html` referenziert.

**Wichtiger Fund dabei:** Das bestehende `README.md` (Großschreibung!) war
kein Wegwerf-Dokument, sondern ein reichhaltiges, größtenteils weiterhin
zutreffendes Projekt-Readme (UI-Tab-Beschreibungen, Mermaid-Architektur-
Diagramm, vollständige Channel-Map) — inhaltlich fast deckungsgleich mit
dem aktuellen Code (LiveTab/FollowspotTab/ProgrammerTab, CH1–CH18-Mapping
stimmen exakt). Wurde **nicht** durch das eigene, schlankere `Readme.md`
ersetzt, sondern gezielt aktualisiert: die veraltete Behauptung „Vanilla
JS, keine Frameworks" korrigiert (ist jetzt React), die Bewegungsformen-
Zahl 7→12 korrigiert (die restlichen 5 waren in den V2-Notes bereits
erwähnt, nur der Summary-Bullet war nicht nachgezogen worden), ein Hinweis
auf die jetzt kaputte One-Click-Installer-Verknüpfung ergänzt (da
`firmware/manifest.json` mitgelöscht wurde), sowie neue Abschnitte „Building
From Source", „Known Issues" und „Documentation" (Verweis auf
`doc/content/` und `CLAUDE.md`) angehängt. Das eigene `Readme.md` wurde
gelöscht (Namenskollision mit `README.md` auf dem case-insensitiven
macOS-Dateisystem) — seine eigenständigen Inhalte sind jetzt im
bestehenden `README.md` aufgegangen.

**Mechanik:** `Moving_Head_Horizon`-Verzeichnis selbst zum Git-Repo gemacht
(`git init`, `origin` via SSH — ein extra für diese Session eingerichteter
Deploy-Key, authentifiziert als `doctormord`, war bereits vorhanden),
`future`-Branch gefetcht und ausgecheckt (lief konfliktfrei, da keine
Dateiname-Überschneidung mit den eigenen, noch ungetrackten Projektdateien
— bis auf das vorher entfernte `Readme.md`/`README.md`-Paar). `git rm -r
V1 V2 V3 firmware`, `.gitignore` für `.pio/`/`.DS_Store` ergänzt, eigene
Dateien gezielt (keine `-A`) gestaged. Git erkannte mehrere Datei-
Umbenennungen automatisch (z. B. `V3/Moving_Head.ino` →
`Moving_Head_Horizon.ino`), was in der Historie sauber als Rename statt
Delete+Add erscheint. Ein Commit, normaler (nicht erzwungener) Push —
lief als Fast-Forward durch, da lokaler und Remote-Stand vorher identisch
waren. `images/`, `install.html`, `LICENSE` komplett unangetastet gelassen.

**Nicht behoben, nur dokumentiert:** Der One-Click-Installer
(`install.html` → `firmware/manifest.json`) ist jetzt kaputt, da
`firmware/` gelöscht wurde. Kein neuer `firmware/`-Ordner mit frisch
gebauten Binaries wurde erzeugt — das ginge über die Anfrage hinaus
(Quellcode pushen, keine Binary-Artefakte), im README aber transparent
vermerkt.

Push verifiziert: `git ls-remote` gegen den Remote-Branch zeigt denselben
Commit-Hash wie der lokale `HEAD`.

---

## 2026-08-16 — `/ultrareview` gestartet, teilweise an Session-Limit gescheitert, 15 von 18 Findings gefixt

`/ultrareview` (= `/code-review ultra`) angestoßen, nachdem das lokale
Verzeichnis zum Git-Repo wurde. Erster Lauf griff mit der Standard-
Heuristik daneben: da `origin/future` und die Arbeitskopie exakt
übereinstimmten (gerade erst gepusht), fand der Agent keinen sinnvollen
Diff und fiel auf `HEAD~1...HEAD` zurück — reviewte nur das letzte,
reine Doku-Commit. Per `SendMessage` an den laufenden Agenten
klargestellt: der eigentlich relevante Vergleich ist `origin/main` gegen
`origin/future` (der alte, veröffentlichte Stand gegen den gerade
gepushten, konsolidierten Code — entspricht genau dem, was eine PR
future→main zeigen würde).

**Der neu gestartete Lauf lief in mehrere parallele Teil-Agenten
("Angles") auf, von denen mehrere an einem Account-Session-Limit
scheiterten** (`You've hit your session limit · resets 3:10am
(Europe/Berlin)`) — betroffen: der Hauptorchestrator selbst sowie die
Teil-Agenten „line-by-line diff scan", „removed-behavior auditor",
„cross-file tracer" und „wrapper/proxy correctness". Die eigentliche
Synthese-/Verifikations-Stufe (die normalerweise Rohfunde dedupliziert
und gegen den Code verifiziert) hat dadurch nie stattgefunden.

**Drei Teil-Agenten liefen trotzdem komplett durch** und lieferten rohe,
unsynthetisierte Fundlisten: „Efficiency" (8 Funde), „Altitude/Bandaid-
Fixes" (7 Funde), „Language-Pitfalls" (7 Funde) — macht 22 Rohfunde. Da
kein Synthese-Schritt mehr lief, wurden diese nicht blind übernommen:
7 der wichtigsten/schwerwiegendsten Funde wurden selbst per `grep`/`Read`
direkt am Code nachverifiziert (nicht nur dem Agenten-Report vertraut),
bevor sie gemeldet wurden. Ein bereits im Backlog dokumentierter Fund
(Rotationsmatrix pro Fixture neu berechnet) wurde erkannt und nicht
dupliziert gemeldet. 18 der 22 Rohfunde (bereinigt, ohne die Dopplung)
wurden final gemeldet.

**User bestätigte „ja dann mach das mal" — 15 von 18 gefixt, 3 bewusst
zurückgestellt** (Details siehe `backlog.md` → „Kürzlich gefixt" bzw.
„Tech Debt" für die zurückgestellten). Sechs echte, selbst verifizierte
Bugs behoben: `/autofade`-NaN-Risiko (Leaf-Guard + Boundary-Clamp),
`updateEngines()`s fehlender oberer `dt`-Clamp, `/set_all`s fehlender
Kanalwert-Clamp, `beatInterval`-Division ohne Zero-Guard, doppelte Magic-
Channel-Numbers 13/14 (jetzt `CH_FOCUS`/`CH_ZOOM`), und der Frontend-
Falsy-Zero-Bug bei `presetActive`. Dazu neun weitere Duplikations-/
Effizienz-Punkte (siehe `backlog.md` für die volle Liste) — u. a.
`executePreset`/`/save`/`/set_all` durchgängig auf `CH_*`-Konstanten
umgestellt (drei Stellen hatten bisher rohe Pan/Tilt-Kanalliterale, wo
`executeChaserSlot` bereits Konstanten nutzte), `/joy_cfg`s doppelte
Swap-Logik in ein Lambda gezogen und `spd`/`crv`/`mom` nachträglich
geklammert, Magic-Constant `183` benannt, redundante NVS-Reloads in
`/save` und `setup()` entfernt, doppelte Trig-Calls in drei Movement-
Shapes dedupliziert, `Audio_Engine.h`s Median-Neuberechnung auf
tatsächlich neue Samples beschränkt, und ein React-Antipattern (Fetch-
Seiteneffekt in einem `setState`-Updater) beim Stage-Map-„SAVE POINT"
behoben.

**Drei Funde bewusst nicht gefixt**, weil sie echte Restrukturierungen
mit höherem Risiko wären statt chirurgischer Änderungen — genau die
Kategorie, die in dieser Session konsequent zurückgestellt statt blind
durchgezogen wurde:
- `SceneData`-NVS-Format-Versionierung — ein Versions-Feld hinzuzufügen
  würde `sizeof(SceneData)` ändern und dadurch *alle aktuell gespeicherten
  Presets* beim nächsten Boot auf Defaults zurückfallen lassen (der
  Legacy-Fallback kennt nur das ganz alte Einzel-Key-Format). Ein echter
  Breaking Change für existierende Geräte — bräuchte eine durchdachte
  Migration, kein Schnellschuss.
- `/api/get_dmx`s JSON-Bau per `String +=` → `snprintf`/ArduinoJson.
- Die zwei unsynchronisierten Frontend-Polling-Loops zusammenlegen.

**Verifikation:** `pio run` und `pio run -t buildfs` → beide `[SUCCESS]`,
Flash-Nutzung minimal verändert. Wie immer: Kompilier-/Größen-Check, kein
Hardware-/Browser-Test. Die drei zurückgestellten Punkte sowie ein erneuter
`/ultrareview`-Lauf (nach Ablauf des Session-Limits um 3:10 Uhr, um die
ausgefallenen Teil-Agenten nachzuholen) bleiben offen für eine künftige
Session.

---

## 2026-08-16 (Fortsetzung) — `/ultrareview` mit korrektem Diff-Scope nachgeholt, 8 von 8 Findings gefixt

User bat darum, `/ultrareview` „jetzt nachzuholen". Erster Versuch griff
wieder daneben: `git diff @{upstream}...HEAD` und der Working-Tree-Diff
waren beide leer (Branch war komplett synchron mit `origin/future`), der
Agent fand also gar keinen Diff und beendete sich mit leerem Ergebnis,
ohne die vorher per `SendMessage` mitgegebene Scope-Anweisung
(`origin/main` vs. `origin/future`) verarbeitet zu haben — vermutlich war
die Nachricht noch nicht zugestellt, als der erste Tool-Round schon lief.
Per `SendMessage` an den (bereits „completed") Agenten resumed, diesmal
mit dem exakten Git-Befehl statt einer Beschreibung
(`git diff origin/main...origin/future`), um jede Interpretationslücke
auszuschließen.

**Zweiter Versuch lief vollständig durch** (53 Tool-Aufrufe, ~16 Minuten) und
lieferte 8 Findings, alle laut Agent verifiziert. Vor dem Melden zusätzlich
selbst am Code nachvollzogen (`grep`/`Read`), alle 8 bestätigt:

1. **4 der 6 FX-Panels im Programmer-Tab (Movement/Dimmer/Gobo-Rotation/
   Prisma-Rotation) an tote State-Keys gebunden.** `TriggerBlock`- und
   Slider-Props in `MovementFx`/`DimmerFx`/`RotationFx` lasen/schrieben
   Langform-Namen (`fxTrigger`, `fxSync`, `fxModSpeed`,
   `fxSpeedStart/End`, `fxSizeStart/End`, `dimTrigger/Sync/Speed`,
   `grTrigger/Sync/Speed`, `prTrigger/Sync/Speed`), die im gesamten
   restlichen Code nirgends vorkommen — der tatsächliche State (Init,
   `/api/get_dmx`-Poll-Sync, `/fx`+`/modfx`-Outbound-Sync) nutzt
   durchgängig Kurzform-Keys (`fxTr`, `fxSy`, `fxMS`, `fxSS`/`fxSE`,
   `fxZS`/`fxZE`, `dimTr`/`dimSy`/`dimSp`, `grTr`/`grSy`/`grSp`,
   `prTr`/`prSy`/`prSp`). Per `grep` bestätigt: jeder Langform-Key kommt
   exakt einmal vor (nur in seiner eigenen Komponente), `TriggerBlock`
   selbst macht kein internes Aliasing. Damit waren diese Regler in
   **beide Richtungen** komplett wirkungslos — Änderungen erreichten das
   Gerät nie, und die Anzeige zeigte nie den echten Geräte-Wert, weil der
   eingehende Poll ebenfalls die Kurzform-Keys schreibt. Nur der Farb-/
   Gobo-Chaser (`ChaserFx`) war korrekt verdrahtet. Vermutlich ein sehr
   alter Bug aus einer frühen Umbenennungs-Runde (lange bevor diese
   Session begann), der schlicht nie auffiel, weil die UI keinen Fehler
   zeigt — die Regler „funktionieren" ja optisch.
2. **`/chaser` restartete bei jeder Config-Änderung, nicht nur bei
   Ein/Aus.** Der Handler resettete `currentSlot`/`nextSlot` auf
   `chaserStartSlot` bei jedem `act=1`, ohne zu prüfen ob der Chaser
   schon lief — anders als `/fx`/`/modfx` im selben File, die einen
   `startFresh`-Guard haben. Das Frontend sendet `/chaser?act=...` aber
   bei *jeder* Änderung an Trigger/Sync/Order/Start/End/Fade/Hold/
   Fade-Trigger/Fade-Sync, nicht nur beim Umschalten — ein Hold-Time-
   Regler mitten im laufenden Chaser hätte die Sequenz auf Slot 0
   zurückgeworfen.
3. **Frontend `track()` verschluckte Kanal-Änderungen dauerhaft**, nicht
   nur verzögert, wenn sie ins 300ms-`isReceiving`-Fenster nach dem Poll
   fielen: Die Vergleichs-Baseline `p['ch'+ch]` wurde unbedingt
   aktualisiert, der eigentliche Versand aber nur bedingt (`if
   (!isReceiving.current)`) — beim nächsten Render fand der Diff-Check
   keinen Unterschied mehr (Baseline war ja schon auf dem neuen Wert) und
   der Wert wurde nie gesendet, obwohl die UI ihn korrekt anzeigte.
4. **`/colfx`/`/sgobfx`/`/rgobfx` fehlte derselbe Start>End-Swap-Guard**,
   den `/chaser` in der Vorrunde schon bekommen hatte — ohne den bleibt
   ein Farb-/Gobo-Chaser bei vertauschter Start/End-Auswahl auf dem
   Startwert eingefroren (`runStep()` springt sofort zurück, sobald
   `currentIdx` über `endVal` hinausgeht).
5. **Pan/Tilt-Live-Anzeige fror während aktivem Movement-FX ein.** Die
   `/api/get_dmx`-Poll-Auswertung las die 8-Bit-`dmxData[CH_PAN]`/
   `[CH_TILT]`-Bytes, die `updateEngines()` nur schreibt wenn
   `!moveFX.active` — der Backend-Response enthält aber längst die immer
   aktuellen, vollauflösenden `cp`/`ct`-Werte (`centerPan16`/
   `centerTilt16`), die das Frontend nie gelesen hat (`grep` bestätigt
   null Treffer für `d.cp`/`d.ct` vor diesem Fix).
6. **`/save` prüfte den NVS-Schreiberfolg nicht** — eine vom Ultrareview
   selbst aufgedeckte Regression aus der eigenen Persistierungs-
   Optimierung der Vorrunde (direktes In-Memory-Update statt
   `loadAllChaserScenes()`-Reload). Der alte Code hätte einen
   fehlgeschlagenen Schreibvorgang durch das erneute Laden von der Flash
   indirekt sichtbar gemacht, der neue, schnellere Code nicht.
7. **Dip-to-Black spielte bei ungültigem Slot trotzdem sinnlos ab.**
   `triggerLoad()` startete den Fade-to-black, bevor der Ziel-Slot
   validiert wurde — die (aus der vorletzten Runde stammenden)
   Bounds-Checks in `executePreset`/`executeChaserSlot` sorgen zwar dafür,
   dass am Ende nichts Falsches geladen wird, aber der komplette
   Fade-zu-Schwarz-und-zurück lief trotzdem sichtbar durch, für nichts.
8. Ein letzter übersehener deutscher Kommentar (`data/index.html:1395`,
   Geschwister-Kommentar 35 Zeilen weiter war schon übersetzt worden).

**Alle 8 gefixt.** Details der Einzel-Fixes in `backlog.md` → „Kürzlich
gefixt". Mit `pio run` und `pio run -t buildfs` verifiziert, beides
`[SUCCESS]`. Wie immer: Kompilier-/Größen-Check, kein Hardware-/
Browser-Test — insbesondere Finding 1 (FX-Panel-Verdrahtung) verdient
einen echten Hardware-/Browser-Test, sobald möglich, da es UI-Verhalten
betrifft, das sich per Compile-Check nicht beobachten lässt.

---

## 2026-08-17 — Erster echter Hardware-Test: geflasht, live geprüft, ein neuer Bug gefunden und gefixt

User: „ich schließe das device an und du machst das." Gerät via USB
verbunden, als `/dev/cu.usbmodem1101` erkannt (VID:PID `303A:1001` =
Espressif natives USB-JTAG/Serial, passend zum ESP32-C3 Supermini).

**Geflasht:** `pio run -t upload --upload-port /dev/cu.usbmodem1101`
(Firmware) und `pio run -t uploadfs --upload-port /dev/cu.usbmodem1101`
(LittleFS/`data/`) — beide `[SUCCESS]`, Hash-Verifikation bestanden.

**Boot-Log per pyserial mitgeschnitten** (DTR/RTS-Reset-Sequenz ausgelöst,
dann 15s Serial gelesen, da `pio device monitor` sich mit `timeout` nicht
sauber begrenzen ließ — macOS hat kein `timeout`/`gtimeout` vorinstalliert).
ROM-Bootlog sauber (`ESP-ROM:esp32c3-api1-20210207`). Fünf
`nvs_open failed: NOT_FOUND`-Zeilen von `Preferences.cpp` — zunächst
unklar, dann durch `/api/state` erklärt: exakt die 5 ungenutzten
Preset-Slots (`sc6`–`sc10`, nie gespeichert) schlagen beim
Read-Only-`prefs.begin()` fehl, die 5 echten Presets (sc1–sc5, u. a.
„Skybeams slow", „discomover BS") öffnen anstandslos — kein Bug, exakt
das erwartete Verhalten für teilweise befüllte Preset-Slots.

**Gerät bereits im Heimnetz** (nicht im AP-Fallback!): `ping
movinghead.local` beantwortet unter `192.168.8.113` — die WLAN-
Zugangsdaten waren schon vor dem Flash in der `"sys"`-NVS-Namespace
gespeichert und blieben unangetastet, da `pio run -t upload`/`uploadfs`
nur die App- bzw. LittleFS-Partition schreiben, nicht die separate
NVS-Partition. Damit war Live-Testen per `curl` gegen die echte API
möglich, ohne die WLAN-Verbindung des Testrechners anzufassen.

**Live-Verifikation (nur lesende Endpunkte, keine Schreibzugriffe auf
echte gespeicherte Show-Daten):** `/` liefert die React-UI (130.671 Bytes,
`200 OK`), `/api/state` und `/api/get_dmx` liefern plausible, konsistente
JSON-Antworten (reale Presets, 8 gepatchte Fixtures unter `/api/patch` mit
Adressen 1/19/36/53/70/87/104/121 — passend zum 18-Kanal-Profil).

**Dabei ein neuer, bis dahin unbekannter Bug live gefunden:**
`/vendor/react.js`/`react-dom.js`/`babel.js` sendeten `Content-Encoding:
gzip` **zweimal** (`curl -D -` zeigte den Header doppelt). Ursache:
`WebServer::_streamFileCore()` (Framework-Quellcode nachgelesen,
`~/.platformio/packages/framework-arduinoespressif32/libraries/WebServer/
src/WebServer.cpp:792`) erkennt selbst, wenn der übergebene Dateiname auf
`.gz` endet, und setzt dann automatisch `Content-Encoding: gzip` —
zusätzlich zum eigenen manuellen `server.sendHeader(...)`-Aufruf davor.
Laut HTTP-Semantik (RFC 7230 §3.2.2) sind zwei gleichnamige Header
äquivalent zu einer kommagetrennten Liste (`gzip, gzip`), was Browser dazu
bringen kann, den Body fälschlich zweimal zu entgzippen und zu scheitern
— hätte die gesamte Offline-Bündelung vom 2026-08-15 im Browser
unbrauchbar machen können, obwohl `pio run -t buildfs` (reine
Größenprüfung) das nie hätte auffangen können. **Gefixt:** die drei
manuellen `sendHeader`-Aufrufe entfernt. Mit `curl --compressed`
(dekomprimiert wie ein Browser) verifiziert: Body dekodiert jetzt sauber
zu echtem React-Quellcode (`/**\n * @license React...`). Neu geflasht
(nur Firmware, kein `uploadfs` nötig) und den Header-Fix am echten Gerät
nochmal per `curl -D -` bestätigt (nur noch ein `Content-Encoding: gzip`).

**Fazit:** Dieser Fund ist ein gutes Beispiel dafür, warum „kompiliert
und Größe passt" nicht dasselbe ist wie „funktioniert wirklich" — der Bug
war für `pio run`/`pio run -t buildfs` unsichtbar (beide prüfen nur
Kompilierbarkeit bzw. Dateigröße, nicht HTTP-Verhalten), wurde aber beim
ersten echten `curl`-Request gegen das Gerät sofort sichtbar.

**Noch nicht geprüft:** die UI selbst im Browser (visuell), die 4 frisch
reparierten FX-Panels tatsächlich am Fixture, der Blackout-Panic-Button,
der Chaser-Restart-Fix — all das braucht entweder einen Browser-Test oder
direkte Beobachtung des Fixtures, was in dieser (CLI-basierten) Session
nicht möglich war.

---

## 2026-08-17 (Fortsetzung) — Echter Hands-on-Hardware-Test, 7 gemeldete Bugs

User hat das Gerät tatsächlich in Betrieb genommen (Fixture live beobachtet,
nicht nur `curl`) und einen konkreten, ungefilterten Fehlerbericht geliefert
(wörtlich, mit Tippfehlern): „kill all fx stopt nicht das prismrad
(mindestens). static, rotating gogo stimmen offensichtlich die numbers
nicht, z.b. gobo 6 static kommt nicht. da stimmen wohl die zahlen nicht, da
ging früher immer. dimmer fx scheint kaputt, schaltet sich manchmal selbst
aus, updated master dimmer slider, times stimmen iwie nicht, curves haben
keine funktion. gobo rotation laufen auf internal timer extrem ruckelig,
wenn andere zeiten als 1ms eingestellt sind. selbe bei prism rotation. das
ging alles schon mal. wenn ich prism rotation ausschalte, läuft es einfach
weiter ob wohl es vorher aus war. color chaser auch, läuft manchmal weiter
nach stop oder geht selbst wieder an. offentlich ein sync problem web api.
gobo chaser auf shake läuft einfach durch." Mitten in der Untersuchung kam
ein Nachtrag dazu: der Curve-Parameter des Movement-Joysticks (virtuell und
Pfeiltasten) habe ebenfalls keine Wirkung, und der „Jog"-Regler im Live-Tab
snappe nach Loslassen nicht auf Mitte zurück.

**Untersuchungsmethode:** Jeder Punkt wurde einzeln am Code nachvollzogen
(nicht pauschal „gefixt"), inklusive `git log`-Checks, um zwischen echten
Regressionen und alten, schon immer so gewesenen Verhaltensweisen zu
unterscheiden.

### Root-caused und gefixt (5 von 7 Kernpunkten + beide Nachträge)

1. **„kill all fx stopt nicht das prismrad" / Gobo-Rotation-Motor läuft
   nach Stop weiter.** `updateEngines()` (`Moving_Head_Horizon.ino`) schrieb
   `dmxData[9]` (Gobo-Index/Rotation) und `dmxData[11]` (Prisma-Rotation)
   nur innerhalb von `if (gRotFX.active)`/`if (pRotFX.active)` — sobald
   `.active` durch `/kill_fx` oder `/modfx?...&a=0` auf `false` wechselte,
   wurde der Kanal schlicht nie wieder beschrieben und blieb für immer auf
   dem letzten FX-Wert eingefroren stehen. Der physische Motor lief also
   sichtbar weiter, obwohl die Firmware sich selbst als „gestoppt" ansah.
   **Fix:** zwei neue `static bool gRotWasActive`/`pRotWasActive` erkennen
   die Flanke aktiv→inaktiv und schreiben in genau diesem einen Frame
   einmalig `0` auf den jeweiligen Kanal — danach bleibt der Kanal wieder
   unangetastet, damit eine anschließende manuelle Steuerung (z. B.
   `/set_all`) nicht laufend überschrieben wird. Bewusst NICHT auf
   `colFX`/`sgobFX`/`rgobFX` (Farb-/Gobo-Wheel-Position) angewendet — dort
   ist „an der zuletzt gewählten Position stehenbleiben" beim Stoppen
   erwartetes, unproblematisches Verhalten (kein Motor, keine
   Dauerbewegung), anders als bei einem echten Rotationskanal.

2. **„gobo rotation laufen auf internal timer extrem ruckelig... selbe bei
   prism rotation" — und indirekt Teil von „dimmer fx scheint kaputt".**
   `Modulator::process()` (`FX_Engine.h`) berechnet die Phasengeschwindigkeit
   als `phase += (speed / 100.0f) * dt * 2.0f`. Die Frontend-Defaults für
   `dimSp`/`grSp`/`prSp` sind alle `2000` (`data/index.html`) — bei diesem
   Divisor ergibt das eine volle Zykluszeit von nur ~25 ms (40 Hz), also
   eine viel zu schnelle, für einen mechanischen Motor unmögliche
   Zielwert-Oszillation, die sich als Rucklen/Zittern äußert statt als
   sanfte Rotation. Zum Vergleich: `MovementEngine::process()` nutzt exakt
   dieselbe Formel (`modSp / 100.0f`), aber mit eigenem Default `10.0f` (der
   frontend-seitige `fxMS`-Default ist `100`) — das ergibt dort eine
   nachweislich funktionierende Zykluszeit von 0,5s. Die beiden „Speed"-
   Wertebereiche waren also nie aufeinander abgestimmt, obwohl sie dieselbe
   Formel teilen. **Fix:** Divisor in `Modulator::process()` von `100.0f`
   auf `2000.0f` angehoben — dasselbe Divisor/Default-Verhältnis wie bei
   `MovementEngine`, ergibt beim Default-Speed (2000) jetzt ebenfalls eine
   0,5s-Zykluszeit. `MovementEngine::process()` bewusst nicht angefasst
   (eigene, bereits korrekt kalibrierte Werte).

3. **„dimmer fx scheint kaputt, schaltet sich manchmal selbst aus" /
   „color chaser... läuft manchmal weiter nach stop oder geht selbst wieder
   an" — User-Diagnose „offensichtlich ein sync problem web api" war
   korrekt.** Root Cause: Race Condition zwischen dem 2-Sekunden-Poll
   (`/api/get_dmx`) und einem gerade erst lokal umgeschalteten
   Running-Flag (`dimFxRunning`, `colFxRunning`, `grFxRunning`,
   `prFxRunning`, `sgFxRunning`, `rgFxRunning`, `fxRunning`,
   `showRunning`). Ablauf: User klickt „Stop", lokaler State wird
   sofort `false`, ein `/modfx`-Request geht raus — falls aber zu diesem
   Zeitpunkt schon eine ÄLTERE Poll-Antwort unterwegs war (die das Gerät
   noch VOR dem Stop-Klick beantwortet hatte, also noch `true` meldet),
   überschreibt die kurz danach eintreffende Poll-Antwort den gerade erst
   gesetzten `false` wieder mit `true` — der Toggle „springt selbst
   wieder an". Der existierende `isReceiving`-Mechanismus schützte nur
   ausgehende Sends während eines Polls, nicht eingehende Poll-Daten vor
   einem kurz zuvor lokal geänderten Feld. **Fix:** neuer
   `dirtyUntilRef`-Ref (`data/index.html`) — im selben Moment, in dem der
   Outbound-Sync-Effekt eine Running-Flag-Änderung tatsächlich sendet,
   wird das betroffene Feld für 2,5 Sekunden (mehr als ein voller
   Poll-Zyklus) als „lokal frisch" markiert; der Poll-Merge-Block
   überspringt in diesem Fenster den eingehenden Wert für genau dieses
   Feld und behält den lokalen. Nach Ablauf des Fensters vertraut die UI
   dem Poll wieder normal. Betrifft alle 8 Running-Flags gleichermaßen,
   nicht nur Dimmer/Color.

4. **„curves haben keine funktion" (Dimmer-/Gobo-Rot-/Prisma-Rot-FX).**
   Separater, schon in der letzten `/ultrareview`-Runde (2026-08-16)
   teilweise gefixter Bug-Typ war hier nochmal aufgetreten: `DimmerFx`/
   `RotationFx` (Gobo- und Prisma-Sektion) banden ihre Mode-/Curve-Dropdowns
   an `state.dimMode`/`dimCurve`/`grMode`/`grCurve`/`prMode`/`prCurve` —
   Langform-Keys, die im tatsächlich gesendeten/empfangenen State nirgends
   existieren (der echte Sync läuft über `dimMo`/`dimCu`/`grMo`/`grCu`/
   `prMo`/`prCu`). Die vorherige Runde hatte exakt dieses Muster bereits
   für Trigger/Sync/Speed gefixt, aber Mode/Curve dabei übersehen — hätte
   mit derselben systematischen Cross-Referenz-Prüfung (State-Key-Liste
   gegen Outbound-Sync-„ground truth") schon damals mit auffallen können.
   **Fix:** alle 6 betroffenen Bindings auf die korrekten Kurzform-Keys
   umgestellt, per Python-Script exhaustiv gegen die Outbound-Sync-Ground-
   Truth verifiziert (keine verwaisten Keys mehr in den FX-Panels).

5. **Nachtrag — „jog" im Live-Tab snappt nach Loslassen nicht auf Mitte
   zurück.** `JogDial` (`data/index.html`) rief `onRelease` über ein
   komplett unsichtbares (`display:'none'`), separates
   `<input type="range">`-Dummy-Element auf, das mit dem tatsächlich
   sichtbaren, per Pointer-Events gesteuerten `RangeSlider`-Custom-Element
   gar nichts zu tun hatte — unsichtbare Elemente erhalten in Browsern
   grundsätzlich keine echten Maus-/Touch-Events, der Handler feuerte also
   nie. Der echte `RangeSlider` besaß bis dahin überhaupt kein
   `onRelease`-Prop. **Fix:** `RangeSlider` bekommt ein echtes
   `onRelease`-Prop, ruft es am Ende von `handleUp` (Pointer-Up/-Cancel)
   auf; `JogDial` reicht `onRelease` jetzt an den echten Regler durch, das
   tote Dummy-Element entfernt. Wichtig für den User: `jogBend` selbst
   bewegt weiterhin keine DMX-Kanäle (separates, schon vor dieser Session
   in `backlog.md`/`README.md` bekanntes „toter Code"-Thema) — dieser Fix
   behebt nur das visuelle Zurückspringen des Reglers, nicht die fehlende
   physische Wirkung von Jog.

6. **Nachtrag — Movement-„Curve"-Regler (virtueller Joystick/Pfeiltasten)
   ohne sichtbare Wirkung, User fragte „das war ja normal die initial
   beschleunigung, oder?".** Root Cause in `updateEngines()`
   (`Moving_Head_Horizon.ino`): `joyInputX`/`joyInputY` sind bei
   Tastatur-Input (`useKeyboardJoystick`, normalisiert `x/=dist; y/=dist`)
   und bei voll ausgelenktem virtuellem Joystick (Drag bis an den
   Rand) strukturell immer ein Einheitsvektor — Betrag exakt `1` (außer
   bei diagonalen Tasten-Kombinationen oder partiellem Maus-Drag). Die
   bisherige Formel `powf(fabsf(joyInputX), joyCurve)` liefert für
   `|x|==1` unabhängig vom `joyCurve`-Wert immer `1` (`1^n == 1`) — die
   Kurve konnte also in den beiden häufigsten Bedienarten (Tastatur, voll
   ausgelenkter Joystick) strukturell nie etwas bewirken. Per `git log`
   verifiziert: keine Regression dieser Session, die Formel war seit der
   initialen Code-Konsolidierung unverändert. **Fix:** Die Kurve wird
   jetzt auf `joySmoothX`/`joySmoothY` angewendet (den bereits
   momentum-geglätteten Rampen-Wert, der bei jeder Eingabemethode beim
   Loslegen erst von 0 Richtung Zielwert hochläuft und dabei alle
   Zwischenwerte durchläuft) statt auf den rohen `joyInputX`/`joyInputY`.
   Das ergibt eine echte, sichtbare Anfangsbeschleunigung/-Kurve
   unabhängig von der Eingabemethode, ohne die von `joyMomentum`
   gesteuerte Ramp-Geschwindigkeit selbst zu verändern (reine
   Verschiebung, wo im Signalpfad die Kurve angewendet wird, keine neue
   Zeitkonstante).

### Bewusst nicht blind gefixt (2 von 7 Kernpunkten)

- **„static, rotating gogo stimmen offensichtlich die numbers nicht, z.b.
  gobo 6 static kommt nicht".** `SGOBOS`/`RGOBOS` (`data/index.html`) und
  `sGoboMap`/`rGoboMap` (`Moving_Head_Horizon.ino`) wurden per `git log -p`
  geprüft: seit der allerersten Einführung im Rahmen der Code-
  Konsolidierung unverändert, und intern konsistent (Frontend-Wert für
  „Gobo 6" == Backend-Map-Wert an Index 6 == `CH.GOBO`/`CH_GOBO` beide
  `7`). Kein Code-Bug auffindbar — die plausibelste Erklärung ist eine
  Diskrepanz zwischen den hier hinterlegten DMX-Werten und der tatsächlichen
  Gobo-Wheel-Personality des physischen Fixtures (Pro Beam 280), die ohne
  Datenblatt oder einen manuellen DMX-Sweep (CH7 langsam 0–255 durchfahren,
  echte Gobo-Wechsel am Gerät notieren) nicht sicher zu kalibrieren ist.
  In `backlog.md` festgehalten statt geraten.
- **„gobo chaser auf shake läuft einfach durch".** `runStep()`s
  Scratch/Shake-Zweig (`if (fx.scratch) val = constrain(val +
  STEPFX_SCRATCH_OFFSET, 0, 255);`) addiert nur eine Konstante (`183`) auf
  den jeweils aktuellen Wheel-Schritt-Wert, im selben Hold-Time-Takt wie
  normales Chasen — das erzeugt keinen echten Shake (schnelles Zittern
  innerhalb eines Hold-Intervalls), sondern verschiebt die Chase-Sequenz
  nur in eine andere DMX-Zone, was sich wie „normales Weiterlaufen"
  anfühlt. `STEPFX_SCRATCH_OFFSET = 183` wurde am 2026-08-16 selbst nur
  benannt (vorher eine unbenannte Magic Number mit demselben Wert), nie
  gegen echte Hardware verifiziert — dieser Fund zeigt jetzt, dass der
  geratene Wert/Mechanismus so nicht funktioniert. Ohne Fixture-Datenblatt
  oder Hardware-Sweep nicht seriös neu zu raten. In `backlog.md`
  festgehalten statt eines zweiten Blindschusses.

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]` nach allen Änderungen.
Auf dem angeschlossenen echten Gerät geflasht (`pio run -t upload` +
`-t uploadfs`, Port `/dev/cu.usbmodem1101`). Nach dem Flash per `curl`
bestätigt: Gerät wieder erreichbar (`/api/get_dmx` liefert `200`, reale
Preset-Namen intakt). Die eigentlichen Verhaltensänderungen (Motor stoppt
wirklich, Rotation läuft ruckelfrei, FX-Toggle bleibt stabil, Jog snappt
zurück, Joystick-Kurve fühlbar) sind visuell/physisch am Fixture zu
bestätigen — das kann nur der User mit Augen auf UI und Lampe, nicht CLI.

---

## 2026-08-17 (Fortsetzung) — Direktes Nutzer-Feedback zum Joystick-Fix, 5 weitere Punkte

Unmittelbar nach dem vorigen Fix-Batch (Motor-Stop, Modulator-Speed,
Poll-Race, Mode/Curve, Jog-Snapback, Joystick-Curve v1) kam noch am selben
Tag konkretes Feedback zurück, wörtlich: „die controls für den joystick
die ich im livetab habe fehlen mir im programmer und im follower tab. auch
habe ich das gefühl, dass curve für den joystick immer noch keine
hinreichende funktion hat weil der movement nicht von 0 aus beschleunigt
wenn ich z.b. max aussteuer. also keine fkt. ich möchte diesen control
block für die anderen tabs auch weil zum einen fehlt es da und zum anderen
gibt es im follower tab einen button "curve" der aber nichts macht. den
advanced motors block im programmer brauchen wir ja nicht, wenn wir den
anderen block aus dem live haben. die constraints im follower tab möchten
wir erhalten natürlich. im follower tab joystick feld wird manchmal ein
dotted kreis gezeichnet iwo im feld der sich nicht bewegt. wenn ich f5 im
browser drücke komme ich immer im livetab an, nicht dort wo ich vorher
war."

### 1. Joystick-Curve grundlegend neu gebaut (v1 aus derselben Session war unzureichend)

Die vorherige Änderung in diesem Chat (Curve auf den momentum-geglätteten
Rampen-Wert `joySmoothX`/`joySmoothY` statt auf den rohen Input anwenden)
war am Code technisch korrekt begründet, aber praktisch zu schwach: der
Momentum-Blend (`blend = 1 - pow(1-smoothFactor, dt*30)`) erreicht seinen
Zielwert bei Default-Momentum in ca. 7 Frames (~144ms bei 50Hz) — jede
`pow()`-Umformung dieses fast augenblicklich konvergierenden Werts bleibt
für das menschliche Auge am Fixture praktisch unsichtbar, insbesondere bei
mechanischer Trägheit des Motors selbst. Der User hatte also recht: „keine
fkt" traf weiterhin zu, trotz der vorherigen, in sich korrekten Analyse.

**Neuer Ansatz:** Curve ist jetzt vollständig vom Momentum-Blend
entkoppelt. Ein neuer, zeitbasierter Tracker (`static float joyHoldTime`)
zählt hoch, solange der Stick in irgendeine Richtung ausgelenkt ist
(`joyHeld`), und wird bei Loslassen sofort auf 0 zurückgesetzt. Daraus:
`rampT = constrain(joyHoldTime / 2.0f, 0, 1)` — eine feste, klar
wahrnehmbare 2-Sekunden-Rampe, unabhängig von Momentum. `accelMul =
powf(rampT, joyCurve)` formt diese Rampe: bei `joyCurve=1` (Minimum)
linear über 2s, bei höheren Werten (Default 2.0, Maximum 3.0) bleibt die
Geschwindigkeit länger niedrig und steigt erst spät steil an — spürbar
mehr „Anlaufzeit". `accelMul` multipliziert die tatsächliche
Geschwindigkeit (`pD`/`tD`), aber **nur während der Stick aktiv gehalten
wird** (`joyHeld ? ... : 1.0f`) — beim Loslassen bleibt `accelMul` sofort
bei `1.0`, sodass die bestehende, unveränderte Momentum-Abbremsung
(`joySmoothX`s natürlicher Zerfall) exakt wie vorher weiterläuft. Dadurch
bleibt „Loslassen bremst weich ab" unangetastet, während „Reindrücken
beschleunigt sichtbar von 0" jetzt neu und robust funktioniert.

### 2. Joystick-Kontrollblock in Programmer- und Followspot-Tab nachgerüstet

Der komplette „SPEED · CURVE · MOMENTUM"-Regelblock (Max Speed / Curve /
Momentum → `/joy_cfg`) existierte bisher nur im Live-Tab, inline definiert
innerhalb von `LiveTab` selbst — Programmer- und Followspot-Tab hatten
eigene Joysticks, aber keinen Zugriff auf diese Parameter. In eine neue
gemeinsame Komponente `JoystickAdvancedControls` extrahiert (definiert im
gemeinsamen Widgets-Scope `horizon-primitives.jsx`, über
`Object.assign(window, {...})` exportiert, wie die anderen geteilten
Bausteine `Pill`/`JogDial`/etc. — nötig, weil jeder
`<script type="text/babel">`-Block sein eigener JS-Scope ist). Jetzt in
allen drei Tabs (Live/Programmer/Followspot) eingebunden, ein einziger,
konsistenter Regelsatz statt einer isolierten Kopie.

### 3. Toter „Curve"-Button im Followspot-Tab gefunden und ersetzt

Der User erwähnte „gibt es im follower tab einen button curve der aber
nichts macht" — Ursache: `<Pill>Curve</Pill>` in der Pills-Reihe
(Pan Rev/Center/Tilt Rev/Curve) hatte überhaupt kein `onClick` und keinen
`active`-State, ein reines Deko-Element ohne jede Funktion. Entfernt und
durch den neuen, echten `JoystickAdvancedControls`-Block ersetzt (der
Curve jetzt tatsächlich implementiert). Die Pills-Reihe ist dadurch wieder
3-spaltig wie in Live/Programmer statt 4-spaltig mit totem Slot.

### 4. „Advanced Motors"-Accordion im Programmer-Tab entfernt

Auf expliziten Wunsch des Users („den advanced motors block im programmer
brauchen wir ja nicht, wenn wir den anderen block aus dem live haben") —
die manuellen Regler für Motor Speed (CH5), Pan Fine (CH15) und Tilt Fine
(CH16) sind mit dem neuen gemeinsamen Joystick-Block redundant genug, um
sie zu entfernen. Die zugehörigen State-Felder (`motorSpeed`, `panFine`,
`tiltFine`) und ihre `track()`-Aufrufe im Outbound-Sync-Effekt bleiben
unverändert bestehen — nur die UI-Regler in diesem Tab sind weg, die
Werte werden weiterhin normal synchronisiert.

### 5. Gestrichelter Marker im Followspot-Joystick wirkte „eingefroren"

„im follower tab joystick feld wird manchmal ein dotted kreis gezeichnet
iwo im feld der sich nicht bewegt" — das ist der `externalPos`-Marker in
`StickyJoystick` (gestrichelter Ring, zeigt die reale, vom Gerät gemeldete
Fixture-Position, getrennt vom eigentlichen Joystick-Thumb). Er wird aus
`state.pan`/`state.tilt` berechnet, die nur alle ~2s vom `/api/get_dmx`-
Poll aktualisiert werden — dadurch sprang der Marker bisher bei jedem
Poll-Tick abrupt an eine neue Position und stand die meiste Zeit
regungslos irgendwo im Feld, was sich exakt wie ein eingefrorener
Fremdkörper liest statt wie eine Positionsanzeige. **Fix:** eine kleine
Ease-Schleife (`requestAnimationFrame`, 12% Annäherung pro Frame) glättet
den gerenderten Marker jetzt kontinuierlich in Richtung des jeweils
neuesten Poll-Werts, statt zu springen — bewegt sich jetzt sichtbar
statt zu „kleben". Keine Änderung an der zugrundeliegenden ~2s-Poll-
Kadenz selbst (das ist ein bekanntes, bereits in `backlog.md` unter Tech
Debt vermerktes größeres Thema).

### 6. Tab-Wahl übersteht jetzt F5/Reload

„wenn ich f5 im browser drücke komme ich immer im livetab an, nicht dort
wo ich vorher war" — `tab`-State war ein reiner In-Memory-`useState`.
Gefixt nach demselben, bereits etablierten Muster wie `night`/`accent`
(`localStorage`): neuer Key `hz_tab`, beim Tab-Wechsel geschrieben, beim
Start gelesen (mit Validierung gegen die vier bekannten Tab-Namen,
Fallback `LIVE` bei fehlendem/ungültigem Wert).

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`pio run -t upload` +
`-t uploadfs`), danach per `curl` als online bestätigt (`/` liefert `200`,
`/api/get_dmx` liefert reale Preset-Namen). Wie beim vorigen Fix-Batch
gilt: das neue Beschleunigungsverhalten, die drei Tabs mit identischen
Joystick-Reglern und der geglättete Followspot-Marker sind visuell im
Browser/am Fixture zu bestätigen, nicht per CLI verifizierbar.

---

## 2026-08-17 (Fortsetzung) — Offizielles Fixture-Datenblatt beschafft und ausgewertet

User lieferte vier Original-Herstellerdateien zum Fixture (SHEHDS 160W
3in1 GOBO, im Projekt bisher nur als „Pro Beam 280" bezeichnet):
`160W三合一光束灯-LED-说明书.pdf` (Handbuch, Chinesisch+Englisch),
`SHEHDS_160W3in1GOBO.d4` (Avolites-Fixture-Personality, XML),
`160W gobo.R20` (MagicQ/Chamsys-Personality, Klartext), `160W gobo.ssl2`
(unbekanntes Binärformat). Auftrag: alles extrahieren und in eine
dauerhafte Projekt-Referenz übertragen, weil in den vorigen Sessions
mehrfach ohne belastbare Datengrundlage geraten werden musste (Shake-
Offset, Gobo-Nummerierung) — User: „ich dachte eigentlich dass das iwo
gestanden hätte."

### Werkzeuge

`poppler` (insb. `pdftotext`) war nicht installiert — über `brew install
poppler` nachinstalliert, danach `pdftotext -layout` genutzt, um den
kompletten Handbuchtext inkl. der tabellarischen DMX-Kanalübersicht
sauber (mit erhaltener Spaltenausrichtung) zu extrahieren. `.d4` ist
valides UTF-8-XML, direkt lesbar. `.R20` ist ein klartextbasiertes
Custom-Format mit Kommentaren, direkt lesbar. `.ssl2` erwies sich per
`file`/`strings` als reines Binärformat ohne extrahierbaren Klartext
(vermutlich eine proprietäre, komprimierte oder verschlüsselte MagicQ-
Show-Datei) — keine zusätzlichen Daten daraus gewonnen, im neuen
Referenzdokument als „nicht auslesbar" vermerkt statt stillschweigend
ignoriert.

### Neue Referenzdatei

`doc/content/mapping_sheds_160w_3in1_gobo.md` — vollständige, kanalweise
DMX-Tabelle (CH1–CH18) direkt aus dem Handbuch übertragen, pro Kanal mit
einem Abgleich gegen den aktuellen Code (`wheelMap`/`sGoboMap`/
`rGoboMap` in `Moving_Head_Horizon.ino`, `SGOBOS`/`RGOBOS`/`COLOR_STEPS`
in `data/index.html`), plus die Bordmenü-Struktur des Fixtures (Abschnitt
9/10 des Handbuchs) als Anhang.

### Wichtigste Erkenntnisse aus dem Abgleich

1. **Farbrad (CH6), statisches Gobo-Rad (CH7), rotierendes Gobo-Rad
   (CH8), Prisma (CH10), Frost (CH12) — alle bereits korrekt im Code.**
   `wheelMap[20]`, `sGoboMap[10]`, `rGoboMap[7]` sowie die
   Prism/Frost-Schwellwerte (128 als An/Aus-Grenze) stimmen exakt mit dem
   offiziellen Datenblatt überein. Das bedeutet insbesondere: der vom
   User in der vorigen Runde gemeldete Bug „Gobo 6 static kommt nicht"
   ist **kein Code-Bug** — `sGoboMap[6] = 60` trifft exakt die Mitte der
   offiziellen Gobo-6-Zone (CH7, 60–69). Die wahrscheinlichste Erklärung
   ist eine physische Abweichung des tatsächlichen Rads dieses konkreten
   Geräts vom gedruckten Handbuch, oder ein mechanischer Defekt an
   genau dieser Position — nicht per Software zu beheben, nur durch
   Sichtprüfung am Gerät zu klären. Damit ist auch die in der vorigen
   Runde offen gebliebene Unsicherheit aufgelöst (dort nur per `git log`
   auf „keine Regression" geprüft, jetzt zusätzlich per echtem
   Datenblatt auf „numerisch korrekt" verifiziert).

2. **Shake-Formel war nachweislich falsch — jetzt mit echten Zahlen
   gefixt.** Das Handbuch listet für CH7 und CH8 jeweils sehr schmale
   (5 DMX-Werte breite), individuell pro Gobo-Nummer versetzte Shake-
   Zonen: CH7 `211–215` (Gobo1) bis `251–255` (Gobo9), CH8 `226–230`
   (Gobo1) bis `251–255` (Gobo6) — Formel `basis + (gobo_nr - 1) × 5`.
   Der bisherige Code (`STEPFX_SCRATCH_OFFSET = 183`, in der vorigen
   `/ultrareview`-Runde nur als Konstante *benannt*, nie gegen echte
   Hardware verifiziert) addierte stattdessen einen einzigen, für alle
   Gobo-Nummern identischen Offset auf den Wheel-Wert — bei Gobo 6 auf
   CH7 z. B. `60 + 183 = 243`, was in die offizielle **Gobo-7**-Shake-Zone
   (241–245) fällt statt in die korrekte Gobo-6-Zone (236–240). Erklärt
   direkt den Bug-Report „gobo chaser auf shake läuft einfach durch" —
   der Offset landete bei praktisch jeder Gobo-Nummer in einer falschen
   oder unpassenden DMX-Zone.

   **Fix in `Moving_Head_Horizon.ino`:** `runStep()`s Lambda bekommt einen
   neuen Parameter `shakeBase` (211 für `sgobFX`/CH7, 226 für
   `rgobFX`/CH8, `0` für `colFX`/CH6 — das Farbrad hat laut Datenblatt gar
   keine Shake-Funktion). Bei aktivem `fx.scratch` und `currentIdx > 0`
   (kein Shake für „White"/Index 0, dafür definiert das Handbuch keine
   Zone) wird jetzt `shakeBase + (currentIdx - 1) × 5` berechnet statt der
   alten Pauschal-Addition. `STEPFX_SCRATCH_OFFSET` als toter Code
   entfernt.

3. **CH9 (Gobo-Rotation/Index) hat zwei entgegengesetzte
   Drehrichtungs-Zonen (64–192 CW, 193–255 CCW) — dokumentiert, aber kein
   Fix nötig.** Ein Modulator-Bereich, der diese Grenze überschreitet,
   würde beim Durchlaufen der LFO-Kurve sichtbar die Drehrichtung
   wechseln. Der aktuelle Frontend-Default für `gRotFX` (`grSt:135,
   grEn:190`) liegt bereits komplett innerhalb der CW-Zone — safe, aber
   bisher nirgends als Invariante festgehalten. Jetzt in der neuen
   Referenzdatei dokumentiert, damit künftige Preset-/Default-Änderungen
   diese Grenze nicht versehentlich überschreiten.

4. **CH17 (Macro) — Diskrepanz erkannt, bewusst nicht blind gefixt.** Das
   Datenblatt kennt nur drei grobe Sammelzonen (`10–120`/`121–150` beide
   „Auto mode", `151–255` „Sound mode"), ohne benannte Einzelmakros.
   Unser Frontend-Dropdown bietet dagegen 13 granulare, benannte Werte
   (Lamp On/Off, Reset fixture, Fan speed, Demo modes, …) — das sieht nach
   einer generischen Platzhalterliste aus einer anderen Fixture-Vorlage
   aus, nicht nach für dieses Gerät verifizierten Werten. Da das
   Handbuch selbst zu ungenau ist, um die echten Einzelwerte
   abzuleiten, wurde hier bewusst nichts geändert — als offener Punkt in
   der Referenzdatei vermerkt (verifizierbar nur durch Durchklicken des
   Bordmenüs „Set → Run Mode" am echten Gerät).

5. **CH5 (Speed) bleibt absichtlich unverändert.** Das Handbuch
   beschreibt „Pan/Tilt speed, Pan/Tilt time" ohne exakten Split-Punkt
   zwischen den beiden Modi — nicht genug Information, um eine
   Verhaltensänderung zu rechtfertigen. Nur als bekannte Unschärfe
   dokumentiert.

### Verifikation

`pio run` `[SUCCESS]` (nur `Moving_Head_Horizon.ino` geändert, kein
`buildfs` nötig, da `data/` nicht angefasst wurde). Auf dem
angeschlossenen echten Gerät geflasht (`pio run -t upload`), danach per
`curl` als online bestätigt (`/api/get_dmx` liefert reale Live-Werte).
Ob der Shake-Effekt jetzt tatsächlich sichtbar/spürbar am Fixture
funktioniert und ob die Gobo-6-Frage tatsächlich physisch/mechanisch ist,
kann nur der User am echten Gerät bestätigen.

---

## 2026-08-17 (Fortsetzung) — Eigene Regression aus der Curve-Runde gefunden, plus ein User-diagnostizierter Bug

Direkt im Anschluss an die vorige Runde (Fixture-Datenblatt/Shake-Fix)
meldete der User drei zusammenhängende Beobachtungen aus echtem
Live-Betrieb, in zwei Nachrichten kurz hintereinander.

### 1. „Einmal tippen mit den Pfeiltasten ergibt immer schon 8 steps beim fahren... fährt dann noch weiter wenn man schon losgelassen hat"

Root Cause: eine selbst verursachte Regression aus der vorigen Runde
(dort „Joystick-Curve neu gebaut, v2"). Die dortige Änderung führte
`accelMul` ein — 0→1 gerampt über eine feste Haltezeit, multipliziert auf
die Bewegungsgeschwindigkeit. Der Fehler: beim Loslassen (`joyHeld` wird
`false`) sprang `accelMul` unconditional auf `1.0`, „damit die
Verzögerung normal weiterläuft" — aber `joySmoothX` (der
momentum-geglättete Wert, der die eigentliche Bewegungsrichtung/-stärke
trägt) konvergiert **unabhängig von `accelMul`** gegen `joyInputX` und
kann daher schon nahe am Zielwert stehen, selbst wenn `accelMul` während
des kurzen Haltens noch klein war (frühe Rampenphase). Das Produkt
`joySmoothX × accelMul` machte also GENAU im Loslass-Moment einen Sprung
von „klein × klein" auf „(fast) voll × 1.0" — ein kurzer, unkontrollierter
Geschwindigkeitsausschlag exakt beim Loslassen, gefolgt vom normalen
Momentum-Ausklingen bei jetzt vollem Multiplikator. Erklärt beide
Symptome in einem: der Ausschlag selbst („8 steps" bei einem kurzen Tap)
und das sichtbare Weiterfahren danach (Ausklingen jetzt bei vollem statt
gerampten Multiplikator).

**Fix:** `accelMul` friert beim Loslassen auf seinem zuletzt gerampten
Wert ein, statt auf `1.0` zu springen. Dadurch bleibt der Übergang
Halten→Loslassen stetig — ein kurzer Tap bleibt während der gesamten
Interaktion (Halten *und* Ausklingen) klein, ein langes Halten bei voller
Rampe verhält sich weiterhin wie vorher (voller Multiplikator, normales
Momentum-Ausklingen).

### 2. „Curve=0 und Momentum=0 sollte sofort hart losballern, hat aber trotzdem Rampe"

Nachtrag des Users, unmittelbar danach — deckte einen zweiten,
unabhängigen Designfehler in derselben v2-Änderung auf: die Rampen-
**Dauer** war fest auf 2 Sekunden verdrahtet, `joyCurve` veränderte nur
die *Form* der Kurve über dieses feste Fenster (`powf(rampT, joyCurve)`).
Ein Curve-Wert nahe am Minimum ergab also weiterhin eine 2-Sekunden-
Rampe, nur mit flacherer Kurvenform — nicht das erwartete „Curve aus =
keine Rampe". Nutzer-Erwartung (wie an einem echten Lichtpult): Curve auf
0 bedeutet sofortige Vollgeschwindigkeit, nur höhere Werte fügen eine
Rampe hinzu.

**Fix (v3):** `joyCurve` ist jetzt direkt die Rampendauer in Sekunden,
linear: `accelMul = constrain(joyHoldTime / joyCurve, 0, 1)`, mit
`joyCurve ≈ 0` als Sonderfall für sofortige Vollgeschwindigkeit
(`accelMul = 1.0` immer). Damit das UI diesen Wert überhaupt erreichen
kann: Frontend-Regler-Bereich von `10–30` (repräsentiert 1,0–3,0) auf
`0–50` (repräsentiert 0,0–5,0) erweitert, Backend-Clamp in `/joy_cfg` von
`0,1–5,0` auf `0,0–5,0` gelockert (vorher hätte selbst ein gesendetes
`crv=0` serverseitig auf 0,1 angehoben — nah an sofort, aber nicht
exakt). `joyMomentum=0` war schon vorher korrekt „sofort" (der
Momentum-Blend kollabiert bei `smoothFactor=1` bereits auf `blend=1`,
also Instant-Tracking) — dieser Teil brauchte keine Änderung, nur die
Curve-Seite war betroffen.

### 3. „Stop Gobo Rot" setzte CH9 nicht auf 0 — User fand die eigentliche Ursache selbst

Diese Nachricht enthielt zwei Beobachtungen, die sich als **derselbe
Bug** herausstellten: (a) CH9 wird beim Stoppen von `gRotFX` nicht
zuverlässig auf 0 gesetzt, der Motor dreht scheinbar weiter, und (b) der
Verdacht, dass die kontinuierliche Live-Aktualisierung der Programmer-
Tab-Slider während eine FX/Chaser läuft unnötig Bandbreite verbraucht.

Nachvollzogen: `state.goboRot` (der rohe CH9-Slider im Programmer-Tab)
wird bei jedem `/api/get_dmx`-Poll (alle ~2s) auf den *aktuellen,
FX-getriebenen* Live-Wert von CH9 aktualisiert — unabhängig davon, ob der
User diesen Slider je manuell angefasst hat. Der Outbound-Sync-
`track()`-Mechanismus (derselbe, der z. B. `state.dimmer` per `/set_all`
synct) hatte dafür **keine Ausnahme** — sobald der zuletzt gesendete
Baseline-Wert (`p['ch9']`) vom aktuellen `state.goboRot` abwich (was bei
laufendem `gRotFX` fast immer der Fall ist, da CH9 ständig oszilliert),
wurde der Poll-Snapshot per `/set_all?c9=...` zurück ans Gerät gesendet —
ein reiner Echo-Roundtrip, der bei laufender FX (die den Kanal
Zig-mal-pro-Sekunde neu schreibt) unauffällig bleibt, aber genau im
Moment des Stoppens gefährlich wird: der letzte, VOR dem Stopp gepollte
(also veraltete) CH9-Wert steht noch in `state.goboRot`, während die
Backend-FX-Engine den Kanal bereits korrekt auf `0` zurückgesetzt hat
(Fix aus einer früheren Runde). Der nächste `track()`-Durchlauf erkennt
den (jetzt fälschlich als „Änderung" interpretierten) Unterschied und
schreibt den alten, veralteten Wert per `/set_all` zurück — und macht den
gerade erst korrekt gesetzten Stop-Wert wieder rückgängig. Der User hatte
also nicht nur ein UX-/Bandbreiten-Anliegen richtig erkannt, sondern
damit direkt auch die tatsächliche Fehlerursache für „CH9 stoppt nicht"
gefunden.

**Fix:** `track(ch, val, skip)` bekommt einen dritten Parameter. Für alle
sechs FX-/Chaser-gekoppelten Kanäle (`CH.DIMMER`↔`dimFxRunning`,
`CH.COLOR`↔`colFxRunning`, `CH.GOBO`↔`sgFxRunning`,
`CH.GOBO_ROT`↔`rgFxRunning`, `CH.GOBO_IDX`↔`grFxRunning`,
`CH.PRISM_ROT`↔`prFxRunning`) wird `skip` auf das jeweilige Running-Flag
gesetzt. Solange `skip` true ist, wird der Kanal komplett von der
Outbound-Sync ausgenommen (kein `/set_all` mehr, behebt auch das
Bandbreiten-Anliegen) — die Vergleichs-Baseline wird dabei aber weiterhin
*still* mitgeführt (`p['ch'+ch] = val`, ohne zu senden), damit beim
Stoppen kein künstlicher Baseline-Mismatch entsteht, der genau dieselbe
Race erneut auslösen würde. Sobald `skip` wieder false ist (FX gestoppt),
läuft die normale Sync sauber weiter, sobald der nächste Poll den Slider
auf den echten (jetzt korrekten) Wert aktualisiert hat.

### Zusätzlich, kleinerer, verwandter Fund: Stop-Kommando konnte in der Debounce-Queue hängen

Bei der Untersuchung von Punkt 1 zusätzlich gefunden: sowohl der
Tastatur- als auch der Maus-/Touch-Joystick senden Bewegungsbefehle über
`tFetch(url, 'joy', 40)` — eine gemeinsame Debounce-/Cooldown-Queue pro
`id`. Ein Stop-Kommando (`x=0,y=0`), das kurz nach einem noch laufenden
Bewegungsbefehl gesendet wird, kann bis zu ~80 ms (Cooldown + zweiter
Roundtrip) in dieser Queue hängen bleiben, bevor es das Gerät überhaupt
erreicht — in diesem Fenster bewegt sich das Fixture nach dem Loslassen
sichtbar weiter. Der User hatte das unabhängig richtig diagnostiziert
(„dieser trigger muss iwie fastlane sofort an die api"). **Fix:** neue
`sendJoy(v)`-Hilfsfunktion — erkennt `x===0 && y===0` und sendet in
diesem Fall direkt per `fetch()` unter Umgehung der Queue, inklusive
Löschen eines eventuell noch wartenden, jetzt überholten
Bewegungsbefehls (`tfPending['joy'] = null`). Von Tastatur- und Maus-
Handler gemeinsam genutzt (`keyJoyRef`, `joystickHandler`).

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`upload` + `uploadfs`), danach per
`curl` als online bestätigt. Ob sich die Joystick-Beschleunigung jetzt
tatsächlich wie erwartet anfühlt (Curve=0 sofort voll, kurzer Tap bleibt
klein, kein Nachlaufen) und ob „Stop Gobo Rot" CH9 jetzt zuverlässig auf 0
hält, kann nur der User am echten Gerät bestätigen.

---

## 2026-08-17 (Fortsetzung) — Shake bekommt echte Parameter, eigener Folgefehler gefunden und gefixt

Weiteres Live-Test-Feedback, noch am selben Tag: „chaser static gobo mit
shake läuft mit richtigen delay aber shake lässt sich nicht einstellen,
(also speed und range) eigentlich sollte das ja iwie eine shake ramp sein
weisst? wenn ich stop drücke shaked der gobo wheel aber weiter."

### Shake bekommt echte Speed-/Range-Parameter

Bisher war „Shake" (`StepFX::scratch`) ein reines An/Aus: bei aktivem
Scratch wurde der DMX-Wert auf einen EINZIGEN, festen Punkt innerhalb der
5-DMX-Werte-breiten Shake-Zone des jeweiligen Gobos gesetzt (siehe
`mapping_sheds_160w_3in1_gobo.md`) — nichts an diesem Wert war
einstellbar, entsprach also nicht der Erwartung „Shake Ramp" (eine
tatsächlich oszillierende Bewegung mit einstellbarer Geschwindigkeit/
Amplitude). Neu gebaut: `StepFX` bekommt zwei neue Felder,
`scratchSpeed` (float, Hz) und `scratchRange` (int, 0–100 % der 5-Werte-
Zone). `runStep()` in `Moving_Head_Horizon.ino` berechnet den Shake-Wert
jetzt als kontinuierliche Dreieckswelle (`fmodf(now/1000 * speedHz, 1.0)`,
dann in eine 0→1→0-Rampe umgewandelt), **jeden Frame neu**, nicht mehr
nur bei jedem diskreten Chase-Schritt — dadurch ist überhaupt eine
sichtbare Oszillation vorhanden, die Speed/Range tatsächlich beeinflussen
können. Neue Query-Parameter `spd`/`rng` an `/sgobfx` und `/rgobfx`
(WebAPI.h, mit Clamps 0,1–20,0 Hz / 0–100 %), im `/api/get_dmx`-JSON als
`sgSp`/`sgRng`/`rgSp`/`rgRng` exponiert. Frontend: `ChaserFx`-Komponente
bekommt zwei neue `LabeledSlider` („Shake speed", „Shake range"), die nur
erscheinen, wenn im Shake-Dropdown „Effect · Shake (Wobble)" gewählt ist.

**Bewusst nicht in `SceneData`/NVS persistiert.** Dieses Gerät hat echte,
im Feld gespeicherte Presets (5 Presets, 8 gepatchte Fixtures). Neue
Felder zu `SceneData` hinzuzufügen ändert dessen `sizeof()` — laut dem
bereits bestehenden Tech-Debt-Eintrag in `backlog.md` würde das alle
aktuell gespeicherten Presets beim nächsten Boot auf Defaults
zurückfallen lassen (der Legacy-Fallback kennt nur ein noch älteres
Format, nicht den heutigen Blob mit altem Layout). `scratchSpeed`/
`scratchRange` leben deshalb nur im RAM-`StepFX`-Objekt: wirken sofort,
werden aber nicht gespeichert — Preset-/Chaser-Recall oder ein Neustart
setzt sie auf den Default (`2.0` Hz / `100` %) zurück. Sollte diese
Live-only-Einschränkung stören, bräuchte es eine echte, durchdachte
`SceneData`-Migration (Versions-Tag), keinen Schnellschuss in dieser
Runde.

### „Wenn ich stop drücke, shaked der Gobo-Wheel aber weiter"

Dasselbe Bug-Muster wie zuvor bei CH9 (Gobo-Rotation-Modulator), nur
diesmal bei den StepFX-Wheel-Choppern (Color-/Static-Gobo-/Rot-Gobo-
Chaser): `runStep()` schrieb `dmxData[channel]` bisher nur innerhalb von
`if (doStep) { ... }` — beim Stoppen (`fx.active` wird `false`) wurde der
Kanal einfach nie wieder beschrieben und blieb auf dem letzten Wert
eingefroren. Bei einem normalen (nicht-shakenden) Gobo ist das harmlos
(Rad bleibt auf der zuletzt gewählten Position stehen) — landete der
Kanal aber gerade MITTEN im Shake (ein Wert innerhalb der 5-DMX-breiten
Shake-Zone), interpretiert das Fixture diesen Wert **selbst, mit seiner
eigenen Firmware** weiterhin als „schütteln" — unabhängig davon, dass
unsere Firmware den Chaser längst als gestoppt betrachtet. Gefixt nach
demselben Muster wie zuvor bei `gRotFX`/`pRotFX`: `runStep()` bekommt
jetzt eine `wasActive`-Referenz pro Chaser (drei separate
`static bool colWasActive, sgWasActive, rgWasActive`, da die Lambda für
alle drei Chaser gemeinsam genutzt wird) und schreibt bei der
aktiv→inaktiv-Flanke einmalig den regulären, nicht-shakenden Wert des
zuletzt gewählten Gobos (`map[currentIdx]`) — der Wert liegt garantiert
außerhalb jeder Shake-Zone, das Fixture hört auf zu schütteln.

### Eigener Folgefehler: erster manueller Wert nach dem Stoppen ging manchmal verloren

Beim Testen dieser Fixes fiel ein dritter, selbst verursachter Bug auf,
noch in derselben Nachricht gemeldet: „wenn ich gobo rot chaser stoppe...
und dann im setup white(0) wähle geht er nicht auf den gobo zurück, ich
muss erst einen anderen wählen und dann auf 0 zurück". Root Cause: der
`track()`-Skip-Mechanismus aus der vorigen Runde (der FX-gekoppelte
Kanäle während des FX-Laufs von der Outbound-Sync ausnimmt) hielt die
Vergleichs-Baseline dabei still auf dem jeweils aktuellen — aber für die
manuelle Steuerung irrelevanten — State-Wert synchron (`p['ch'+ch] = val`
auch während des Skips). Traf der erste manuelle Wert, den der User NACH
dem Stoppen wählt, zufällig mit dieser (die ganze FX-Laufzeit über
unveränderten) Baseline zusammen — der häufigste Fall: beide „0"/White,
da der manuelle Gobo-Picker meist auf seinem Default steht, während der
Chaser lief — erkannte `track()` fälschlich „keine Änderung" und sendete
gar nichts, obwohl der echte Gerätekanal etwas völlig anderes zeigte
(wo immer der Chaser ihn verlassen hat). Wählte der User stattdessen
zuerst einen ANDEREN Wert, änderte sich die Baseline echt, der Send ging
durch — und von dort aus funktionierte auch der Rücksprung auf 0 wieder
normal, weil jetzt eine echte Werteänderung vorlag. Genau das
Umgehungsverhalten, das der User beschrieb.

**Fix:** `track(ch, val, skip)` merkt sich jetzt separat pro Kanal, ob er
beim letzten Aufruf geskippt war (`p['pend'+ch]`). Während des Skips wird
die Baseline gar nicht mehr angefasst. Der erste Aufruf, nachdem `skip`
wieder `false` wird, erzwingt **einen** Resend, unabhängig vom
Baseline-Vergleich (`forceSend`) — der Flag wird erst gelöscht, wenn der
Resend tatsächlich passiert (respektiert weiterhin das
`isReceiving`-Zeitfenster, damit kein Force-Resend in ein laufendes Poll-
Fenster hineinplatzt und verlorengeht).

### Zusätzlich gefixt: Movement-FX „Size" bei 0

Beim Umbau des Shake-Mechanismus zusätzlich gemeldet: Movement-FX „Size"
sollte nicht auf `0` fallen können dürfen. Die Frontend-Regler klammern
bereits auf 1–100, aber `/fx` (`WebAPI.h`, Parameter `zs`/`ze`/`ss`/`se`)
und der Preset-Ladepfad (`triggerSceneFX`) hatten keinen serverseitigen
Clamp — ein Preset mit gespeichertem `size=0` (oder ein direkter
API-Aufruf, z. B. Art-Net-unabhängig über HTTP) hätte die Bewegungs-
Amplitude auf einen einzigen Punkt kollabieren lassen, während die FX
weiterhin als „laufend" gemeldet wird — sieht identisch zu einer
hängengebliebenen FX aus, ohne es zu sein. Jetzt an beiden Stellen auf
1–100 geklammert (Defense-in-Depth, wie im Rest des Projekts an
vergleichbaren Stellen bereits üblich).

### Bewusst nicht gefixt: Layout-Bug bei „MAX"-Reglern

„die beiden slider für MAX in movement fx sind größer als das layout
initial" — ohne Browser-Zugriff nicht sicher zu lokalisieren, welche
zwei Regler genau gemeint sind (Speed End/Size End in Movement FX? Der
neue gemeinsame „Max Speed"-Regler aus `JoystickAdvancedControls"?) und
was genau „größer als initial" beschreibt (denkbar: ein Timing-Effekt der
`Accordion`-Öffnen-Animation, die per CSS `max-height`-Transition
arbeitet). Statt zu raten und ggf. blind an der falschen Stelle zu
„fixen", als offener Punkt zurückgestellt — braucht entweder ein
Screenshot/genauere Beschreibung oder eigene Browser-Verifikation.

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`upload` + `uploadfs`), danach per
`curl` als online bestätigt. Ob sich der Shake jetzt tatsächlich wie eine
einstellbare Ramp anfühlt, ob er nach dem Stoppen wirklich aufhört, und
ob der Track-Force-Resend-Fix das gemeldete Verhalten behebt, kann nur
der User am echten Gerät bestätigen.

---

## 2026-08-17 (Fortsetzung) — Vier weitere Fixes, drei Punkte bewusst zur Rückfrage gestellt

Nächste Runde Live-Feedback, noch am selben Tag. Diesmal enthielt die
Nachricht sowohl klar diagnostizierbare Bugs als auch Punkte, bei denen
nach mehreren vorigen Guess-Runden eine Rückfrage sinnvoller war als ein
weiterer Blindschuss.

### Gefixt

1. **„Manual speed" zeigte irreführend „ms" an.** `dimSp`/`grSp`/`prSp`
   sind abstrakte 0–10000-Speed-Werte (höher = schneller), aber
   `TriggerBlock` (gemeinsam mit den echten Hold-Time-Reglern der
   StepFX-Chaser genutzt) hatte die Einheit `unit="ms"` hartcodiert — las
   sich wie eine Zeitdauer (höher = langsamer), also genau andersherum
   als die tatsächliche Bedeutung. Kein Formel-Bug, nur ein irreführendes
   Label. Neuer `holdUnit`-Prop (Default `"ms"`, für die drei
   Modulator-Stellen jetzt leer).
2. **Gobo-Chaser-Stop sollte auf den manuellen Setup-Wert zurückgehen,
   nicht auf die letzte Chaser-Position.** „wenn man die beiden gobo
   chaser stoppt, sollten die beiden wieder zurück gehen auf das was
   beim setup für ch 7 8 9 eingestellt ist." Der vorige Stop-Fix
   (`runStep()`s `wasActive`-Reset) landet auf `map[currentIdx]` — einem
   sicheren, aber für den User falschen Wert, wenn der manuelle Slider
   etwas anderes zeigt (z. B. „Open"). Neuer `mv`-Parameter an
   `/sgobfx`/`/rgobfx`: der Frontend sendet beim Stoppen den aktuellen
   `sgoboBase+sgoboOff`/`rgoboBase+rgoboOff`-Wert mit, das Backend
   schreibt ihn atomar in derselben Anfrage auf den Kanal — kein
   zweistufiges „erst falsch, dann kurz danach korrigiert" mehr.
   `runStep()`s eigener Fallback bleibt für Stop-Pfade ohne `mv`
   (z. B. `/kill_fx`) bestehen.
3. **Zu wenig Abstand zwischen den neuen Shake-Speed/-Range-Reglern.**
   Grid-Gap 6→16.
4. **Shake wirkte wie „eine Rampe über mehrere Gobo-Changes hinweg".**
   Die Oszillationsphase (`fmodf(now/1000 * speedHz, 1.0)`) war an die
   absolute Systemzeit gekoppelt statt an den jeweiligen Chase-Schritt —
   bei niedriger Speed war eine Schwingungsperiode länger als ein
   Hold-Intervall, wodurch die Welle unverändert über mehrere
   Gobo-Wechsel hinweg weiterlief, statt bei jedem neuen Gobo frisch (bei
   Phase 0) zu beginnen. Jetzt an `(now - fx.lastStepTime)` gekoppelt —
   `lastStepTime` wird bei jedem `doStep` zurückgesetzt, jedes Gobo
   bekommt einen konsistenten, eigenen Shake-Zyklus.

### Bewusst zur Rückfrage gestellt statt ein drittes Mal geraten

- **„Rotating gobo shake funktioniert nicht so gut, für speed und range,
  das ist irgendwie murksig" + „shake range scheint auch den speed zu
  beeinflussen".** Nach zwei Iterationen (fixer Offset in einer früheren
  Runde → einstellbare Sub-Zonen-Oszillation → jetzt der Phasen-Fix)
  weiterhin unbefriedigend. Das offizielle Handbuch
  (`mapping_sheds_160w_3in1_gobo.md`) beschreibt die Shake-Zonen nur als
  flache, unstrukturierte 5-DMX-Werte-Blöcke — anders als die
  Rotation-Zonen, die explizit als geschwindigkeits-gemappt beschrieben
  sind. Plausible Erklärung: das Fixture interpretiert die ganze
  Shake-Zone nur binär („Shake an", mit fester interner Rate), und jede
  Sub-Zonen-Variation, die meine Software sendet, hat am eigentlichen
  Fixture-Verhalten keinen dokumentierten, vorhersagbaren Effekt — mein
  Oszillationsmodell wäre dann grundsätzlich an der Realität vorbei
  geraten, nicht nur falsch kalibriert. Weiteres Feintuning ohne echte
  Hardware-Rückmeldung (z. B. ein manueller DMX-Sweep durch die Zone mit
  Beobachtung am Gerät) hat abnehmenden Grenznutzen — deshalb als
  Rückfrage an den User gestellt statt eines dritten Blindschusses.
- **„Mit curve/momentum 0 fährt der Fixture mit 1 Tick per Keyboard ca.
  11 Steps bei Max Speed 2000, so soll das nicht sein."** Bei
  `joyCurve≈0` ist `accelMul` laut Design immer `1.0` (keine Rampe,
  „sofort volle Geschwindigkeit" — explizit so von dieser Person in einer
  vorigen Runde gewünscht). Bei `joyMomentum=0` ist der Blend ebenfalls
  sofort `1.0` (Instant-Tracking). Ob 11 Steps bei einem sehr kurzen Tap
  und Max Speed 2000 eine inhärente, erwartete Konsequenz dieser
  „wirklich sofort, keine Bremse"-Kombination ist (ein Tap ist nie kürzer
  als ein Netzwerk-Roundtrip, und bei hoher Geschwindigkeit legt selbst
  ein kurzes Zeitfenster real messbare Strecke zurück) oder ein
  eigenständiger Bug, ließ sich aus dem Code allein nicht sicher
  unterscheiden.
- **„Der Stop beim Movement mit Momentum faded nicht sauber auf 0,
  sondern stoppt abrupt, so geht das nicht."** Die Momentum-Blend-Formel
  selbst (`blend = 1 - pow(1-smoothFactor, dt*30)`) ist seit dieser
  Session unverändert und mathematisch weiterhin plausibel (hohe
  Momentum-Werte ergeben langsame Konvergenz). Kein offensichtlicher
  Bug im Code gefunden. Zwei denkbare, nicht codeseitig unterscheidbare
  Erklärungen: (a) der jetzt gefixte Release-Burst-Bug aus einer
  vorigen Runde hat diese Beobachtung bisher überdeckt, oder (b) der
  User beobachtet den On-Screen-Joystick-Punkt (der über eine eigene,
  unabhängige ~180ms-Spring-Animation sofort auf die Mitte zurückspringt,
  unabhängig vom physischen Momentum-Wert) statt der tatsächlichen
  physischen Fixture-Bewegung. Braucht mehr Kontext vom User.

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`upload` + `uploadfs`), danach per
`curl` als online bestätigt.

---

## 2026-08-17 (Fortsetzung) — Joystick-Commit-Delay, Shake-Kalibrierung begonnen, NVS-Frage geklärt

Antwort auf die drei offenen Rückfragen aus der vorigen Runde. Der User
bestätigte für den Curve/Momentum-Punkt eine klare, unmissverständliche
Erwartung: „bei curve=0 muss tap minimal sein, auf jeden fall. eben
sofort mit speed=was jetzt ist losfahren, und beim loslass event sofort
hart stoppen (wenn momentum ebenfalls 0)." Für Shake: „da müssen wir mal
testen mit curl wie du sagst" — explizite Zustimmung zur gemeinsamen
Hardware-Kalibrierung statt weiterem Software-Raten. Zusätzlich eine neue
Frage: „werden die werte eigentlich im nvram gespeichert fürs nächste mal
und zwar für follower separat?" Movement-Stop-mit-Momentum bewusst offen
gelassen: „lassen wir erstmal offen, scheint iwie anders geworden zu
sein."

### Kurzer Tastatur-Tap bei Curve/Momentum=0 — Root Cause gefunden und gefixt

Mit der jetzt eindeutigen Erwartung (Halten = sofort volle Geschwindigkeit,
Tap = minimal) ließ sich die Ursache klar eingrenzen: Bei `joyCurve≈0`
gibt es laut Design absichtlich keine Rampe mehr (`accelMul` ist immer
`1.0`) — das bedeutet, die tatsächlich zurückgelegte Strecke ist direkt
proportional zu der Zeitspanne, in der das Backend `joyInputX != 0` sieht.
Diese Zeitspanne ist aber NICHT identisch mit der echten
Tastendruckdauer — sie wird durch reale, aber variable Netzwerk-Latenz
zwischen „Start-Befehl angekommen" und „Stop-Befehl angekommen"
aufgebläht. Bei Max Speed 2000 (≈50.000 Einheiten/Sekunde bei
`(joyMaxSpeed*25)`) reichen schon 20 ms Latenz für spürbare Strecke — und
20 ms liegen ohne weiteres im Bereich normaler lokaler WLAN-Latenz für
zwei aufeinanderfolgende HTTP-Requests.

Ohne jede Rampe (die früher, vor der expliziten „curve=0 = sofort"-
Anforderung, diese Latenz auf natürliche Weise absorbiert hätte) ist das
Zeitfenster zwischen Start- und Stop-Ankunft die einzige verbleibende
Bremse gegen ungewollte Kurz-Tap-Strecke — und dieses Fenster war bisher
komplett unkontrolliert (bestimmt allein durch Netzwerk-Timing, nicht
durch Software).

**Fix in `useKeyboardJoystick`:** neuer `commitTimerRef`. `startLoop()`
committet den ersten Bewegungsbefehl jetzt nicht mehr synchron beim
Tastendruck, sondern verzögert ihn um 15 ms (`setTimeout`). Wird die
Taste vor Ablauf dieser 15 ms wieder losgelassen, ruft `stopLoop()` den
Timer ab, **bevor** `apply()` je aufgerufen wurde — es wird also
überhaupt kein Bewegungsbefehl gesendet, kein Netzwerk-Traffic, keine
Bewegung. Bei einem echten, absichtlichen Tastendruck (die weit
überwiegende Mehrheit realer Interaktionen — 15 ms ist deutlich unter der
für Menschen wahrnehmbaren Reaktionsschwelle) verstreichen die 15 ms
unbemerkt, danach startet die Bewegung wie gewünscht sofort mit voller,
unrampter Geschwindigkeit. Betrifft nur den Tastatur-Pfad
(`useKeyboardJoystick`) — der Maus-/Touch-Joystick (`StickyJoystick`)
bewegt sich proportional zur tatsächlichen Zeigerposition/Drag-Distanz,
nicht binär auf/ab, und hat dieses spezifische Problem strukturell nicht.

Der zweite Teil der Anforderung („beim Loslassen bei Momentum=0 sofort
hart stoppen") war bereits durch die vorigen Runden abgedeckt: `sendJoy`s
Fastlane-Stop-Pfad liefert den Stop-Befehl ohne Debounce-Verzögerung, und
`joyMomentum=0` ergibt auf Backend-Seite bereits einen Blend-Faktor von
`1.0` (Instant-Snap von `joySmoothX` auf `0`, sobald der Stop-Befehl
ankommt) — keine weitere Änderung nötig, nur bestätigt.

### Gobo-Shake-Kalibrierung mit dem User begonnen

Statt einer dritten Software-Guess-Runde: `sgobFX`/`rgobFX` als inaktiv
bestätigt (`/api/get_dmx` → `sgA:0`, `rgA:0`), dann CH7 (statisches Gobo)
per `curl`/`/set_all?c7=N` manuell durch alle fünf möglichen Werte der
Gobo-1-Shake-Zone gefahren (`211, 212, 213, 214, 215`, je ~4 Sekunden
Haltezeit), anschließend zurück auf `White` (`c7=0`). User beobachtet
live am Fixture. Ziel: herausfinden, ob/wie das Fixture unterschiedliche
Werte innerhalb der schmalen 5-Werte-Zone tatsächlich unterschiedlich
interpretiert (feste Rate vs. wert-abhängige Rate), um die
Speed-/Range-Parameter aus der vorigen Runde auf eine echte
Datengrundlage zu stellen statt auf eine Annahme. **Auswertung der 5
Werte durch den User steht noch aus** — Fortsetzung in der nächsten
Session-Runde.

### NVS-Persistenz-Frage geklärt

„werden die werte eigentlich im nvram gespeichert fürs nächste mal und
zwar für follower separat?" — Antwort: `joySpeed`/`joyCurve`/
`joyMomentum` werden bereits persistiert (`WebAPI.h`, `/joy_cfg`-Handler,
`prefs.putInt("j_msp",...)`/`putFloat("j_crv",...)`/`putFloat("j_mom",...)`
unter dem `"sys"`-Namespace) — das war schon vor dieser Session so.
**Aber:** es ist ein einziger, globaler Wertesatz, nicht getrennt pro
Tab. Seit der Joystick-Controls-Vereinheitlichung (frühere Runde
desselben Tages) zeigen Live-, Programmer- und Followspot-Tab alle
denselben `JoystickAdvancedControls`-Block, der denselben globalen
`state.joySpeed`/`joyCurve`/`joyMomentum` liest und schreibt — es gibt
also faktisch nur ein gemeinsames Profil für alle drei Bewegungsarten,
kein separates Followspot-Profil. Ein echtes, eigenständiges
Followspot-Profil (z. B. für langsameres, präziseres Tracking) wäre ein
neues Feature — eigene Backend-Variablen, eigene NVS-Keys, eine
API-Unterscheidung, welches Profil gerade gilt — keine kleine Korrektur.
Bewusst nicht blind gebaut, sondern beim User rückgefragt, ob das
tatsächlich gewünscht ist.

### Movement-Stop mit Momentum — bewusst offen gelassen

User: „movement-stop lassen wir erstmal offen, scheint iwie anders
geworden zu sein." Keine Code-Änderung in dieser Runde — explizit
zurückgestellt auf Wunsch des Users, nicht vergessen.

### Verifikation

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Da in dieser Runde
nur `data/index.html` geändert wurde, genügte `pio run -t uploadfs` (kein
vollständiger Firmware-Reflash nötig) — auf dem angeschlossenen echten
Gerät ausgeführt, danach per `curl` als online bestätigt.

---

## 2026-08-17 (Fortsetzung) — Eigener Bug im Gobo-Chaser-Stop-Fix, per Screenshot belegt

User schickte einen Screenshot des Programmer-Tabs mit rot/gelb
markierten Bereichen: links die manuellen Setup-Dropdowns „Static gobo"
(CH7, „White (Open)") und „Rotating gobo" (CH8, „White (Open)"), rechts
die „Static Gobo Chaser"- und „Rotating Gobo Chaser"-Panels mit
konfiguriertem Bereich (Gobo 2→9 bzw. Gobo 1→6). Zitat: „wenn da im
chaser was eingestellt ist und ich stop drücke geht er nicht zurück auf
die werte die da links eingestellt sind." Das ist exakt das Verhalten,
das der `mv`-basierte atomare Stop-Restore aus einer vorigen Runde
eigentlich beheben sollte — offenbar tat er das nicht zuverlässig.

### Root Cause: zwei eigene Fixes bekämpften sich gegenseitig

Nachvollzogen anhand des Codes: `/sgobfx`/`/rgobfx` (`WebAPI.h`) schrieben
den `mv`-Wert korrekt in `dmxData[CH_GOBO]`/`dmxData[CH_GOBO_ROT]` — das
Problem lag nicht dort. Der Fehler steckte im Zusammenspiel mit
`runStep()`s eigenem, unabhängigem Stop-Reset aus einer noch früheren
Runde (die `wasActive`-Flankenerkennung, die beim Stoppen einmalig den
regulären Gobo-Wert des zuletzt gewählten Wheel-Index schreibt, gedacht
als Fallback für Stop-Pfade ohne bekannten manuellen Wert wie
`/kill_fx`). Diese beiden Mechanismen liefen **unabhängig voneinander**:
der HTTP-Handler setzte `dmxData[CH_GOBO] = mv` synchron beim Empfang der
Anfrage — aber `sgWasActive`/`rgWasActive` (die `runStep()` braucht, um
zu wissen, ob gerade eine Stop-Flanke vorliegt) waren zu diesem Zeitpunkt
noch `true`. Im allernächsten `updateEngines()`-Durchlauf (Hauptschleife,
läuft weit öfter als der 30ms-DMX-Sendetakt) sah `runStep()` also
`active=false` und `wasActive=true` — genau die Bedingung für seinen
eigenen Fallback — und überschrieb den gerade erst korrekt gesetzten
`mv`-Wert sofort wieder mit `map[currentIdx]`, der letzten
Chaser-Wheel-Position. Zwei für sich genommen korrekte Fixes aus zwei
verschiedenen Runden, die beide denselben Kanal beim Stoppen beschreiben
wollten, ohne voneinander zu wissen.

### Fix

`colWasActive`/`sgWasActive`/`rgWasActive` waren bisher `static`-
Lokalvariablen innerhalb von `updateEngines()` — für `WebAPI.h`
grundsätzlich unerreichbar, weshalb der `/sgobfx`/`/rgobfx`-Handler dem
`runStep()`-Fallback nicht mitteilen konnte „ich habe den Stop-Restore
bereits selbst erledigt, überspringen". Gefixt: die drei Flags zu
echten globalen Variablen gemacht, deklariert direkt bei den anderen
FX-Globals (`moveFX`, `dimFX`, `gRotFX`, `pRotFX`, `colFX`, `sgobFX`,
`rgobFX`) in `Moving_Head_Horizon.ino`, **vor** dem `#include
"WebAPI.h"` — genau das im Projekt etablierte Muster, warum dieser
Include bewusst so spät erfolgt (siehe `CLAUDE.md`: „Arduino
concatenates translation units, so include order = declaration order").
`/sgobfx`/`/rgobfx` setzen `sgWasActive`/`rgWasActive` jetzt explizit auf
`false`, sobald sie den `mv`-Restore selbst angewendet haben —
`runStep()`s Fallback greift dadurch nur noch bei Stop-Pfaden, die
keinen `mv`-Wert mitschicken.

### Verifikation — diesmal nicht nur „online", sondern das gemeldete Verhalten nachgestellt

Über `curl` das exakte gemeldete Szenario nachgebaut, nicht nur den
Server-Neustart geprüft: `/sgobfx?a=1&st=1&en=8&...` gestartet (Gobo
2–9), nach 2s CH7 abgefragt (`70`, ein gültiger Zwischenwert aus dem
laufenden Chase) — dann `/sgobfx?a=0&...&mv=0` gesendet (entspricht dem
manuellen Setup-Wert „White (Open)"). CH7 direkt danach: `0`. CH7 eine
weitere Sekunde später (um ein verzögertes Zurückkippen auszuschließen):
weiterhin `0`. Root Cause bestätigt behoben, nicht nur vermutet.

`pio run` `[SUCCESS]` (nur `.ino`/`.h` geändert, kein `buildfs` nötig).
Auf dem angeschlossenen echten Gerät per `pio run -t upload` geflasht.

---

## 2026-08-17 (Fortsetzung) — Gobo-Shake-Kalibrierung ausgewertet: Shake ist 5 diskrete Firmware-Stufen, nicht kontinuierlich

Fortsetzung der in der vorigen Runde begonnenen interaktiven
Hardware-Kalibrierung. Nachdem die 5 Werte der Gobo-1-Shake-Zone (CH7 =
211, 212, 213, 214, 215, je ~6s gehalten, User beobachtet live am
Fixture) durchgefahren wurden, meldete der User eine klare, unmissver­
ständliche Beobachtung: „wackelt links rechts aufsteigend speed gesteppt,
5 stufen, scheint ok."

### Bedeutung des Befunds

Das ist eine vollständige, eindeutige Antwort auf die Frage, die seit
zwei Software-Guess-Runden offen war: Die 5 DMX-Werte innerhalb einer
Shake-Zone sind **keine kontinuierliche Größe** (wie z. B. die
Rotation-Zonen, wo der Wert die Drehgeschwindigkeit stufenlos
skaliert), sondern **exakt 5 diskrete, aufsteigende Shake-
Geschwindigkeiten**, komplett von der Fixture-eigenen Firmware
gesteuert — Stufe 1 (kleinster Zonenwert) am langsamsten, Stufe 5
(größter Zonenwert) am schnellsten. Das bestätigt die in der vorigen
Runde geäußerte Vermutung („plausibel, dass das Fixture die ganze Zone
nur binär interpretiert... mit fester interner Rate") in einer präziseren
Form: nicht binär (an/aus), sondern 5-stufig — aber ebenso vollständig
von der Fixture selbst bestimmt, nicht von irgendeiner Software-seitigen
zeitlichen Modulation.

Das erklärt **beide vorigen, bis dahin unverstandenen Beschwerden**
rückwirkend exakt:

- **„Speed scheint invers zu sein":** Das vorige Software-Modell ließ
  den DMX-Wert kontinuierlich innerhalb der Zone oszillieren (eine
  Dreieckswelle, deren Frequenz der „Speed"-Parameter steuerte) — dabei
  durchlief die Fixture ständig alle 5 eingebauten Geschwindigkeiten in
  wechselnder Reihenfolge, unabhängig davon, in welche Richtung der
  Slider gedreht wurde. Was der User als „Speed" wahrnahm, war in
  Wirklichkeit ein chaotisches Springen zwischen allen 5 Firmware-Stufen
  gleichzeitig — daraus ließ sich keine konsistente Richtung ablesen,
  was sich wie eine zufällige oder gar inverse Beziehung anfühlen musste.
- **„Range beeinflusst auch Speed":** Der `range`-Parameter bestimmte die
  Amplitude der Oszillation (wie weit sie von der langsamsten Stufe aus
  in Richtung der schnelleren Stufen reicht). Bei niedrigem Range blieb
  die Oszillation nahe an Stufe 1 (durchgängig langsam), bei hohem Range
  wurden auch die schnelleren Stufen erreicht (im Mittel spürbar
  schneller) — Range hatte also durchaus einen echten Effekt auf die
  wahrgenommene Geschwindigkeit, nur eben nicht den beabsichtigten
  (Amplitude), sondern einen unbeabsichtigten Nebeneffekt der falschen
  Grundannahme (Oszillation statt Stufenwahl).

### Neu gebaut: Speed wählt jetzt direkt eine von 5 Firmware-Stufen, Range entfällt komplett

Mit der jetzt gesicherten Datengrundlage vollständig neu implementiert,
statt weiter am alten Oszillationsmodell zu feilen:

- **`StepFX::scratchSpeed`** (`FX_Engine.h`) ist nicht mehr ein
  Hz-Wert für eine Oszillation, sondern direkt die gewünschte Stufe
  (`int`, `1`–`5`, Default `3`).
- **`runStep()`** (`Moving_Head_Horizon.ino`) berechnet den Shake-Wert
  jetzt als `shakeBase + (gobo_index - 1) × 5 + (stufe - 1)` — ein
  einziger, fest gehaltener DMX-Wert, keine `fmodf`/Dreieckswellen-
  Berechnung mehr, kein Bezug zu `now`/`lastStepTime` für die
  Shake-Berechnung selbst (die Gobo-Wechsel-Logik über `doStep`/
  `holdTime` bleibt davon unberührt).
- **`scratchRange` vollständig entfernt** — als Feld aus `StepFX`, als
  Query-Parameter `rng` aus `/sgobfx`/`/rgobfx` (`WebAPI.h`), als
  `sgRng`/`rgRng`-Feld aus dem `/api/get_dmx`-JSON, und als kompletter
  „Shake range"-Regler samt State/Sync aus dem Frontend (`data/
  index.html`). Es gibt kein reales Gegenstück mehr dazu, an dem ein
  Regler etwas Sinnvolles tun könnte.
- **„Shake speed"-Regler im Frontend** auf `min=1 max=5 step=1`
  umgestellt (vorher ein 1–100-Regler, der intern durch 10 geteilt eine
  Hz-Zahl ergab) — zeigt jetzt direkt die Stufe, kein Umrechnungsfaktor
  mehr nötig (`spd=${s.sgSp}` statt `spd=${(s.sgSp/10).toFixed(1)}`).

### Verifikation

Live per `curl` bestätigt, nicht nur kompiliert: `/sgobfx?...&sc=1&spd=1`
(Gobo 1 fixiert) → `CH7 == 211` (Stufe 1, exakter Zonenanfang). Danach
`spd=5` → `CH7 == 215` (Stufe 5, exaktes Zonenende). Beide Werte trafen
exakt die berechnete Formel.

`mapping_sheds_160w_3in1_gobo.md` (die Fixture-Referenzdatei) wurde direkt
mit dem bestätigten Befund aktualisiert — der „offener Punkt"-Eintrag zum
Shake-Verhalten ist jetzt als gelöst markiert, mit dem genauen
User-Zitat als Beleg. `pio run` und `pio run -t buildfs` beide
`[SUCCESS]`, auf dem angeschlossenen echten Gerät geflasht (`upload` +
`uploadfs`).

---

## 2026-08-17 (Fortsetzung) — Idee für einen selbstgebauten, stufenlosen Shake live geprüft und verworfen

Direkt im Anschluss an die erfolgreiche 5-Stufen-Shake-Kalibrierung kam
ein cleverer Vorschlag vom User: „du könntest ja theoretisch einen
eigenen shake bauen, da wir das rad ja links/rechts drehen können und
der speed da stufenlos ist... CH7 und CH8 bieten das doch an oder nicht?
dafür gibts ja nen slider wo man den speed links/rechts einstellen
kann." Gemeint: CH7 hat laut Handbuch zwei stufenlos
geschwindigkeits-gemappte Rotationszonen (`100–129` CW, `135–210` CCW,
„Scroll" laut `.d4`-Datei) — im Gegensatz zur 5-stufigen Shake-Zone. Die
Idee: schnell zwischen einem kleinen CW- und CCW-Wert hin- und
herschalten, um einen eigenen, weicheren Shake mit frei wählbarer
Geschwindigkeit *und* Stärke zu bauen — genau das, was sich der User von
Anfang an unter „Shake Ramp" vorgestellt hatte.

### Analyse vor dem Bauen

Kanalabgleich: CH7 hat tatsächlich beide Richtungen (symmetrisch nutzbar).
CH8 hat laut Handbuch **nur** `70–129` CW, keine dokumentierte
Gegenrichtung — für ein echtes Wackeln am Rad 2 hätte stattdessen CH9
(die eigene Index-/Rotationsachse von Rad 2, `64–192` CW / `193–255`
CCW) genutzt werden müssen, was aber mit der bereits existierenden
„Rotation FX" (`gRotFX`) kollidiert wäre, die genau diesen Kanal auch
ansteuert.

Wichtiger als die Kanalfrage: unklar war, ob „Rotation/Scroll" bei diesem
Fixture bedeutet „dasselbe Gobo dreht sich an Ort und Stelle" (das würde
den Plan tragen) oder „das ganze Rad spinnt durch und man sieht dabei
die Nachbar-Gobos durchlaufen" (das würde den Plan kaputt machen — ein
CW/CCW-Alternations-„Shake" sähe dann wie wildes Vor-und-Zurück-Spinnen
durch mehrere Motive aus, nicht wie ein sauberes Wackeln). Das Handbuch
beantwortet das nicht.

### Live-Test statt Bauen auf Verdacht

Statt den ganzen Oszillator zu bauen und erst danach am echten Gerät zu
scheitern (wie schon zweimal beim ursprünglichen Shake-Modell), erst
verifiziert: Gobo 1 ausgewählt (`c7=15`), 4s gehalten, dann auf einen
niedrigen CW-Rotationswert gewechselt (`c7=105`), 7s gehalten, User
beobachtet live. Ergebnis: „ist nur durchgelaufen, ganz am ende ganz
schnell ansonsten constant speed" — die Rotation-Zone dreht das gesamte
Rad kontinuierlich durch verschiedene Gobo-Motive, sie wackelt **nicht**
das aktuell gewählte einzelne Gobo an Ort und Stelle.

### Ergebnis: nicht weiterverfolgt

Die Idee ist damit widerlegt, nicht nur ungebaut liegen gelassen — ein
selbstgebauter Shake über CW/CCW-Alternation auf der Rotation-Zone würde
technisch funktionieren, aber optisch das Gegenteil eines sauberen
Wackelns liefern. Der bereits gebaute, fixture-native 5-Stufen-Shake
bleibt der einzige saubere Weg zu einem echten Einzel-Gobo-Wackeln auf
diesem Fixture. Keine Code-Änderung in dieser Runde — reine Recherche,
die eine potenziell aufwendige Fehlimplementierung verhindert hat. In
`mapping_sheds_160w_3in1_gobo.md` festgehalten (neuer Abschnitt direkt
bei der CH7-Shake-Dokumentation), damit die Idee nicht ohne Grund erneut
aufkommt.

---

## 2026-08-17 (Fortsetzung) — Rotation-Pulse-Shake für CH7 doch gebaut: die Testtechnik war falsch, nicht die Idee

Direkt im Anschluss an das „nicht weiterverfolgt"-Fazit widersprach der
User berechtigt: „ne, du musst quasi ein wert aus 100-129 und 135-210
abwechselnd senden, dann hast du dein shake. 135 ist langsam eine
richtung, aufsteigend schneller, 129 ist langsam andere richtung,
absteigend schneller. keine drehung ist 130... idealerweise müssten wir
auf dem gobo mittig bleiben und shaken." Der vorige Test hatte eine
**gehaltene Dauerrotation** geprüft (6+ Sekunden auf einem festen
Rotationswert) — das ist etwas fundamental anderes als **kurze,
abwechselnde Pulse** zwischen beiden Richtungen, die nie lange genug in
eine Richtung laufen, um das Nachbar-Gobo zu erreichen. Die ursprüngliche
Idee war also nicht falsch, nur der erste Test hatte die falsche
Technik geprüft.

### Zonen-Verständnis korrigiert

Wichtige Präzisierung vom User zur Geschwindigkeitsrichtung innerhalb
der Zonen: `130` = keine Drehung (Stop), `129` = langsamste
Uhrzeigersinn-Drehung (Geschwindigkeit steigt, je weiter man sich in
Richtung `100` von `130` entfernt), `135` = langsamste
Gegen-Uhrzeigersinn-Drehung (Geschwindigkeit steigt Richtung `210`).
Also: Geschwindigkeit nimmt mit dem **Abstand vom Stop-Wert** zu, nicht
mit dem absoluten DMX-Wert selbst — sinnvoll, da der Stop-Bereich
(`130–134`) mittig zwischen beiden Rotationszonen liegt.

### Korrigierter Live-Test vor dem Bauen

Statt sofort loszubauen, erst die richtige Technik verifiziert: Gobo 6
fest ausgewählt (`CH7=60`), dann `CH7` alternierend zwischen `129`
(langsamste CW) und `135` (langsamste CCW) gesetzt, ~150ms pro Richtung,
über ~6 Sekunden. User beobachtete live: „wackelt links rechts
aufsteigend..." (erste Iteration, bevor der Test versehentlich als
Dauerrotation missverstanden wurde) — nach Klarstellung der Technik im
zweiten Anlauf mit Re-Anchor zwischen den Pulsen: „gobo scheibe pendelt
links rechts langsam" — die Technik funktioniert, das Gobo pendelt an
Ort und Stelle statt zum Nachbarn zu wandern.

Der curl-basierte Test selbst lief deutlich langsamer als erwartet (der
User bemerkte: „es läuft schon seit ewigkeiten") — das lag ausschließlich
an der Testmethode selbst (jeder curl-Aufruf ist ein einzelner externer
HTTP-Roundtrip übers WLAN, nicht an der eigentlichen Technik), nicht an
einem Problem der späteren Firmware-Implementierung, die intern ohne
Netzwerk-Overhead läuft.

### Implementierung

`StepFX` (`FX_Engine.h`) bekommt `scratchRange` zurück (war in der
vorigen Runde entfernt worden, jetzt mit neuer, echter Bedeutung:
0–100% Intensität für CH7) und `scratchSpeed` wechselt von einem
diskreten 1–5-Stufenwert zurück zu einem kontinuierlichen Hz-Float
(0,2–10,0) — beide Felder haben jetzt, abhängig vom Kanal, zwei
unterschiedliche, dokumentierte Bedeutungen (siehe ausführlicher
Kommentar direkt am Feld).

`runStep()` (`Moving_Head_Horizon.ino`) bekommt einen neuen
`rotationPulse`-Parameter. Wenn aktiv (nur `sgobFX`/CH7): Vier-Phasen-
Zyklus über eine volle Periode (`1/scratchSpeed` Sekunden, per
`fmodf(now/1000.0f, period)` bestimmt) — CW-Puls → Index-Re-Anchor →
CCW-Puls → Index-Re-Anchor, je ein Viertel der Periode. CW-/CCW-Werte
werden aus `scratchRange` berechnet: `129 - intensity*29/100` (CW, näher
an `100` bei hoher Intensität) bzw. `135 + intensity*75/100` (CCW, näher
an `210` bei hoher Intensität). Der Index-Re-Anchor-Wert ist einfach
`map[currentIdx]` — der reguläre, gerade gewählte Gobo-Wert, exakt wie
vom User vorgeschlagen. `rgobFX`/CH8 bleibt unverändert beim
fixture-nativen 5-Stufen-Shake aus der vorigen Runde (`rotationPulse=
false`) — CH8 hat laut Handbuch keine dokumentierte Gegenrichtung auf
sich selbst, eine CH9-basierte Alternative würde mit der bereits
existierenden Rotation FX kollidieren (beide würden denselben Kanal
beschreiben wollen). Ausführlich im `StepFX::scratchSpeed`-Kommentar in
`FX_Engine.h` begründet.

Frontend (`data/index.html`): `ChaserFx` zeigt jetzt je nach Wheel
unterschiedliche Regler — „Shake speed" (Hz) + „Shake range" (%) für
Static Gobo, nur „Shake speed" (Stufe 1–5) für Rotating Gobo, gesteuert
über `cfg.rngKey` (nur bei `sg` gesetzt). `/sgobfx` bekommt einen neuen
`rng`-Parameter, `/api/get_dmx` exponiert `sgRng` wieder.

### Verifikation — echte Firmware, kein externer curl-Loop mehr für den Effekt

Nach dem Flash Start/Stop direkt gegen die echte Firmware getestet (der
Shake-Zyklus selbst läuft jetzt intern in `updateEngines()`, keine
externen curl-Aufrufe pro Puls mehr nötig): `/sgobfx?a=1&st=6&en=6&sc=1&
spd=3.0&rng=40` gestartet, User bestätigte live: „es wackelt und pendelt
overlaying minimal links/rechts. nicht 100% smooth aber geht" — die
leichte Unrundheit ist plausibel durch die Index-Re-Anchor-Phasen erklärt
(der Motor muss zwischen Rotations-Kommando und Index-Seek-Kommando
kurz „umschalten"), aber die Kernfunktion (Pendeln statt Wandern) ist
bestätigt. Stop mit `mv=60`: `sgA` wird `0`, CH7 bleibt sauber bei `60`
(Gobo 6), kein Zurückspringen auf White.

### Nebenbefund während des Live-Tests: Mehrfach-Client-Sync-Konflikt

Während des Tests sprang `sgA` einmal unerwartet auf `0` zurück, ohne
dass ein Stop-Kommando gesendet wurde — dabei blieben `sgSp`/`sgRng`
korrekt auf den gesetzten Werten (`3.00`/`40`), was gegen einen
Geräte-Reboot spricht (ein Reboot hätte auch `dmxData[1]` (Dimmer) auf
den Boot-Default `0` zurückgesetzt, tatsächlich stand dort aber ein
realer, von-Null-verschiedener Wert). Wahrscheinlichste Erklärung: ein
parallel offener Browser-Tab mit der Web-UI, dessen eigener, noch nicht
aktualisierter React-State (`sgFxRunning`) beim nächsten Sync-Zyklus
einen `a=0`-Befehl gesendet hat — genau das bereits in `backlog.md`
unter „Bekannte kleine Issues" dokumentierte Mehrfach-Client-Sync-
Problem, kein neuer Bug. Nach Schließen aller Browser-Tabs lief der
zweite Testdurchlauf ohne dieses Symptom durch.

### Nächster möglicher Schritt, bewusst zurückgestellt

User fragte direkt im Anschluss, ob jetzt auch Speed-/Intensitäts-Rampen
über die Zeit möglich wären („wa-wa-wosh", sanftes Anlaufen, kurzes
Aufdrehen, bissi Shake und dann Superspeed-Wechsel). Technisch gut
machbar mit derselben Modulator-Technik, die bereits bei Dimmer-FX und
Rotation FX für Mode/Curve/Speed verwendet wird — bewusst nicht in
derselben Runde zusätzlich gebaut, um die gerade frisch verifizierte
Basis (Start/Stop/Pendeln) erst zu dokumentieren und zu sichern, bevor
eine weitere Komplexitätsschicht draufkommt.

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`upload` + `uploadfs`).

## 2026-08-18 — Rotation-Pulse-Shake nachgeschärft (Amplitude, Gobo-Übergänge) + Stop-Race im Frontend gefixt

Nach dem ersten Live-Test der neuen Rotation-Pulse-Shake-Technik (siehe
2026-08-17 oben) kam echtes UI-Feedback mit drei getrennten,
unterscheidbaren Problemen zurück:

> „shake ist zu groß für langsame speeds, da rollt der gobo raus. beim
> gobo wechsel sollte der shake nicht laufen, sonst sieht das choppy
> aus. aber ansonsten geht es gut. was auffällt, wenn man stop drück
> sowohl static gobo als auch rot gobo, er springt dann manchmal nach 1
> sekunden wieder auf run. wenn man danach stop drückt geht er auch
> wieder auf open bzw,. das was links eingestellt ist. will sagen,
> manchmal nimmt er das stop async wohl nicht an."

### Fix 1 — Puls-Dauer von der Shake-Speed entkoppelt

Root Cause: die bisherige Formel leitete die Pulsdauer direkt aus der
Periode ab (`quarter = period/4`). Bei niedrigem `scratchSpeed` (Hz)
wird die Periode lang, also war auch der einzelne CW-/CCW-Puls lang —
mehr gehaltene Rotationszeit bei gleicher Zonen-Intensität bedeutet mehr
Winkel-Drift, genug um über die Mitte des aktuellen Gobos hinaus zum
Nachbarn zu wandern. Das war exakt das gemeldete „rollt raus" bei
niedrigen Speeds.

Fix in `runStep()` (`Moving_Head_Horizon.ino`): Pulsdauer ist jetzt eine
feste Konstante (`FIXED_PULSE_S = 0,05s`/50ms), gedeckelt auf höchstens
die Hälfte der Halbperiode (damit sich bei sehr hohen Speed-Werten die
beiden Pulse einer Halbperiode nicht überlappen). `scratchSpeed`
bestimmt jetzt nur noch, wie viel Ruhezeit zwischen den Pulsen liegt
(den Rhythmus), nicht mehr, wie weit ein einzelner Puls dreht — die
Amplitude pro Puls bleibt dadurch bei jeder Speed-Einstellung gleich
beschränkt.

**Live per curl verifiziert:** Chaser mit `spd=0.3` (0,3 Hz, sehr
langsam), `rng=60` (60% Intensität) auf Gobo 5 (`CH7`-Anker = 50)
gestartet, danach 35× im 100ms-Takt `CH7` abgefragt. Ergebnis: 34 von 35
Samples zeigten den Anker-Wert `50`, ein einzelnes Sample traf mitten in
einen Puls und zeigte `112` — exakt der erwartete, intensitätsproportionale
CW-Pulswert (`129 - 60×29/100 = 112`), keine sonstigen Zwischenwerte, kein
Driften. Bestätigt: der Wechsel sitzt fast durchgehend auf dem Anker und
nur kurz auf einem klar begrenzten Pulswert, statt lange in der
Rotationszone zu verweilen.

### Fix 2 — Settle-Fenster nach Gobo-Wechsel

Reported: „beim gobo wechsel sollte der shake nicht laufen, sonst sieht
das choppy aus." Root Cause: die Shake-Berechnung kannte bisher nur die
absolute Zeit (`now`/`millis()`), nicht aber, ob gerade eben ein
Gobo-Schritt (`doStep`) stattgefunden hat — der Shake konnte also mitten
in einen frischen Wechsel hineinlaufen.

Fix: neue Konstante `SHAKE_SETTLE_MS = 220` in `runStep()`, prüft
`now - fx.lastStepTime` (der Zeitstempel wird bei jedem `doStep` ohnehin
schon aktualisiert) und unterdrückt für die ersten 220ms nach jedem
Schritt sowohl den Rotation-Pulse-Zyklus (CH7) als auch den nativen
CH8-Shake-Fallback — in diesem Fenster liefert `runStep()` einfach den
planen, nicht-shakenden Gobo-Wert. Danach setzt der Shake normal wieder
ein.

### Fix 3 — Stop-Race für Gobo-Chaser im Frontend

Reported: Stop-Druck (sowohl Static- als auch Rotating-Gobo-Chaser)
„springt dann manchmal nach 1 sekunden wieder auf run... nimmt er das
stop async wohl nicht an." Erst per curl gegen das Backend direkt
getestet, um Frontend- von Backend-Ursache zu trennen: Start → Stop mit
`mv=60` → Status sofort und noch einmal ~1,6s später abgefragt, beide
Male sauber `sgA:0, CH7:60`, kein Zurückspringen. Der bereits
implementierte atomare `mv`-Stop-Restore (siehe 2026-08-17 oben) war
serverseitig also die ganze Zeit korrekt — das gemeldete Verhalten war
demnach eine reine Frontend-Race, nicht im Backend zu finden.

Fix in `data/index.html`: neue Hilfsfunktion `tFetchImmediate(url, id)`
— dasselbe Bypass-Muster wie `sendJoy`s Stop-Sonderfall (siehe
`history.md`, Joystick-Commit-Delay-Fix). Der State-Sync-Effekt für
`sgFxRunning`/`rgFxRunning` unterscheidet jetzt Start/laufende Änderung
(weiter über die normale `tFetch`-Debounce-Queue) von Stop (`a=0`,
umgeht die Queue komplett über einen direkten `fetch()` und leert
`tfPending['sgfx']`/`['rgfx']`). Vorher verließ sich der Stop-Fall
ausschließlich auf das 2,5s-`dirtyUntilRef`-Schutzfenster gegen
überschreibende Poll-Antworten — laut gemeldetem Verhalten offenbar
nicht zuverlässig genug, wenn der Stop-Request selbst hinter einer noch
laufenden Debounce-Cooldown der vorherigen Start-Anfrage feststeckte.

**Noch nicht vom User selbst bestätigt** (curl kann UI-Timing und
physisches Pendeln nicht beurteilen): ob die Amplitude bei niedriger
Speed jetzt tatsächlich am Gobo bleibt, ob der Gobo-Wechsel jetzt sauber
statt choppy wirkt, und ob der Stop im Browser jetzt zuverlässig beim
ersten Versuch greift.

### Nebenbefund: Gerät nach dem Flashen vorübergehend "unerreichbar" — falscher Alarm

Nach dem Flashen von Firmware + Filesystem war das Gerät für mehrere
Minuten weder per `movinghead.local` noch per direktem `curl` erreichbar.
Diagnoseversuch: `pio device monitor` schlug in dieser Sandbox-Shell
fehl (kein TTY für `termios`), ein direkter Zugriff auf den seriellen
Port per `pyserial` lieferte zunächst gar keine Bytes. Ein manueller
DTR/RTS-Reset-Versuch zur Diagnose landete das Gerät kurzzeitig im
ROM-Bootloader-Downloadmodus (`rst:0x15 ... boot:0x5 DOWNLOAD`) —
harmlos, durch erneutes `pio run -t upload` (das den Chip über
`esptool`s eigene, bewährte Reset-Routine wieder in den normalen
Programmlauf zurückholt) behoben. Zusätzliche Erkenntnis dabei: bei
deaktiviertem „USB CDC on Boot" (wie hier per Projektkonvention immer)
trägt der USB-Port ausschließlich die ROM-Bootloader-Konsole — die
Sketch-eigenen `Serial.print()`-Ausgaben laufen stattdessen über die
UART0-Pins, nicht über USB. Serielles Monitoring des laufenden Sketches
ist von dieser Sandbox aus daher grundsätzlich nicht möglich; nur der
ROM-Bootloader ist sichtbar.

Am Ende stellte sich heraus: Das Gerät lief die ganze Zeit einwandfrei
(Neustart nach beiden Flashs erfolgreich, korrekter, sinnvoller
Non-Default-Status) — nur unter seiner festen LAN-IP `192.168.8.113`
statt per `movinghead.local` erreichbar. Die mDNS-Auflösung war schlicht
wieder flakey, dasselbe bereits am 2026-08-17 beobachtete und dort per
`backlog.md` dokumentierte Muster. Kein Firmware-Bug, keine Auswirkung
der neuen Shake-Änderungen auf Boot/WLAN.

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Auf dem
angeschlossenen echten Gerät geflasht (`upload` + `uploadfs`), Backend-
Verhalten (Fix 1 Amplitude, Fix 3 atomarer Stop-Restore) live per `curl`
bestätigt. Fix 2 (Settle-Fenster) und die Browser-seitige Hälfte von
Fix 3 sind code-verifiziert, aber noch nicht am echten Gerät/UI vom User
gegengeprüft.

## 2026-08-18 (Fortsetzung) — Start/Stop-Race und Kanal-Clobber für alle FX-Typen geprüft und gefixt

Direkt im Anschluss an den sg/rg-Stop-Race-Fix meldete der User denselben
Symptomtyp auch für andere FX, verbunden mit einer Bandbreiten-Beobachtung:
„dimmer fx updated auch die controls in CH1, genauso wie color fx die
fields in CH6 updated. das frisst m.E. Bandbreite. ich habe nämlich das
Problem, dass wenn ich z. B. start/stop drücke, er manchmal einfach wieder
zurück springt oder die Änderungen nicht annimmt. Prüfe das für alle FX,
weil das wird ja ein größeres Problem sein.“

**Root-Cause-Recherche** (kein Rätselraten, jede Komponente einzeln
gelesen, bevor etwas geändert wurde):

1. `data/index.html` per grep auf `tFetch`/`dirtyUntilRef`/`isLocalDirty`
   durchsucht: bestätigt, dass `fx` (Movement), `dimFx`, `grFx`, `prFx`,
   `colFx` und `chaser` beim Stop noch alle die alte debounced
   `tFetch`-Queue nutzten — exakt dasselbe Muster, das für sg/rg schon als
   Race identifiziert und per `tFetchImmediate`-Bypass gefixt worden war.
2. `track()` und seine Aufrufstellen (Zeilen ~1550–1581) gelesen: die
   bestehende `skip`-Logik (unterdrückt `/set_all`-Sync für FX-eigene Kanäle
   während die FX läuft) ist bereits korrekt und musste nicht angefasst
   werden.
3. Per grep die Poll-Merge-Zeilen gefunden: `next.dimmer`, `next.goboRot`,
   `next.prismRot` und `next.colorBase` (aus `colorEntry`) wurden bei
   *jedem* Poll (alle 2 s) unconditional aus dem Live-DMX-Wert
   übernommen — unabhängig davon, ob die zugehörige FX gerade lief. Das
   erklärt exakt die gemeldete Beobachtung („Regler wackelt mit der FX
   mit“) und zusätzlich, warum manuelle Reglerbewegungen *während* eine FX
   lief, von der nächsten Poll-Antwort wieder überschrieben werden konnten,
   bevor der User überhaupt Stop gedrückt hatte.
4. `WebAPI.h` (`/fx`, `/modfx`, `/colfx`, `/chaser`) gelesen: keiner dieser
   vier Handler hat einen `mv`-artigen atomaren Stop-Restore-Parameter wie
   `/sgobfx`/`/rgobfx`.
5. `FX_Engine.h` (`Modulator::stop()`, `MovementEngine::stop()`) gelesen:
   beide setzen nur `active = false`, schreiben selbst keinen DMX-Kanal.
6. `updateEngines()` in `Moving_Head_Horizon.ino` gelesen (Zeilen ~313–333):
   hier lag der eigentliche, unerwartete Fund — **kein Race, sondern ein
   echter Backend-Bug:**
   - `gRotFX`/`pRotFX` (CH9/CH11): `else if (gRotWasActive) { dmxData[9] =
     0; ... }` — beim Stop wurde der Kanal hart auf **0** gesetzt, nicht auf
     den manuellen Programmer-Wert. Bestätigt der Sache nach exakt das
     gemeldete „Änderungen werden nicht angenommen“ für Gobo-Rotation/
     Prism-Rotation-FX.
   - `dimFX`: `dimFX.process(..., dimSmoothTarget)` schreibt bei jedem
     aktiven Tick direkt in `dimSmoothTarget` (per Referenz) — die
     eigentliche „Zielgröße“, auf die `dimSmoothCurrent` beim Stop
     zurückgleitet. Nach einem Stop blieb `dimSmoothTarget` also auf dem
     letzten LFO-Wert stehen, nicht auf dem manuellen Wert, bis ein
     separater `/set_all`-Aufruf ihn korrigierte — und genau dieser
     Korrektur-Aufruf konnte durch die Debounce-Race (Punkt 1) zu spät
     ankommen, während `dimFX.active` backend-seitig noch `true` war und
     `dimSmoothTarget` pro Tick weiter überschrieb.
   - `moveFX` (Pan/Tilt), `colFX` (Farbrad) und `chaserActive` hatten dieses
     Clobber-Problem nicht: Movement restauriert Pan/Tilt jeden Frame direkt
     aus `centerPan16`/`centerTilt16` (Zeilen 292/305), `colFX` nutzt
     bereits denselben `wasActive`→`map[currentIdx]`-Fallback wie sg/rg.

**Gebaut:**
- `Moving_Head_Horizon.ino`: `gRotFX`/`pRotFX`-Stop-Zweige entfernen das
  harte `dmxData[9|11] = 0` ersatzlos — der Kanal bleibt unangetastet, bis
  `/modfx`s eigener `mv`-Restore (oder der nächste `track()`-erzwungene
  `/set_all`) ihn korrekt setzt.
- `WebAPI.h`: `/modfx` akzeptiert jetzt einen optionalen `mv`-Parameter
  (analog `/sgobfx`/`/rgobfx`). Beim Stop: für `pfx=gr`/`pr` wird
  `dmxData[9]`/`dmxData[11]` direkt gesetzt; für `pfx=dim` wird stattdessen
  `dimSmoothTarget` gesetzt (da `updateEngines()` darüber restauriert, nicht
  über `dmxData[CH_DIMMER]` direkt).
- `data/index.html`:
  - Alle sechs verbliebenen Stop-Übergänge (`fx`, `dimFx`, `grFx`, `prFx`,
    `colFx`, `chaser`) nutzen jetzt denselben `tFetchImmediate`-Sofort-
    Bypass wie zuvor nur sg/rg — Stop-Kommandos umgehen die
    `tFetch`-Debounce-Queue komplett.
  - `grFx`/`dimFx`/`prFx` senden dabei zusätzlich `mv=<manueller Wert>`
    mit, damit der neue Backend-Restore sofort den richtigen Zielwert
    bekommt.
  - Der Poll-Merge überschreibt `dimmer`/`goboRot`/`prismRot`/`colorBase`
    jetzt nur noch, wenn die jeweils zugehörige FX (`dimFxRunning`/
    `grFxRunning`/`prFxRunning`/`colFxRunning`) *nicht* läuft.

**Live per curl verifiziert** (Start → Stop mit `mv=<Wert>` → 2 s später
erneut geprüft, jeweils für CH1/CH9/CH11): Kanal landet sofort auf dem
`mv`-Wert und bleibt stabil dort — kein Rückfall auf 0, kein Rückfall auf
den letzten FX-Wert. `pio run` und `pio run -t buildfs` beide `[SUCCESS]`,
auf dem echten Gerät geflasht (`upload` + `uploadfs`), Gerät danach über
`192.168.8.113` erreichbar und alle FX-Flags nach `/kill_fx` sauber auf 0.
Browser-/Hardware-seitige Live-Bestätigung durch den User steht noch aus.

## 2026-08-18 (Fortsetzung) — Dimmer-Speed-Einheit, Beat-Sync-Feel, HW-Mic-Programmer-Button

Drei neue User-Meldungen im direkten Anschluss an den vorigen Stop-Race-
Fix, alle einzeln root-caused (kein Raten):

**1. „manual speed beim dimmer sollte milliseconds, ist es aktuell
nicht."** Bestätigt: Der Slider für Dimmer-/Gobo-Rotation-/Prism-
Rotation-FX ("Manual speed", `holdUnit=""`, Bereich 0–10000, Step 100)
sah wie eine ms-Eingabe aus, war es aber nicht. `Modulator::process()`
in `FX_Engine.h` berechnete `phase += (speed/2000.0f) * dt * 2.0f`,
also eine tatsächliche Zyklusdauer von `1.000.000/speed` ms — ein
inverser Kehrwert, kein literaler ms-Wert (größere Zahl = schneller,
nicht "mehr Millisekunden"). Gefixt: `phase += (dt*1000.0f) / periodMs`
mit `periodMs = max(speed, 1.0f)` — `speed` ist jetzt buchstäblich die
volle LFO-Zyklusdauer in ms, exakt wie der Slider es zeigt. Gilt
einheitlich für `dimFX`/`gRotFX`/`pRotFX`, da alle dieselbe `Modulator`-
Klasse nutzen. Frontend: `holdUnit=""` entfernt (jetzt Standard "ms")
für alle drei Controls.
- **Live per curl verifiziert:** `dimFX` gestartet mit `sp=1000`,
  `mo=0` (Forward/Saw), `cu=0` (Linear) — CH1 alle ~290ms gesampelt und
  die Phasenposition aus dem Wert zurückgerechnet (`value/0.255` =
  ms-Position im Zyklus). Über 5 Samples stimmte die berechnete
  Phasenposition exakt mit einer 1000ms-Periode überein (inkl. zweier
  beobachteter Wraps genau an den erwarteten Zeitpunkten) — kein
  Timing-Zufall, sondern exakte Übereinstimmung mit `speed=1000` →
  1000ms Periode.

**2. „ausserdem ist der sync auf beat nicht sauber, mainly ist das
licht iwie aus anstatt an."** Root-Cause gefunden (kein Bug in der
Mathematik, sondern ein ungünstiger Default): Bei BPM-Sync
(`trigger==1`) ist `phase` exakt 0 an jedem Beat und läuft bis kurz vor
dem nächsten Beat auf 1 hoch. Mit dem bisherigen Default `mode=0`
(Forward) + `curve=3` (Sine) bedeutet das: **dunkel exakt auf dem Beat**,
hell kurz VOR dem nächsten Beat, dann Sprung zurück auf dunkel — das
Gegenteil vom intuitiven "Licht flasht AUF den Beat". Da dies der
ausgelieferte Default war (`dimMo: 0` im Frontend, `mode=0` in der
`Modulator`-Klasse) und niemand das Mode-Dropdown angefasst haben muss,
um dieses Verhalten zu sehen, ist das der wahrscheinliche Grund für
"Licht eher aus als an". Fix: Default auf `mode=2` (Reverse/Decay)
geändert — sowohl in der `Modulator`-Klasse (`FX_Engine.h`) als auch im
Frontend-Startzustand (`dimMo: 2`). Damit ist die Helligkeit bei
`phase=0` (also exakt auf dem Beat) maximal und fällt bis zum nächsten
Beat ab — das klassische "Flash on beat, decay" Verhalten. Die anderen
Modi bleiben für Power-User weiterhin über das Dropdown wählbar, das ist
nur ein Default-Wechsel, keine Verhaltensänderung der Modi selbst.
- Nebenbefund, geprüft und ausgeschlossen: die Sorge, `globalBPM` könnte
  0 werden und eine Division durch 0 in den BPM-Sync-Intervall-
  Berechnungen (`Modulator`, `StepFX`, Chaser) auslösen. Bestätigt über
  `Audio_Engine.h`: `globalBPM` wird nach jedem Audio-Update über
  `constrain(globalBPM, BPM_MIN_LIMIT=60, BPM_MAX_LIMIT=180)` geklemmt,
  Startwert 120, Fallback bei Verlust der Erkennung ebenfalls über
  `BPM_DEFAULT_FALLBACK`. `/beat` (manueller Tap) liest `globalBPM` nur,
  schreibt es nie. Kein Divide-by-zero-Risiko — kein Fix nötig.
- **Live per curl verifiziert:** `/sync` (setzt `masterSyncTime` auf
  jetzt) gefolgt von `dimFX` Start mit `mo=2, cu=3, tr=1` (Beat-Sync) —
  CH1 direkt nach dem Sync-Reset bei 245 (nahe Maximum, exakt auf dem
  simulierten Beat), danach abfallendes/oszillierendes Muster über die
  folgenden Beat-Intervalle — bestätigt "hell auf dem Beat, Abfall bis
  zum nächsten" statt umgekehrt.

**3. „hw mic im programmer button tut nichts, wenn hw mic nicht im
live an ist, die scheinen nicht gesycnt. genauso wue ix sensitity."**
Root-Cause: Der "HW MIC"-Button und der "Mic sensitivity"-Slider im
Programmer-Tab (`ProgrammerTab` in `data/index.html`) waren an
`state.micSync`/`state.micSens` gebunden — zwei Felder, die **nirgendwo
sonst im Code vorkommen**: nie initialisiert, nie gelesen, nie per
`fetch()` ans Backend geschickt. Kein Sync-Bug zwischen zwei echten
Zuständen, sondern zwei komplett tote, rein dekorative Duplikate ohne
jede Backend-Anbindung — daher "tut nichts". Der echte Zustand
(`micOn`/`hwSens`, angebunden an `/hwaudio` über `handleMic()`) existierte
nur im `LiveTab`. Fix: `ProgrammerTab` bekommt jetzt dieselben Props wie
`LiveTab` (`micOn`, `onMicToggle`, `hwSens`, `setHwSens`) von der
App-Komponente durchgereicht; Button und Slider im Programmer-Tab nutzen
jetzt diese echten, gemeinsamen Werte statt der toten lokalen Felder.
Beide Tabs zeigen jetzt denselben Live-Zustand und lösen denselben
`/hwaudio`-Request aus, unabhängig davon, in welchem Tab man klickt.

**Gebaut:**
- `FX_Engine.h`: `Modulator::process()` Free-run-Phasenformel auf
  literale ms-Periode umgestellt; `Modulator`-Default `mode` von 0 auf 2.
- `data/index.html`: `dimMo`-Default von 0 auf 2; `holdUnit=""` für
  Dimmer-/Gobo-Rot-/Prism-Rot-"Manual speed" entfernt (zeigt jetzt "ms");
  `ProgrammerTab` erhält `micOn`/`onMicToggle`/`hwSens`/`setHwSens` als
  Props, Button/Slider nutzen diese statt `state.micSync`/`state.micSens`;
  Render-Aufruf von `HorizonProgrammerTab` reicht dieselben Handler durch
  wie `HorizonLiveTab`.

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`, auf dem echten
Gerät geflasht (`upload` + `uploadfs`), Gerät danach unter
`192.168.8.113` erreichbar. Alle drei Punkte oben live per `curl`
bestätigt (Dimmer-Speed-Timing, Beat-Sync-Helligkeitsrichtung). Die
HW-Mic-Programmer-Fix ist eine reine Frontend-Verdrahtung ohne
eigenständig curl-prüfbares Verhalten — braucht Bestätigung im Browser.

---

## 2026-08-19 — Preset-Panel-Layout, Slot-Reload-Bug, mobiles Confirm-Dialog

Drei konkrete UI-Fixes im Programmer-Tab (`data/index.html`), von einem
Hintergrund-Job umgesetzt.

**1. „Save to preset" war rechts, weit weg vom Joystick — nicht per
Daumen erreichbar.** Der `Accordion`-Block "Save to preset · slot" saß
in der rechten Spalte von `ProgrammerTab`, hinter "Optics · prism ·
macros" — mehrere Panels von "Speed · Curve · Momentum"
(`JoystickAdvancedControls`, in der linken Spalte innerhalb "Joystick ·
pan + tilt") entfernt. Fix: Block in die linke Spalte verschoben, direkt
unter `JoystickAdvancedControls`/dem Joystick-Panel.

**2. Preset-Name lud nicht neu, wenn man denselben Slot erneut wählte.**
Root-Cause: Das Nachladen des Namens (`presetSaveName:
s.presetNames[slot-1]`) hing komplett am `onChange` des nativen
`<select>` — und der DOM feuert `change` nur, wenn sich der Wert
tatsächlich ändert. War man schon auf Slot 1 und wählte erneut "Slot 1",
blieb der `change`-Event aus, der Namens-Reload lief nie, ein zuvor
eingetippter/übriggebliebener Text blieb im Feld stehen. Fix: zusätzlich
`onFocus` am `<select>` — feuert zuverlässig beim Öffnen des
Pickers (auch mobil, iOS Safari/Android Chrome), lädt den Namen des
aktuell gewählten Slots also schon vor jeder Auswahl frisch nach,
unabhängig davon ob sich der Wert danach ändert.

**3. Save/Recall ohne Sicherheitsabfrage, und `window.confirm()` ist auf
dem Handy nicht zuverlässig genug.** Es gab noch keine eigene
Modal-Komponente im File — nur `window.showToast` (rein-imperatives
Vanilla-DOM, an `window` gehängt, damit es aus jedem
`<script type="text/babel">`-Scope aufrufbar ist, siehe `CLAUDE.md` zur
Scope-Trennung der Blöcke). `window.confirm()` wird an drei Stellen
verwendet (OTA-Upload, WiFi-Reset, Clear Programmer), aber genau das
wollte der Nutzer für Save/Recall explizit vermeiden ("popup nicht
clever", muss auf dem Handy zuverlässig funktionieren). Gebaut:
`window.confirmDialog(message, opts) → Promise<boolean>` nach exakt
demselben Muster wie `showToast` — eigenes Overlay/Card mit großen
Yes/No-Touch-Targets, `<style>`-Block ergänzt (`.hz-confirm-*`-Klassen,
gleiche Optik wie die Toasts). SAVE- und RECALL-Button im
Preset-Accordion rufen jetzt `await confirmDialog(...)` auf und brechen
bei Ablehnung ab, bevor der `/save`- bzw. `/recall`-Request rausgeht.

**Gebaut:**
- `data/index.html`: `.hz-confirm-overlay`/`.hz-confirm-card`/
  `.hz-confirm-msg`/`.hz-confirm-btns`/`.hz-confirm-btn` CSS ergänzt
  (neben den bestehenden `.hz-toast-*`-Klassen).
- `window.confirmDialog(...)` ergänzt, direkt neben `window.showToast`/
  `window.haptic`.
- "Save to preset · slot"-`Accordion` aus der rechten in die linke
  Spalte von `ProgrammerTab` verschoben (jetzt direkt unter dem
  Joystick-Panel).
- Slot-`<select>` bekommt `onFocus`-Handler zum Neuladen des
  Preset-Namens.
- SAVE-/RECALL-Button nutzen `confirmDialog(...)` vor dem jeweiligen
  `fetch()`.

`pio run` und `pio run -t buildfs` beide `[SUCCESS]` (Flash-Nutzung
90.9%, unverändert gegenüber vorher — reine Inline-Style/JS-Ergänzung,
kein Byte-Sprung). Nicht auf echter Hardware/im Browser getestet — reine
Frontend-Änderung ohne curl-prüfbares Backend-Verhalten, braucht
Bestätigung im Browser (insbesondere `onFocus`-Verhalten auf echtem
Mobilgerät).

**Offen:** Beat-Sync für `MovementEngine`-Pattern (Kreis/Achterbahn
etc.) wurde in derselben Session besprochen, aber noch nicht
umgesetzt — siehe Rückmeldung im Chat: aktuell moduliert `sync`
(`FX_Engine.h` `MovementEngine::process()`) nur die Speed/Size-Hüllkurve
(`modPhase` → `currentSpeed`/`currentSize`), nicht die tatsächliche
Pattern-Phase (`enginePhase`, wird pro Frame integriert, nie auf den
Beat zurückgesetzt) — dadurch läuft die Form nie phasenexakt zum Beat,
und kurze Divisoren (1/2, 1/4, 1/8 Beat) verlangen bei großen Mustern
Winkelgeschwindigkeiten, die der Motor nicht schafft. Vorschlag noch
nicht implementiert, siehe `doc/content/backlog.md`.

---

## 2026-08-19, Fortsetzung — Movement-Beat-Sync umgesetzt (phasenexakt statt integriert)

User bestätigte im selben Chat den im vorigen Eintrag skizzierten
Vorschlag ("Ja, umsetzen"). Umgesetzt von einem Hintergrund-Job.

**Fix in `FX_Engine.h`, `MovementEngine::process()`:** Der finale
Integrations-Schritt (`enginePhase += currentSpeed * dt * 5.0f`) läuft
jetzt nur noch für `trigger != 1` (freilaufend/Audio-Trigger). Für
`trigger == 1` (BPM-Sync) wird `enginePhase` stattdessen direkt aus dem
ohnehin schon beat-exakten `modPhase` abgeleitet
(`enginePhase = modPhase * PI * 2.0f`) — `modPhase` selbst wurde schon
vorher korrekt aus dem Beat-Takt berechnet
(`(now - masterSyncTime) % interval / interval`), nur die Pattern-Phase
hing bisher nicht daran. Ergebnis: eine Umdrehung startet jetzt
garantiert exakt auf einem Beat und ist exakt am Ende des
`sync`-Intervalls fertig, kein Drift mehr über die Zeit. Die
Speed/Size-Hüllkurve (`currentSize`/`currentSpeed`, moduliert über
`modMo`/`modCu`) bleibt unverändert bestehen und läuft weiterhin
synchron zur selben Phase.

**Eigene Divisor-Tabelle für Movement statt der gemeinsamen `syncBeats[]`:**
`Moving_Head_Horizon.ino` bekommt `const float moveSyncBeats[7] = {1.0,
2.0, 4.0, 8.0, 16.0, 32.0, 64.0}` (Beats **pro Umdrehung**, nicht
Sekundenbruchteile) — `moveFX.process(...)` nutzt jetzt diese Tabelle
statt des gemeinsamen `syncBeats[]` (`{8, 4, 2, 1, 0.5, 0.25, 0.125}`),
das für Dimmer-/Gobo-Rotation/Prisma-Rotation (`dimFX`/`gRotFX`/`pRotFX`)
unverändert weiterläuft — dort sind kurze Divisoren bis 1/8 Beat
sinnvoll (LFO auf einem Kanal, kein physisches Pan/Tilt-Slew-Limit).

**Frontend (`data/index.html`):** Der Sync-Dropdown im Movement-FX-Panel
zeigte bisher dieselbe `SYNCS`-Liste ("Sync · 1 Beat" … "Sync · 1/8 Beat")
wie alle anderen fünf FX-Typen — jetzt inhaltlich falsch, da die
Divisor-Indizes eine andere Bedeutung haben. Neue, separate
`MOVE_SYNCS`-Liste ("Sync · 1 Beat / rev" … "Sync · 64 Beats / rev").
`TriggerBlock` bekommt einen neuen `syncOptions`-Prop (Default bleibt
`SYNCS`, unverändert für Dimmer-/Gobo-Rot-/Prism-Rot-/Color-Chaser-/
Gobo-Chaser-TriggerBlocks), nur `MovementFx`s `TriggerBlock`-Aufruf
übergibt jetzt `syncOptions={MOVE_SYNCS}`.

**Bewusste Bedeutungsänderung, vom User im Chat bestätigt:** `moveFX.sync`
ist Teil von `SceneData` (NVS-persistiert). Ein gespeichertes Preset mit
`sync=3` bedeutete vorher "1 Beat"-Hüllkurvenperiode, bedeutet nach
diesem Fix "8 Beats pro Umdrehung" — bestehende gespeicherte Presets/
Chaser-Szenen mit aktivem Movement-BPM-Sync bewegen sich also spürbar
anders (langsamer/größere Zeitskala) als vor dem Fix. Kein Versehen,
sondern genau der besprochene Trade-off.

**Verifiziert:** `pio run` und `pio run -t buildfs` beide `[SUCCESS]`
(Flash-Nutzung 91,0 %, minimal gestiegen durch die zusätzliche
`moveSyncBeats[]`-Konstante und den `syncOptions`-Prop). Nicht auf
echter Hardware getestet — reine Logik-/Datenänderung ohne
curl-prüfbares Verhalten über den bestehenden State-Poll hinaus, braucht
Bestätigung am echten Fixture (insbesondere: schließt ein großer Kreis
bei "8 Beats / rev" wirklich sauber auf dem Beat ab, ohne dass der Motor
sichtbar hinterherhinkt).

---

## 2026-08-19, Fortsetzung — Movement-Sync-Divisoren auf 8 Stufen (bis 128 Beats) erweitert

User-Feedback direkt danach: „movement fx hat immernoch nur max. 8 beats,
nicht 16 32 64 128". Zwei mögliche Ursachen abgewogen: entweder das Gerät
lief noch mit der alten, nicht geflashten Firmware (dort ist „8 Beats"
tatsächlich der höchste Wert, aus der alten gemeinsamen `syncBeats[]`) —
oder der neue `moveSyncBeats[7]`-Bereich (1–64 Beats/Umdrehung aus dem
vorigen Eintrag) reicht dem User nicht, der explizit auch 128 erwähnte.
Da Letzteres so oder so ein legitimer, einfacher Ausbau ist, umgesetzt statt
nur nachgefragt:

- `Moving_Head_Horizon.ino`: `moveSyncBeats[]` von 7 auf 8 Einträge erweitert
  (`{1, 2, 4, 8, 16, 32, 64, 128}`).
- `FX_Engine.h`, `MovementEngine::process()`: `constrain(sync, 0, 6)` →
  `constrain(sync, 0, 7)` (nur hier — `Modulator::process()`, von Dimmer/
  Gobo-Rot/Prisma-Rot genutzt, bleibt bei `0, 6` mit dem unveränderten
  7-Werte-`syncBeats[]`).
- `WebAPI.h`, `/fx`-Route: `moveFX.sync`-Clamp ebenfalls von `0, 6` auf
  `0, 7` (nur der Movement-spezifische Handler — der geteilte `/modfx`-
  Handler für dim/gr/pr bleibt bei `0, 6`).
- `data/index.html`: `MOVE_SYNCS` um `[7, 'Sync · 128 Beats / rev']`
  ergänzt.

`pio run` und `pio run -t buildfs` beide `[SUCCESS]`. Weiterhin nicht auf
echter Hardware getestet — der User muss so oder so neu flashen, um
überhaupt eine der beiden Movement-Sync-Änderungen aus dieser Session zu
sehen (falls die ursprüngliche Rückmeldung tatsächlich vom alten,
ungeflashten Stand kam statt von einer echten Grenze im neuen Code).

---

## 2026-08-19, Fortsetzung — Geräte selbst geflasht, echter Root-Cause für "Movement random/1-Beat" gefunden, plus BPM-Tap-Bug

User: "flashe selbst, gerät ist dran" — Firmware und Filesystem per
`pio run -t upload`/`-t uploadfs` über `/dev/cu.usbmodem1101` geflasht,
beide Hash-verifiziert, Gerät danach unter `192.168.8.113` erreichbar
bestätigt. Direkt danach zwei Live-Rückmeldungen vom User:

1. **"movement fx hat immernoch nur max. 8 beats, nicht 16 32 64 128."**
   Divisor-Tabelle war zu diesem Zeitpunkt bereits im Code auf 7 Einträge
   (bis 64) erweitert — User wollte zusätzlich 128. `moveSyncBeats[]` auf 8
   Einträge erweitert (`{1,2,4,8,16,32,64,128}`), zugehörige Clamps in
   `MovementEngine::process()` (`FX_Engine.h`) und der `/fx`-Route
   (`WebAPI.h`) von `0,6` auf `0,7`, Frontend-`MOVE_SYNCS` um den 8. Eintrag
   ergänzt. Geflasht und per curl bestätigt: `/fx?...&sy=7` wird jetzt
   akzeptiert und als `"fSy":7` zurückgemeldet.

2. **"da funktioniert so nicht, die movements sind total wiered und
   random... sieht auch eher so aus, als ob er auf 1 beat sync obwohl z.b.
   8 oder 32 eingestellt sind."** Das war der eigentlich interessante Fund
   dieser Session — ein waschechter, vorher unentdeckter Bug, keine
   Fehlbedienung. Root Cause: `masterSyncTime` wird bei **jedem einzelnen
   erkannten Beat** (echter Audio-Bass-Treffer über `pollAudioEngine()`,
   `Audio_Engine.h:174-176`, UND jedem manuellen `/beat`-Tap) auf `now`
   zurückgesetzt — das ist beabsichtigtes Verhalten für die
   Selbstkorrektur bei kurzen (≤1 Beat) Zyklen, bricht aber jede
   `(now - masterSyncTime) % interval`-Berechnung für `interval > 1 Beat`
   fundamental: der Zähler kann nie über eine Beat-Länge hinauswachsen,
   bevor er wieder auf ~0 zurückgesetzt wird — der `sync`-Wert (8, 32, ...)
   war für Movement (und ebenso für Dimmer-/Gobo-Rot-/Prisma-Rotation!)
   **schon immer wirkungslos**, sobald echte Beat-Erkennung lief. Genau das
   hat mein vorheriger Fix (Pattern-Phase direkt aus `modPhase` statt
   integriert) von einer kaum wahrnehmbaren Hüllkurven-Delle in einen
   sofort sichtbaren Positions-Sprung verwandelt — daher "random" und
   "kleine Kreise, die ab und an springen".
   **Fix (`FX_Engine.h` + `Moving_Head_Horizon.ino`):** Neuer globaler
   `beatCount` (wächst nur bei echten, vollständigen Beat-Intervallen,
   nie zurückgesetzt) plus ein pro Frame einmal berechnetes
   `beatsElapsedTotal = beatCount + clamp((now-lastBeatTime)/beatIntervalMs, 0, 1)`
   in `updateEngines()`. `Modulator::process()` und
   `MovementEngine::process()` nehmen jetzt dieses `beatsElapsedTotal`
   entgegen statt `masterSyncTime`+`globalBPM`, und berechnen die
   Zyklusposition als Bruchteil von `beatsElapsedTotal / syncBeats[sync]`
   (nur der Nachkommaanteil) — das wächst weiter über beliebig viele echte
   Beats, unabhängig davon, wie oft `masterSyncTime`/`lastBeatTime`
   zwischendurch neu verankert wird. Betrifft/fixt gleichermaßen Movement,
   Dimmer-, Gobo-Rotations- und Prisma-Rotations-BPM-Sync (alle nutzten
   dieselbe kaputte Formel).

**Zusätzlich vom User gemeldet, separat root-caused (nicht Teil der
ursprünglichen Movement-Frage, aber im selben Testlauf aufgefallen):**
„bpm function scheint kaputt, wenn ich manuell reintappe und mic aus ist,
stimmt es kurz... aber nach 1s ist er wieder... zurück. auch scheint es
mit mic on zu driften... bpm ist immer zu langsam."

3. **Manueller Tap persistierte `globalBPM` nie.** `/beat` (`WebAPI.h`)
   setzte bisher nur `lastBeatTime`/`manualTap` (Phasen-Alignment) — den
   tatsächlichen `globalBPM`-Wert hat der Tap nie berührt. Der Frontend-
   `useTapTempo()`-Hook berechnet die getappte BPM zwar korrekt, hält sie
   aber nur in lokalem React-State — und der nächste `/api/state`-Poll
   (alle 500 ms) überschreibt diesen lokalen State bedingungslos mit dem
   unveränderten, alten Backend-`globalBPM` (`if (d.bpm) setBpm(d.bpm)`).
   Deshalb "stimmt es kurz, dann zurück" — exakt ein Polling-Zyklus lang.
   **Fix:** `tap()` gibt den berechneten Wert jetzt zurück,
   `tapWithFirmware` hängt ihn als `?bpm=` an den `/beat`-Request an;
   `/beat` setzt `globalBPM` jetzt direkt (geklammert auf
   `BPM_MIN_LIMIT`/`BPM_MAX_LIMIT`, 60–180), wenn der Parameter vorhanden
   ist. Live per curl verifiziert: `/beat?bpm=126` → `globalBPM` bleibt
   auch 1,5 s später noch bei 126 (vorher wäre es im nächsten Poll-Zyklus
   zurückgesprungen).
4. **Mic-Erkennung "immer zu langsam"/driftet — plausibelste Ursache
   identifiziert und behoben, aber nicht live mit echtem Audio
   verifizierbar.** Die Bass-Beat-Erkennung (`Audio_Engine.h`,
   `pollAudioEngine()`) akzeptierte ein neues Intervall bisher nur, wenn
   es innerhalb ±20 % des aktuellen `globalBPM`-Schätzwerts lag
   (`BPM_DEVIATION_TOLERANCE_DIVISOR`). Ein reiner Energie-Schwellwert-
   Detektor übersieht auf echtem Audio gelegentlich einen leiseren Kick —
   sobald das passiert, ist das gemessene Intervall ~2× so lang wie der
   echte Beat, die Schätzung rutscht auf die halbe Tempo ("Oktave"), und
   ab dann werden alle folgenden, eigentlich korrekten (schnelleren)
   Intervalle von der ±20 %-Toleranz für immer abgelehnt, da sie ~50 %
   vom (falschen) aktuellen Schätzwert abweichen — ein permanenter
   Zu-langsam-Lock ohne Weg zurück. Erklärt "immer zu langsam" sehr
   plausibel (ein reiner Rauschartefakt hätte keine derart systematische,
   einseitige Verzerrung). **Fix:** Oktave-Fehlerkorrektur ergänzt — vor
   dem Toleranz-Check wird zusätzlich geprüft, ob das gemessene Intervall
   verdoppelt oder halbiert deutlich besser zum aktuellen Schätzwert passt;
   falls ja, wird der bessere Kandidat (roh, falls die Messung eigentlich
   das schnellere/korrekte Intervall war, oder halbiert, falls ein Beat
   ausgelassen wurde) in die Historie/den Median übernommen statt
   verworfen. **Nicht mit echtem Audio verifizierbar** in dieser Umgebung
   (kein Mikrofon-Input hier) — dafür zwei neue Debug-Felder in
   `/api/state` ergänzt: `rawBPM` (Median-Schätzung vor der 19:1-Glättung)
   und `rawMs` (letztes akzeptiertes, ggf. oktave-korrigiertes Intervall
   in ms), damit sich das live per curl beobachten/verifizieren lässt statt
   erraten zu werden.

**User-Vorschlag "vllt. musst du eine fkt bauen um dir den jitter der
mainloop zu debuggen"** — aufgenommen, aber mit Vorbehalt: die drei oben
gefundenen Bugs erklären die gemeldeten Symptome bereits vollständig und
mechanistisch (kein diffuses Rauschen, sondern klar reproduzierbare
Ursachen), Main-Loop-Jitter erschien daher unwahrscheinlich als
Hauptursache. Trotzdem als billige, dauerhaft nützliche Diagnose ergänzt:
`loop()` trackt jetzt die größte Lücke zwischen zwei Iterationen im
letzten 5-Sekunden-Fenster (`loopMaxMs`), exponiert als `loopMax` in
`/api/state`. Live gemessen direkt nach dem Flashen: **8 ms** im Leerlauf
— bestätigt, dass Jitter (zumindest ohne aktive FX/Traffic) keine
signifikante Rolle spielt, Feld bleibt aber dauerhaft für künftige
Diagnose unter Last (viele Fixtures, OTA, etc.) verfügbar.

**Verifiziert:** `pio run` und `pio run -t buildfs` beide `[SUCCESS]`
(Flash-Nutzung 91,0 %), auf dem echten, angeschlossenen Gerät geflasht
(`upload` + `uploadfs`, beide Hash-verifiziert). Per curl bestätigt:
`/fx?...&sy=7` akzeptiert, `/beat?bpm=126` persistiert über 1,5 s. Die
Movement-Beat-Lock-Korrektur selbst (Punkt 2) und die Oktave-Korrektur
(Punkt 4) sind **nicht** per curl beobachtbar (Pattern-Position liegt nie
in der JSON-API, Oktave-Fix braucht echtes Mikrofon-Signal) — beide
brauchen eine Live-Prüfung durch den User am Gerät, jetzt mit `rawBPM`/
`rawMs`/`loopMax` als zusätzlichen Debug-Signalen.

---

## 2026-08-19, Fortsetzung — Movement-FX-Defaults auf 10 %/1000 ms, und ein echter Bug in der `beatCount`-Korrektur selbst gefunden

**1. "movement fx defaults sollen alle 10% und 1000ms sein."** Backend-
Defaults in `FX_Engine.h` (`MovementEngine`: `spdSt`/`spdEn` 50→10,
`szSt`/`szEn` 30→10, `modSp` 10.0f→1000.0f) und Frontend-Initialzustand
(`data/index.html`, `fxMS` 100→1000, `fxSS`/`fxSE`/`fxZS`/`fxZE` je auf
10) angepasst. Nebenfund beim Ändern: der bisherige Frontend-Default
(`fxSS:0, fxSE:255, fxZS:0, fxZE:255`) lag komplett außerhalb des
Slider-eigenen Bereichs (`min=1 max=100`, %) — vermutlich ein Leftover
aus einem anderen Kontext (0–255 sieht nach 8-Bit-DMX-Byte-Default aus),
wurde aber in der Praxis nie sichtbar, weil der erste `/api/get_dmx`-Poll
(~500 ms nach Laden) diesen Wert sofort mit dem echten Backend-Wert
überschreibt.

**2. "movement fx stimmt noch nicht. ich habe 16 beats gesetzt bei
120bpm und er zuckt während dem kreisfahren mehrmals zurück mit dem
beat... als würde er jugglen."** Ein echter, selbst eingeführter Bug in
der `beatCount`-Korrektur von weiter oben — nicht nur eine Restungenauig-
keit. Root Cause: `beatCount` wurde bisher **nur** im internen
Metronom-Tick (`Moving_Head_Horizon.ino`, unconditional jeden Loop)
hochgezählt, nicht aber bei echten, per Mikrofon erkannten Beats
(`Audio_Engine.h`, setzt `lastBeatTime = now` direkt, ohne `beatCount++`).
Da eine echte Beat-Erkennung `lastBeatTime` fast immer VOR dem internen
Metronom-Tick zurücksetzt (Audio-Erkennung ist unmittelbarer als der frei
laufende virtuelle Klick), gewinnt die Audio-Erkennung dieses Wettrennen
praktisch bei jedem einzelnen Beat — der Metronom-Tick (und damit
`beatCount++`) feuert dadurch fast nie, während der Bruchteilsanteil
(`(now-lastBeatTime)/beatInterval`) bei jeder Audio-Erkennung erneut auf
~0 zurückspringt. Ergebnis: `beatsElapsedTotal` blieb effektiv innerhalb
eines einzelnen Beats gefangen (exakt der alte "sieht aus wie 1-Beat-
Sync"-Bug, nur diesmal durch meinen eigenen Fix reproduziert) UND sprang
bei jeder Erkennung sichtbar zurück — das "Jugglen".
**Fix:** `beatCount++` jetzt zusätzlich direkt an der Stelle in
`Audio_Engine.h`, an der ein echter Beat `lastBeatTime` zurücksetzt —
jeder Reset von `lastBeatTime` (egal ob virtueller Metronom-Tick oder
echter erkannter Beat) ist jetzt untrennbar mit genau einem
`beatCount++` gekoppelt, damit `beatsElapsedTotal` durchgehend
monoton wächst, unabhängig davon, welche der beiden Quellen den nächsten
Beat zuerst meldet.
**Ehrlich eingeordnet:** kleine Korrektur-Ruckler bleiben bei sehr langen
Zyklen (16/32/64/128 Beats) prinzipbedingt möglich, wenn die
Live-Beat-Erkennung geringfügig von der `globalBPM`-Schätzung abweicht
(normales Verhalten bei jedem live-beat-gelockten System) — aber
begrenzt auf höchstens den Bruchteil eines einzelnen Beats, nicht mehr
auf "springt mitten in der Umdrehung fast auf Null zurück".

**Auf User-Wunsch nur kompiliert und committed, NICHT geflasht** ("ich
flashe morgen"). `pio run` und `pio run -t buildfs` beide `[SUCCESS]`.
Weder die neuen Defaults noch der `beatCount`-Fix sind bisher live
verifiziert.

## 2026-08-20 — `/ultrareview` gegen `origin/main`, acht Findings gefixt, Gerät geflasht

**`/ultrareview` lief zunächst mit dem Default-Target und scheiterte an der
Diff-Grösse** (33 Dateien, 13.049 Zeilen — enthielt `doc/content/history.md`
und alte `V3/data/index_old.html`/`index_semiworking.html`-Snapshots). Auf
Rückfrage stellte sich heraus: Ziel war ein Review des aktuellen Codes auf
Bugs, kein Review dieses aufgeblähten Diffs. Da dieses Repo keinen lokalen
`main`-Branch hat (nur `origin/main`, ein veralteter Stand von vor dem
V1/V2/V3-Merge, siehe `README.md`), lief das eigentliche Review gegen
`origin/main` mit `V1/**`, `V2/**`, `V3/**`, `doc/**`, `firmware/**`,
`data/vendor/**` und `*.md` ausgeschlossen — übrig blieb der reale aktuelle
Quellcode (`Audio_Engine.h`, `FX_Engine.h`, `Moving_Head_Horizon.ino`,
`WebAPI.h`, `data/index.html`, `platformio.ini`, ~3.090 Zeilen).

**Ein erster Versuch scheiterte am Account-Session-Limit** (alle
Finder-Subagenten terminiert), ein zweiter Versuch am Rechner-Sleep
mid-run. Der dritte Versuch kam durch — ein Teil der 10 parallelen
Finder-Subagenten crashte trotzdem wieder (Sleep/Stream-Stalls), der
Hauptagent hat die fehlenden Winkel per direkter manueller Code-Inspektion
nachgeholt (ein abgeschlossener Cross-File-Tracer-Subagent lieferte
zusätzlich vier unabhängig verifizierte Funde). Acht Findings (sieben
Correctness, ein Simplification), Details siehe `backlog.md` →
„Kürzlich gefixt" 2026-08-20-Eintrag — Kurzfassung:

1. `triggerSceneFX()` liess `colWasActive`/`sgWasActive`/`rgWasActive`
   stehen — Preset-Recall konnte durch `runStep()`s Stop-Fallback einen
   Tick später wieder überschrieben werden.
2. `onArtDmx()` (Art-Net-Übernahme) hatte denselben Bug — brach kurzzeitig
   das „externes DMX gewinnt immer" Prinzip im Moment der Übernahme.
3. `/modfx` klammerte `st`/`en` nicht (anders als alle Schwester-Routen) —
   ein weit ausserhalb 0–255 liegender Wert erreichte einen `(byte)`-Cast
   als float ausserhalb des Byte-Bereichs (undefined behavior).
4. `/colfx` hatte keine `mv`-Restore-on-Stop-Logik, anders als
   `/sgobfx`/`/rgobfx` — sichtbarer Farbglitch beim Stoppen.
5. Hard Sync (`/sync`) war für `trigger==1` (BPM-sync) FX wirkungslos, weil
   deren Phase jeden Tick frisch aus dem geteilten `beatCount`/
   `lastBeatTime`-Takt berechnet wird, nicht aus dem eigenen `.phase`-Feld,
   das `/sync` (und `manualTap`) bisher allein zurücksetzten.
6. `float(millis())`-Präzisionsverlust im Rotation-Pulse-Shake nach
   ~4,66 h Laufzeit (float-Mantisse reicht nur bis 16.777.216).
7. `d.fw` im Settings-Panel las ein Feld, das `/api/state` nie sendete —
   totes UI-Feld, Anzeige zeigte dauerhaft den Platzhaltertext.
8. (Simplification) Acht fast identische Diff/Fetch-Blöcke im
   App-State-Sync-Effect (`data/index.html`) zu einem `syncFx()`-Helper
   zusammengefasst.

Alle acht Fixes umgesetzt (`Moving_Head_Horizon.ino`, `WebAPI.h`,
`data/index.html`), plus ein neues `#define FW_VERSION "1.0.0"` und
`functions.md` für die geänderten Routen-Parameter (`mv` bei `/colfx`,
Klammerung bei `/modfx`, `fw`-Feld bei `/api/state`, korrigierte
`/sync`-Beschreibung) aktualisiert. `pio run` und `pio run -t buildfs`
beide `[SUCCESS]`.

**Geflasht.** `pio run -t upload` scheiterte zweimal mit „No serial data
received" — das Board (ESP32-C3 Supermini, natives USB-Serial/JTAG) trat
trotz manuellem BOOT-Halten nicht in den Download-Modus ein, weil
PlatformIOs Default-Reset-Logik (`--before default-reset`) das Board vor
der Verbindung aktiv zurücksetzte und damit den manuell erzwungenen
Bootloader-Zustand sofort wieder aufhob. Fix: `esptool` direkt aufgerufen
(`~/.platformio/penv/bin/python -m esptool`) mit `--before no-reset`, damit
es das Board im Zustand anspricht, in dem es bereits ist, statt es selbst
zurückzusetzen. Firmware (`firmware.factory.bin`, gemergter Bootloader+
Partitionstabelle+App) auf `0x0` geschrieben, LittleFS-Image separat auf
`0x290000` (Offset aus der Partitionstabelle via `gen_esp32part.py`
decodiert — 4 MB Flash, `spiffs`-Partition bei `0x290000`/1408K). Beide
Schreibvorgänge inkl. Hash-Verify erfolgreich, `esptool` hat danach per
RTS-Pin hart zurückgesetzt.

**Netzwerk-Reachability-Check von diesem Rechner aus fehlgeschlagen**
(`curl http://movinghead.local/api/state` → `HTTP:000`, auch nach
mehreren Sekunden Wartezeit) — dieser Rechner ist offenbar nicht im
selben WLAN wie das Gerät, kein Hinweis auf einen Firmware-Fehler. Live-
Verhalten der acht Fixes (insbesondere Hard Sync und `/colfx`-`mv`) am
Gerät selbst noch nicht gegengeprüft, siehe `backlog.md` → „Offen".

## 2026-08-20, Fortsetzung — Root Cause für Nichterreichbarkeit gefunden: `esptool`-Workaround hat `nvs` gelöscht

**Korrektur zum vorigen Eintrag:** Die vermutete Ursache für die
fehlgeschlagene `movinghead.local`-Erreichbarkeit („dieser Rechner ist
offenbar nicht im selben WLAN") war falsch. User meldete den echten
Seriell-Log direkt nach dem Booten:

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0xd (SPI_FAST_FLASH_BOOT)
[E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FOUND
[... 11 weitere identische Zeilen ...]
```

Zwölf `nvs_open`-Fehlschläge direkt beim Boot, vor jeder WLAN-Aktivität —
das ist genau die Anzahl an `Preferences`-Namespaces, die
`loadAllChaserScenes()`/`setup()` beim Start öffnet (`sys`, `sc1`..`sc10`,
`patch`). **Root Cause:** Der `esptool`-Flash-Workaround vom vorigen
Eintrag hat `.pio/build/supermini/firmware.factory.bin` — den von
PlatformIO gemergten Blob aus Bootloader+Partitionstabelle+App, EIN
zusammenhängendes Byte-Array von `0x0` bis weit über `0x10000` hinaus —
mit einem einzigen `write-flash 0x0 firmware.factory.bin`-Aufruf
geschrieben. Dieser Blob überschreibt zwangsläufig auch die Lücke
zwischen den einzelnen Komponenten, und in genau dieser Lücke liegt die
`nvs`-Partition (`0x9000`, 20K laut Partitionstabelle) sowie `otadata`
(`0xe000`) — beide wurden dadurch überschrieben. Verifiziert durch
Rücklesen von `0x9000` via `esptool read-flash`: kein reines `0xFF`
(vollständig gelöscht), aber die Namespace-Öffnungen scheitern trotzdem
für alle zwölf bekannten Namespaces — konsistent mit einer beschädigten/
zurückgesetzten NVS-Struktur, nicht mit unberührten Altdaten.

**Konsequenz:** WLAN-Zugangsdaten, Fixture-Patch, Master-Brightness,
Smoothing und alle 10 Preset-/Chaser-Slots sind weg. Das Gerät läuft mit
der neuen Firmware technisch korrekt — ohne gespeicherte WLAN-Zugangsdaten
fällt es wie dokumentiert auf den AP-Fallback (`Moving_Head_Ctrl`/
`12345678`) zurück, das ist kein Firmware-Bug. Kein Backup der alten
NVS-Daten vorhanden (Partition wurde vor dem Flash nicht gesichert) —
Wiederherstellung nur durch manuelle Neukonfiguration über den AP-Fallback
(siehe `backlog.md` → „Offen" für die Schritte).

**Guard ergänzt, damit das nicht wieder passiert:** neues
`scripts/flash_esptool.sh` — schreibt `bootloader.bin`/`partitions.bin`/
`boot_app0.bin`/`firmware.bin` einzeln an ihre echten, nicht
zusammenhängenden Partitions-Offsets (identisch zu dem, was
`pio run -t upload` im Erfolgsfall tut), statt den gemergten
`firmware.factory.bin` als einen Blob zu schreiben — die `nvs`-Lücke
zwischen `partitions.bin`-Ende (~`0x8c00`) und `boot_app0.bin`-Start
(`0xe000`) bleibt dadurch unangetastet. `CLAUDE.md` warnt jetzt explizit
vor dem gemergten-Blob-Fehler und verweist auf das Script; Script
verwendet ausserdem `--before no-reset`, damit `esptool` den manuell
erzwungenen Bootloader-Zustand nicht selbst wieder aufhebt (siehe voriger
Eintrag für dieses zweite, unabhängige Problem).

## 2026-08-20, dritte Fortsetzung — Gerät neu konfiguriert, drei GUI-Sync-Bugs gefixt, echter Root-Cause für Movement-Jitter gefunden, neuer AUDIO-DEBUG-Tab gebaut, zwei Fake-FFT-Bugs damit aufgedeckt

Das Gerät wurde zwischen Sessions über den AP-Fallback manuell neu
konfiguriert (WLAN/Patch/Presets) und ist seither über `movinghead.local`
bzw. direkte IP erreichbar — diese Session bestätigt live per `curl`.

**Teil 1 — drei vom User gemeldete Frontend/Backend-Sync-Bugs.** User
meldete: (1) Gobo-1-Auswahl direkt nach „White Open" wirkungslos, erst
Gobo-2-dann-Gobo-1 funktioniert; (2) FX-Start-Button wird kurz grün, fällt
nach ~2s wieder auf rot zurück; (3) Preset-Recall im Live-Executor lädt
nicht alle Werte (Gobo/Zoom fehlen), zweites Drücken vervollständigt es.
Root Cause für (1)/(2): der ausgehende State-Sync-Effect in
`data/index.html` setzt `isReceiving.current` für ~300ms nach jedem
2s-Poll, um zu verhindern, dass gerade empfangene Werte sofort wieder
zurückgeschickt werden — landet eine Nutzeraktion in diesem Fenster, wird
sie beim bestehenden `track()`-Helper korrekt verworfen (kein permanenter
Schaden, nächste unabhängige Aktion holt es nach), aber `syncFx()`
markierte den Snapshot als „gesendet", obwohl der Fetch übersprungen
wurde — der Button blieb lokal „an", das Backend erfuhr nie davon, der
nächste Poll (Gerät meldet weiter „aus") zog ihn zurück. Fix:
`syncFx()`s Baseline-Update jetzt nur noch im selben Zweig wie der
tatsächliche Fetch, identisch zur bereits vorhandenen Disziplin in
`track()`. Gleicher Fix für `master`/`damping`/`transMode`/`joyKey`
(dasselbe Muster, noch nicht gemeldet, aber genauso betroffen). Für (3):
`sgoboBase`/`rgoboBase` wurden im Gegensatz zu `colorBase` nie vom
`/api/get_dmx`-Poll zurückgelesen — nach einem Preset-Recall änderte sich
das Gobo auf dem echten Gerät korrekt, das Dropdown zeigte aber dauerhaft
die zuletzt manuell gewählte Position. Fix: neue Zonen-Lookup-Tabellen
(`SGOBO_ZONES`/`RGOBO_ZONES`) plus Readback im Poll, analog zum
bestehenden `colorEntry`-Muster.

**Zwischenfund — Browser-Cache verschleierte den ersten Test.** Nach dem
ersten Flash meldete der User Symptome, die genau wie die alten,
eigentlich gefixten Bugs aussahen (Dimmer-FX zeigt „RUN" obwohl
`/api/get_dmx` `dA:0` lieferte). Per `curl` verifiziert: das Gerät
servierte bereits die korrigierte Datei (MD5-Abgleich lokal vs. Gerät
identisch) — der Browser-Tab hatte einfach die alte JS im Speicher
behalten, weil `server.serveStatic("/", LittleFS, "/index.html")` ganz
ohne Cache-Header auslieferte (kein `ETag`, kein `Last-Modified`, aber
manche Browser cachen trotzdem ohne Validatoren). Fix:
`Cache-Control: no-store` ergänzt.

**Teil 2 — echter Root-Cause für Movement-Multi-Beat-Sync-Jitter
gefunden.** User meldete: „Circle" bei 32-Beats-Sync zuckt nur links-
rechts, keine echte Umdrehung — tritt bei Global-BPM-Sync auf, nicht bei
direktem Audio-Trigger. `pollAudioEngine()` (`Audio_Engine.h`) setzte bei
jedem echten erkannten Beat `manualTap = true` — dieselbe Flag, die der
echte Tap-Tempo-Button (`/beat`) nutzt. Der `.ino`-Handler dafür macht
`beatCount = 0` (richtig für einen bewussten User-Tap, der die
Taktreferenz neu setzen will). Da `loop()` `pollAudioEngine()` direkt vor
`updateEngines()` aufruft, wurde `beatCount` im **selben** Loop-Durchlauf,
in dem es gerade erst durch `pollAudioEngine()`s eigenen (bereits aus
einer früheren Session vorhandenen) `beatCount++` hochgezählt worden war,
sofort wieder auf 0 genullt — bei z. B. 32 Beats/Umdrehung kam das
Pattern dadurch nie über 1/32 einer Umdrehung hinaus. Fix: `manualTap =
true` aus dem Audio-Beat-Pfad entfernt; die inkrementellen
`beatCount++`/`lastBeatTime`/`masterSyncTime`-Updates bleiben (korrekt für
einen laufenden Beat-Strom), der volle Phasen-Reset bleibt exklusiv
`/beat` vorbehalten. Zusätzlich in derselben Session gefixt (unabhängiger
Bug, gefunden beim Nachlesen von `MovementEngine::process()`):
`modSp`/`modPhase`s Size/Speed-Modulator nutzte eine falsche Formel
(behandelte die UI-Millisekunden als beliebigen Ratenfaktor statt als
Periodendauer) — bei Size/Speed Start==End (Default) folgenlos, sonst
lief der Modulator ~20× zu schnell. Fix: identische Formel wie
`Modulator::process()` übernommen.

**Figure-8-Untersuchung — kein Code-Bug gefunden, vermutlich
Projektionsgeometrie.** User meldete danach weiterhin eine „Acht" statt
eines Kreises, reproduzierbar bei Pan/Tilt 127/127, unabhängig vom
Trigger-Modus (schließt den `manualTap`-Fix als Ursache aus) und bei
mehreren Size-Werten, verschwindet beim Wegbewegen von dieser Position.
`getValues()` wurde erneut Zeile für Zeile gegengerechnet (Rotation,
Invertierung, Skalierung sind alle formerhaltend, `case 1` ist ein exakter
Kreis), Patch geprüft (ein Fixture, Phase 0). Für die Video-Analyse wurde
lokal `ffmpeg` via Homebrew installiert (vorher nicht vorhanden), Frames
aus zwei vom User gelieferten Videos extrahiert und per PIL/`qlmanage`
inspiziert: beide Videos filmen den rotierenden Fixture-Kopf selbst aus
der Nähe (Pan 540°/Tilt 270°, verschachtelte Doppelachse), nicht einen
projizierten Lichtpunkt auf einer entfernten Wand. Ein mathematisch
perfekter Kreis in (Pan-Winkel, Tilt-Winkel)-Raum sieht für einen
Beobachter seitlich der Rotationsachsen generell nicht wie ein Kreis aus —
reine 3D-Projektionsgeometrie jeder Pan/Tilt-Fixture, unabhängig von
Software. Nächster Schritt (an User delegiert, siehe `backlog.md`):
Fixture auf eine entfernte Fläche richten und den projizierten Punkt
filmen statt das Gehäuse.

**Teil 3 — neuer AUDIO-DEBUG-Tab, live zwei echte Fake-FFT-Bugs damit
gefunden.** Auf User-Wunsch („kannst du ein Debug-Tab einbauen, wo man
einen rollenden Graphen sieht") neuer Tab in `data/index.html`: Canvas-
basierter ~15Hz-EKG-Graph für Low/Mid/High-Bänder plus Threshold-Linie,
Sensitivity-Regler, und Live-Tuning für die vorher hartkodierten
Envelope-Follower-Parameter (jetzt `inline int tune*`-Variablen in
`Audio_Engine.h` statt `#define`s). Neue Routen `/api/audio_debug`
(liest, per fixem `snprintf`-Puffer statt der sonst üblichen `String +=`-
Kette wegen der hohen Poll-Frequenz) und `/audio_tune` (schreibt) in
`WebAPI.h`. Direkt beim ersten Live-Test mit echter Musik zeigte der neue
Graph zwei vorher unsichtbare Bugs:

1. **Beat-Tick-Anzeige fast immer leer, obwohl Bass sichtbar über der
   Threshold-Linie lag.** `/api/audio_debug` las `triggerBass` &Co. —
   diese werden am Kopf von `pollAudioEngine()` bei jedem `loop()`-
   Durchlauf auf `false` gesetzt, weit öfter als das interne 40ms-Audio-
   Throttle; ein `true` überlebt nur Mikrosekunden, ein von außen
   kommender HTTP-Request kann das praktisch nie einfangen. Fix: eigene,
   dedizierte Latch-Flags `dbgBassHit`/`dbgMidHit`/`dbgHighHit`
   (bewusst getrennt von den bestehenden `guiBass`/`guiMid`/`guiHigh`,
   die `/api/state` mit seinem eigenen 500ms-Poll schon latcht-und-
   löscht — zwei Poller auf demselben Flag hätten sich gegenseitig die
   Treffer weggeschnappt).
2. **Mid-Band strukturell tot (~0) trotz laufender Musik, User meldete
   „Mid und High bekommen quasi gar kein Signal".** Alle drei Bänder
   sind Hüllkurven desselben unbandgefilterten Rohsignals mit
   unterschiedlichen Attack/Decay-Shifts, keine echte Frequenztrennung.
   `midEnergy = max(0, envMid − envSlow)` — Mid- und Slow-Attack hatten
   denselben Default-Shift (beide ÷4), stiegen bei jeder Flanke also im
   exakten Gleichlauf (Differenz strukturell 0), Mids schnellerer Decay
   zog `envMid` danach nur unter `envSlow`. Fix:
   `tuneSlowAttackShift`-Default von 2 auf 3 (÷8) geändert, stellt eine
   echte fast<mid<slow-Attack-Ordnung her (spiegelt die schon korrekte
   Decay-Ordnung). Live per `curl` bestätigt: `hi`-Wert reagierte danach
   auf reines Raumrauschen statt bei 0 zu bleiben.

Alle Code-Fixes dieser Session mit `pio run` + `pio run -t buildfs`
gegenkompiliert, Firmware und Filesystem mehrfach per `pio run -t
upload`/`-t uploadfs` geflasht (normaler Auto-Reset funktionierte diesmal
durchgehend, kein `scripts/flash_esptool.sh`-Workaround nötig) und live
per `curl` gegengeprüft. Details siehe `backlog.md` → „Kürzlich gefixt"
für die vollständige, technische Fix-Liste.

## 2026-08-20, vierte Fortsetzung — Figure-8 als echter Hardware-Encoder-Defekt bestätigt und softwareseitig gefixt

Der Wand-Projektions-Test aus dem vorigen Eintrag lieferte ein
eindeutiges Ergebnis: User filmte den **projizierten Lichtpunkt direkt**
(nicht mehr das Gehäuse) — bleibt bei Pan/Tilt 127/127 eine sich selbst
kreuzende Acht. Damit als Ursache ausgeschlossen: die zuvor vermutete
3D-Projektions-/Blickwinkelgeometrie. User beschrieb es präzise: „es ist
quasi so als müsste die zweite Hälfte des Kreises irgendwie anders herum
kodiert werden … als ob eine Richtung des Kreises dann kippt" — exakt die
Beschreibung einer nicht-monotonen Tilt-Antwort.

**Live-Verifikation mit echten Telemetriedaten statt Vermutung.** Neue
Debug-Felder `op`/`ot` in `/api/get_dmx` ergänzt (`Moving_Head_Horizon.ino`
`liveOutPan0`/`liveOutTilt0`, gesetzt im Fixture-Loop nach `getValues()`)
— die bestehenden `cp`/`ct`-Felder zeigen nur das *Zentrum* des Patterns,
nie die animierte Position, ein erster Sample-Versuch lief deshalb ins
Leere (dauerhaft `32512`). Mit den neuen Feldern per `curl`-Loop (10Hz,
~12s) samplet und mit `matplotlib` geplottet (dafür lokal via
`pip install`/bereits vorhandenes `matplotlib` genutzt): die vom ESP32
tatsächlich gesendeten DMX-Werte sind ein **mathematisch perfekter
Kreis** — glatt, geschlossen, keine Diskontinuität, keine Anomalie exakt
an der 32768-Grenze. Pan/Tilt sind korrekt 90°-phasenverschobene
Sinuskurven. Das beweist zweifelsfrei: der Bug liegt nicht in der
Pan/Tilt-Mathematik (`getValues()`), sondern zwischen dem DMX-Signal und
der tatsächlichen Bewegung — also im Fixture selbst.

**Kalibrierung als Ursache geprüft und ausgeschlossen.** User fand das
Fixture-eigene Kalibrierungsmenü (LCD-Display, kein Passwort nötig für
diese Ebene) — zeigte `Tilt Calibration: -037` (ungewöhnlich groß im
Vergleich zu Pan `+002`, Colour `+014`, Gobo `+004` etc.). Live auf `0`
gesetzt. Um das sauber zu testen, wurde der Software-Fix (siehe unten)
temporär deaktiviert (`FX_Engine.h`, `TILT_FOLD_FIX_ENABLED`-Flag,
inzwischen wieder entfernt) und derselbe rohe Kreis erneut geflasht und
per Telemetrie bestätigt (identischer Werte-Range wie vor jedem Fix).
Ergebnis am realen Fixture: **weiterhin eine Acht** — plus eine wichtige
Nebenbeobachtung: das Fixture zeigt bei Tilt-Kalibrierung `0` nicht mehr
korrekt gerade nach oben. `-037` war also eine echte, gebrauchte
Kalibrierung für die Nulllage, nicht die Fehlerursache — Kalibrierung
damit als Erklärung ausgeschlossen.

**Fixture-eigenes Diagnosemenü als dritte, unabhängige Bestätigung.**
User schickte ein Video des „Sensor Monitor"-Menüs (Hall-Sensoren,
Pan/Tilt-Codewheel-Status, ohne Passwort erreichbar) während der Kreis
lief: `Pan Codewheel Step` zählt im Video kontinuierlich hoch (0192 →
0238 über die Aufnahme), `Tilt Codewheel Step` bleibt über einen langen
Abschnitt praktisch eingefroren (`0040`–`0042`), bevor er sich langsam
wieder ändert. Das Fixture meldet damit selbst, dass sein Tilt-
Encoder/-Motor Positionsänderungen in diesem Bereich nicht sauber
verfolgt — ein echter, physischer Encoder-/Motor-Defekt dieser konkreten
Einheit, kein Firmware- oder Kalibrierungsproblem, nicht durch das
Fixture-Menü behebbar (das „Advanced"-Menü mit möglicher
Encoder-Neukalibrierung ist zusätzlich Passwort-geschützt, User kennt das
Passwort nicht).

**Software-Workaround, zwei Iterationen.** Erste Version: einzelne
Ausgabe-Samples unterhalb der Fold-Grenze (32768) wurden am Grenzpunkt
gespiegelt (`tOut = 2*32768 - tOut`) — vermied zwar die Selbstkreuzung
(per Telemetrie bestätigt), faltete die Kreisform aber zu einer flachen
Kuppel/„Halfpipe" zusammen (vom User live beobachtet: „bewegt sich wie
ein Smiley oder eine Halfpipe hin und her"), da alle Werte unterhalb der
Grenze auf denselben Bereich oberhalb zurückgeworfen wurden. Zweite,
bessere Version (jetzt aktiv): statt einzelner Samples wird das
**Pattern-Zentrum** in `MovementEngine::getValues()` (`FX_Engine.h`) pro
Frame so weit nach oben verschoben, dass der volle Bewegungsradius des
aktuellen Patterns (`currentSize * 32767 * (|sin(rot)| + |cos(rot)|)`,
eine konservative Schranke für beliebige Rotation) die Problemzone gar
nicht erst erreicht — die Form bleibt dadurch ein echter, unverzerrter
Kreis, nur automatisch etwas höher zentriert als der angeforderte
Rohwert. Per Telemetrie-Sampling verifiziert: Tilt-Range bleibt
durchgehend oberhalb 32768, berührt die Grenze nur exakt am tiefsten
Punkt. Bewusst nur für Movement-FX aktiv, nicht für manuelle/Joystick-
Tilt-Steuerung (dort gibt es keine Formgebung zu schützen).

Root Cause jetzt vollständig verstanden und dokumentiert (siehe
`mapping_sheds_160w_3in1_gobo.md` → CH3/CH4 für den vollständigen
technischen Befund), Fix aktiv und geflasht. Kein echter Fix ohne
Fixture-Reparatur/Encoder-Neukalibrierung durch den Hersteller möglich —
dieser Software-Workaround ist die bestmögliche Abhilfe von unserer
Seite. `pio run` gegenkompiliert, Firmware mehrfach per `pio run -t
upload` geflasht, Ergebnis jeweils live per `curl`-Telemetrie-Sampling
gegengeprüft (kein Rateversuch an irgendeiner Stelle dieser Untersuchung).

## 2026-08-20, fünfte Fortsetzung — Zentrum-Verschiebungs-Fix limitierte die Tilt-Range zu stark, danach per Kalibrier-Sweep als falsch diagnostiziert widerlegt und komplett entfernt

Zwei unabhängige Probleme mit dem Fix aus dem vorigen Eintrag, in dieser
Reihenfolge aufgedeckt:

**Erstes Problem — Fix kappte Pattern-Spitzen.** User meldete: „jetzt
scheint alles gedeckelt, geht nicht mehr über einen bestimmten Punkt
hinaus." Root Cause: die Zentrum-Verschiebung prüfte nur die *untere*
Grenze (32768), nie die *obere* (65535) — bei Size ≳50 % (oder kleineren
Sizes mit `rot`, da `|sin|+|cos|` bis zu √2 reicht) landete die
verschobene Kreis-Spitze regelmäßig über 65535 und wurde vom
`constrain()` einfach flachgeklappt. Fix (zunächst): `ry` und die
Verschiebung auf den tatsächlich verfügbaren „sicheren" Bereich
(32768–65535, nur die Hälfte der gesamten Range) herunterskaliert, statt
den harten Clamp die Spitze kappen zu lassen. Live per 60 %-Size-Test
verifiziert (Trajektorie nutzt die komplette sichere Zone, keine
Kappung mehr) — Details siehe Backlog-Historie, dieser Zwischenstand
wurde nicht separat committed.

**Zweites, grundsätzliches Problem — der ganze Ansatz beruhte auf einer
falschen Diagnose.** User meldete danach: „ich kann den Effekt nicht
mehr korrekt ausrichten, z. B. Clover über Tilt 127 fahren — das ging
früher, so taugt der Fix nicht." Statt weiter am Symptom zu patchen,
wurde die ursprüngliche Annahme (nicht-monotone DMX→Winkel-Abbildung
genau bei DMX-Tilt ~127) direkt am Gerät geprüft: geführter, statischer
Kalibrier-Sweep über CH4 (nicht über Movement FX — reine, unbewegte
`/set_all`-Werte), 90 bis 165 in kleinen Schritten, jeder Wert ca. 2,3 s
gehalten, User filmte dabei durchgehend das fixture-eigene „Sensor
Monitor"-Menü. Zwei Aufnahmen mussten wiederholt werden (erste sendete
versehentlich `c3`/PAN statt `c4`/TILT — eigener Fehler, sofort korrigiert
und mit vorherigem Countdown neu aufgenommen; zweite lief noch mit
zwischenzeitlich auf `0` gesetzter Tilt-Kalibrierung, User wies darauf
hin, bevor ausgewertet wurde — Kalibrierung erst auf `-037` zurückgesetzt,
dann dritte, gültige Aufnahme).

Für die Auswertung wurde `tesseract` (OCR) versucht, lieferte auf der
Dot-Matrix-LCD-Schrift aber nur Datenmüll — stattdessen wurden 30 Einzel-
frames exakt zu den bekannten Sende-Zeitstempeln extrahiert (Timing über
die Datei-`creation_time`-Metadaten mit den eigenen Kommando-Logs
korreliert), zu Kontaktbögen zusammengesetzt und von Hand abgelesen.
Ergebnis: `Tilt Codewheel Step` steigt **glatt und monoton** über den
gesamten Bereich (DMX 93→Schritt -12, 108→+1, 120→+11, 127→+19,
128→+20, 135→+24, 150→+37, 165→+50) — keine Umkehrung, keine
Unstetigkeit, keine Anomalie genau am vermuteten Übergangspunkt. Das
widerlegt die Kern-Annahme, auf der der gesamte Fix aufbaute.

User bestätigte zusätzlich: der Fehler ist **nicht geschwindigkeitsabhängig**
(auch bei sehr langsamer Bewegung weiterhin eine Acht) und **nicht durch
den Encoder erklärbar** — der eingefroren wirkende `Tilt Codewheel Step`
aus dem früheren Movement-FX-Video war vermutlich ein Artefakt der
LCD-Menü-Refresh-Rate bei schneller Animation, kein echter Tracking-Fehler.
Tatsächliche Erklärung: ein fester physischer Defekt an einem absoluten
Tilt-Winkel, der nur beim tatsächlichen *Durchfahren* dieses Winkels
auftritt (unabhängig von Geschwindigkeit) — eine gehaltene, unbewegte
Position (wie im Kalibrier-Sweep) löst ihn nie aus, weshalb der Sweep
sauber aussah, obwohl der Effekt real ist.

Auf User-Wunsch („remove the fix, it doesn't work this way, i need to
aim patterns through that point on purpose") wurde die gesamte
Zentrum-Verschiebungs-Logik aus `MovementEngine::getValues()`
(`FX_Engine.h`) wieder entfernt — kein Software-Fix ist möglich, wenn ein
Pattern absichtlich durch den defekten Winkel fahren soll, und die
automatische Verschiebung stand dieser Absicht mehr im Weg, als sie half
(zusätzlich zum eigenen Kappungs-Bug oben). `mapping_sheds_160w_3in1_gobo.md`
→ CH3/CH4 enthält jetzt die vollständige Diagnose-Historie inkl. aller
geprüften und verworfenen Hypothesen (Kalibrierung, DMX-Mapping,
physischer Defekt), inklusive der vom Fixture-Menü abgelesenen
Tilt-Codewheel-Range (-90 bis +130, `+021` = gerade nach oben) und
Pan-Codewheel-Range (-83 bis +517) als Referenzwerte für künftige
Untersuchungen. `pio run` gegenkompiliert, Firmware geflasht und live
per `curl` gegengeprüft (Gerät erreichbar, normaler Betrieb).

## 2026-08-21 — Root Cause der Figure-8 per Web-Recherche gefunden (Gimbal-Pol-Singularität, kein Defekt), neuer Fix implementiert und kompiliert, Hardware-Verifikation steht noch aus

User bat darum, im Netz nach ähnlichen Problemen bei anderen Moving
Heads zu suchen, statt die Ursache als endgültig ungeklärten
Hardware-Defekt stehen zu lassen — „das kann ja nicht sein, dass wir
die einzigen sind, die das Problem haben."

**Recherche.** Web-Suche fand praktisch identische Bug-Reports in
mehreren unabhängigen Lighting-Foren: QLC+ (Thread-Titel fast wortgleich
zu unserem Bug: „head doesnt move in circle but rather a figure 8"),
Avolites („When pointing straight up/down this will indeed result in an
8 figure"), grandMA2 (dieselbe Verzerrung beim Kreuzen der Pan/Tilt-
Home-Werte) und DMXControl Projects (deutsch, mit expliziter
geometrischer Herleitung: ein Kreis unter dem Fixture braucht eine volle
360°-Pan-Drehung, kreuzt die Bahn dabei die Vertikale, wird sie zur
Acht). Übereinstimmender Mechanismus über alle Quellen: ein
Pan/Tilt-Mover ist ein 2-Achsen-Gimbal — am Zenit (Beam entlang der
Pan-Rotationsachse) wird Pan geometrisch degeneriert (jeder Pan-Wert
ergibt dieselbe physische Richtung, „Gimbal Lock", dieselbe Mathematik
wie die Pol-Singularität in Kugelkoordinaten). Ein per unabhängigem
Sinus/Cosinus auf Pan/Tilt gezeichneter Kreis — genau das, was praktisch
jeder Konsolen-Kreisgenerator inkl. unserer `MovementEngine::getValues()`
macht — ist nur dann ein echter Kreis im physischen Raum, wenn er den
Pol nicht kreuzt; kreuzt er ihn, faltet sich dieselbe DMX-Bahn zu einer
Acht. Erklärt lückenlos jede bisherige Beobachtung aus den Vorgänger-
Sessions (siehe 2026-08-20-Einträge oben): die „perfekte Kreis"-DMX-
Telemetrie stimmte (im DMX-Wertebereich ist die Bahn das auch), der
Kalibrier-Sweep war zurecht glatt monoton (er testete nur die
Encoder-Abbildung, nicht die Kreis-nahe-dem-Pol-Projektion), der Effekt
ist geschwindigkeitsunabhängig (rein geometrisch, keine Dynamik), und
tritt exakt um den Zenit-Winkel auf. **Fazit: kein physischer Defekt
dieser Einheit — jeder Pan/Tilt-Mover zeigt dieses Verhalten.**

**Rückfrage des Users, ob es doch lösbar ist.** User schlug vor: wenn
man bei Tilt=127 einfach eine volle 360°-Pan-Drehung durchführt, müssten
die Koordinaten doch wieder stimmen. Das ist tatsächlich exakt der
korrekte Spezialfall der allgemeinen Lösung — am Pol selbst ist „Radius
konstant halten, Pan über 360° sweepen" die geometrisch korrekte
Kreisbahn, keine Krücke.

**Fix implementiert.** `MovementEngine::getValues()` (`FX_Engine.h`)
verwendet jetzt statt der reinen kartesischen Addition (`centerP + rx`,
`centerT + ry`) pro Sample eine Blend-Formel zwischen diesem alten
linearen Modell (unverändert für alles, was weit vom Zenit entfernt
bleibt — keine Verhaltensänderung dort) und einem polaren Modell (Pan =
Azimut des Offset-Vektors `atan2(rx,ry)`, Tilt = Radius vom Pol) für
genau die Samples, deren naives Ergebnis nahe am Zenit landet. Die
Blend-Breite skaliert mit der Pattern-Größe. Dadurch wird — anders als
beim in der vorigen Session entfernten, zentrum-verschiebenden Fix — nur
der tatsächlich betroffene Teil eines Patterns korrigiert (z. B. nur die
Spitze eines „Clover", das über den Zenit reicht), nicht das ganze
Pattern verschoben; absichtliche User-Positionierung durch den Zenit
bleibt dadurch möglich. Stale gewordener Kommentar in
`Moving_Head_Horizon.ino` (Zeile ~496, referenzierte noch den entfernten
Fix) korrigiert.

**Noch offen:** `pio run` kompiliert sauber (Flash 91.5 %, keine
Größenregression), aber das Gerät war zum Zeitpunkt dieser Session weder
per USB noch im WLAN erreichbar — kein Flash, keine Live-Verifikation
möglich. User meldet sich am Montag mit Zugriff auf den Controller zurück.
Bekannte, selbst dokumentierte Einschränkung des neuen Fixes: ein exakt
auf dem Zenit zentriertes Pattern kann pro Umdrehung einen kleinen
Pan-Sprung zeigen (kein State-basiertes Phase-Unwrapping über mehrere
Frames) — vorbehaltlich Live-Test, ob das in der Praxis auffällt. Siehe
`mapping_sheds_160w_3in1_gobo.md` → CH3/CH4 und `backlog.md` → „Bekannte
kleine Issues" für den vollen, aktualisierten Stand.

## 2026-08-24 — Blend-Fix live getestet, per Telemetrie und Live-Beobachtung als fehlerhaft erkannt, komplett zurückgenommen

Gerät wieder verfügbar (USB **und** WLAN erreichbar). Firmware mit dem in
der vorigen Session gebauten Blend-Fix geflasht (`pio run -t upload`,
erfolgreich, Gerät antwortete danach normal über `/api/get_dmx`).

**Test 1 — Kreis exakt auf dem Zenit zentriert** (Fixture stand bereits
auf `cp=32767, ct=32767`, also exakt am Pol — der schwierigste Testfall,
identisch zum vom User selbst vorgeschlagenen „360°-Pan-bei-Tilt=127"-
Szenario): `/fx?a=1&t=1&zs=30&ze=30&ss=20&se=20` gesetzt, 20 Samples über
`liveOutPan0`/`liveOutTilt0` per `curl`-Polling mitgeschnitten. Ergebnis:
Tilt blieb **nicht** konstant wie erhofft, sondern sprang zwischen zwei
Clustern hin und her (~23000–25000 und ~40000–42500 — beides ≈ Pol
± Pattern-Radius, aber abwechselnd auf beiden Seiten statt auf einer
Seite bleibend), während Pan dazwischen ebenfalls unstete Sprünge
zeigte. Root Cause selbst identifiziert, bevor der User dasselbe live am
Fixture sah: die Formel entschied „welche Seite des Pols" (`poleSide`)
pro Sample anhand des Vorzeichens des *naiven linearen* Tilt-Ergebnisses
(`outT_lin`) — für einen exakt um den Zenit zentrierten Kreis wechselt
dieses Vorzeichen aber zweimal pro Umdrehung (immer wenn `ry` durch 0
geht), wodurch der polare Korrektur-Zweig den Tilt-Ausgang zwischen
`Pol+Radius` und `Pol-Radius` hin- und herspringen ließ, statt ihn (wie
für einen sauberen Kreis um den Pol nötig) konstant auf einer Seite zu
halten, während nur Pan durchläuft.

User bestätigte unabhängig dasselbe Ergebnis am realen Fixture: „nein,
das funktioniert so nicht. jetzt haben wir quasi einen durchgestrichenen
Kreis, auch wieder iwie am Ende eine Acht. also nehmen wir den fix
raus." Ein Reparatur-Ansatz wurde durchdacht (immer eine feste Polseite
verwenden, z. B. immer `Pol+Radius`, und die tatsächliche Richtung
komplett über Pan/`atan2(rx,ry)` abbilden, da Pan mit vollem
Azimut-Bereich ohnehin jede Richtung erreichen kann) — auf expliziten
User-Wunsch aber **nicht mehr live ausprobiert**, sondern direkt
verworfen. `FX_Engine.h` und `Moving_Head_Horizon.ino` per
`git checkout -- FX_Engine.h Moving_Head_Horizon.ino` komplett auf den
letzten committeten Stand (`07c5d38`, der dokumentierte
„kein Software-Fix"-Zustand) zurückgesetzt, neu kompiliert (`pio run`,
erfolgreich) und erneut geflasht (`pio run -t upload`, erfolgreich) —
Gerät läuft jetzt wieder exakt mit dem vor dieser Zwei-Sessions-Recherche
dokumentierten, unveränderten Code.

User merkte zusätzlich an, das Fixture bewege sich insgesamt sehr
geschmeidig, „in meiner Erinnerung war das davor nicht so" — das ist
real und unabhängig von der heutigen Recherche: die
Multi-Beat-BPM-Sync-Jitter- und `modSp`-Einheiten-Bugs (siehe deutlich
frühere Session, Commit `92ab41f`) wurden lange vor dieser Untersuchung
gefixt und sind durch den heutigen `git checkout` unangetastet geblieben
(der betraf ausschließlich `MovementEngine::getValues()`s letzten
Projektionsschritt und einen Kommentar).

**Stand danach:** kein Software-Fix für die Zenit-Acht aktiv (siehe
`mapping_sheds_160w_3in1_gobo.md` → CH3/CH4 und `backlog.md` für den
identifizierten Formel-Bug als Ausgangspunkt für einen künftigen
Versuch). Die Root-Cause-Recherche selbst (Gimbal-Pol-Singularität,
branchenweites Phänomen, kein Defekt dieser Einheit) bleibt gültig und
dokumentiert — nur der zweite Fix-Versuch wurde zurückgenommen.

## 2026-08-25 — LIVE-UI-Politur (Schriftgröße, Preset-Grid, Save Center) plus lange State-Sync-Bugjagd (9 echte Root Causes)

**Teil A — UI-Politur, auf User-Feedback zu einem LIVE-Tab-Screenshot.**
Drei Punkte: Schrift/Buttons zu klein im Dunkeln (Viewport-Meta hat
`user-scalable=no` — Pinch-Zoom ist auf Mobilgeräten aktiv deaktiviert, ein
eingebackener Default ist der einzige Weg zu größerer UI auf dem Handy), der
Programmer-Tab-Slot-Picker war ein nacktes `<select>` ohne Anzeige, was
gerade geladen ist, und der Wunsch, nur die Center-Position eines bereits
programmierten Slots neu zu speichern, ohne den vollen Programmer-Load/Edit/
Save-Umweg zu gehen (fehleranfällig).

Umgesetzt: CSS-`zoom` auf `.tab-scroll` (nicht auf `html`/`body` — siehe
Korrektur weiter unten) mit umschaltbarem Faktor (100/115/130 %, Default
115 %, Toggle in der `StatusBar`, `localStorage`-persistiert); gemeinsame
`PresetGrid`-Komponente (Zahl + Name + Aktiv-Highlight, tap=recall/
hold=save) aus dem LIVE-Tab in den `horizon-primitives.jsx`-Scope gehoben
und in beiden Tabs (LIVE + Programmer) verwendet, ersetzt dort das alte
`<select>`; neue `SaveCenterButton`-Komponente + `/save_center?slot=N`-Route
(`WebAPI.h`) — mutiert nur `chaserScenes[slot-1].dmx[CH_PAN/CH_PAN_FINE/
CH_TILT/CH_TILT_FINE]` direkt im RAM-Cache und schreibt den Blob ohne
`prefs.clear()` zurück (der Name-Key bleibt dadurch unangetastet), kein
Einfluss auf FX-Parameter. Programmer-Tab bekam zusätzlich einen eigenen
„unsaved changes"-Tracker (`PROG_DIRTY_KEYS`, Baseline-Snapshot statt reinem
Boolean, damit ein Save-Center-Save nur die pan/tilt-Baseline aktualisiert,
nicht versehentlich einen echten offenen FX-Edit verdeckt) — warnt vor
Recall nur, wenn seit dem letzten Recall/Save tatsächlich etwas geändert
wurde, nicht bei jedem Tap.

**Erste Korrektur (noch selbe Session):** `zoom` auf `html`/`body` gesetzt
brach das Scrollen komplett — die App hat dort bewusst `overflow: hidden`
(kein Rubber-Banding am Chrome), der eigentliche Scroll-Container ist
`.tab-scroll` weiter unten im Baum. Zoom auf `.tab-scroll` selbst verschoben
— StatusBar/Tab-Leiste bleiben fixe Chrome-Größe, der Inhalt darunter wächst
und bleibt trotzdem scrollbar.

**Teil B — die eigentliche Bugjagd: "no slot active" beim Preset-Wechsel,
später "Parameter werden zwischen Presets übertragen", zuletzt einzelne FX
(Dimmer FX vor allem) kommen beim Recall nicht zuverlässig wieder.** Neun
echte, unabhängige Root Causes über viele Testrunden gefunden — jede per
Live-Telemetrie verifiziert (neues Debug-Feld `"gsrc"` in `/api/get_dmx`,
siehe unten), nicht nur vermutet:

1. **`/chaser` setzte `activePresetSlot=0` bei *jedem* Aufruf mit
   `act≠1`**, nicht nur bei einem echten Running→Stopped-Übergang — das
   Frontend ruft diese Route aber routinemäßig zum Chaser-Konfig-Sync auf,
   unabhängig von Presets. Fix: Guard auf `wasActive && !chaserActive`
   (`WebAPI.h`).
2. **`/set_all` hatte denselben Bug** für den einmaligen Baseline-Push
   direkt nach Seitenladen. Fix (Zwischenstand, später ganz anders gelöst
   — siehe Punkt 7): Guard auf `anyChange` (echte Byte-Differenz gegen
   `dmxData`).
3. **Die Optimistic-Write-vs-Poll-Race selbst wurde von einem Wall-Clock-
   Timer (`dirtyUntilRef`, „vertrau dem lokalen Wert für ~2,5 s") auf einen
   echten Generation-Counter umgebaut:** `stateGen` (`Moving_Head_Horizon.ino`),
   von jeder mutierenden Route zurückgegeben statt „OK", Frontend merkt sich
   pro Feld „warte auf mindestens Generation G" (`pendingGenRef`/
   `isLocalDirty` in `data/index.html`) statt eine Dauer zu raten.
4. **`colorOff` wurde nie vom Poll zurückgelesen**, nur `colorBase` — und
   nur bei exaktem Treffer in der 10-Werte-`COLORS`-Liste, was für den
   „Rotation"-Farbmodus (Range 100–255) nie zutraf. Sobald einmal ein
   Rotation-Preset geladen wurde, blieben `colorBase`/`colorOff` dauerhaft
   auf einem falschen Wert eingefroren und korrumpierten den nächsten
   `/set_all`-Echo für ein *anderes* Preset. Fix: eigene, nach `v` sortierte
   `COLOR_ZONES`-Tabelle (die Anzeige-`COLOR_BASES` ist absichtlich nicht
   sortiert), symmetrisch zum bereits bestehenden Gobo-Zonen-Muster.
5. **`panFine`/`tiltFine` wurden unnötig via `/set_all` zurückgesendet**,
   obwohl kein UI-Element sie je direkt editiert — sie sind ein reiner
   Spiegel des Joystick-getriebenen Backend-Zustands, der wegen der
   Pan/Tilt-Smoothing-Schleife (~30 ms-Takt) kontinuierlich driftet. Der
   Echo war deshalb fast immer schon veraltet, wenn er ankam, was
   `/set_all`s Byte-Diff-Check als „echte Bearbeitung" las. Fix: Tracking
   für diese beiden Kanäle komplett entfernt (`track()`-Aufrufe in
   `data/index.html`).
6. **Der Poll-Merge für Dimmer/Gobo-Rotation/Prisma-Rotation/Color-Base/
   Sgobo-Base/Rgobo-Base prüfte `prev.XFxRunning`** (den Wert *vor* diesem
   Poll) statt des in genau diesem Poll frisch berechneten Werts — ließ nach
   jedem FX-Stop (z. B. weil ein neu recalltes Preset diese FX nicht nutzt)
   einen vollen ~2-s-Zyklus, in dem der manuelle Wert am alten,
   eingefrorenen Pre-Stop-Snapshot hing, während `track()`s „einmal
   force-resenden"-Logik genau diesen veralteten Wert zurückschickte. Fix:
   FX-Running-Flags werden jetzt *vor* dem manuellen Kanal-Merge im selben
   `setState`-Callback berechnet und dort verwendet.
7. **`/set_all`s `anyChange`-Inferenz („war das eine echte Bearbeitung")
   erwies sich als grundsätzlich unzuverlässig**, nicht nur punktuell
   lückenhaft — nach vier Einzelfixen (Punkte 4–6 plus die ursprüngliche
   `panFine`-Idee) tauchte bei jedem erneuten Live-Test ein weiterer,
   bisher unsichtbarer Fall auf. Pragmatische Entscheidung: die
   `activePresetSlot`-Reset-Logik komplett aus `/set_all` entfernt.
   `activePresetSlot` ändert sich seither nur noch über `/recall`,
   `/kill_fx` und einen echten `/chaser`-Stop — ein stabiles „zuletzt
   recalltes Preset" statt eines oft falschen „…und seither nichts
   geändert".
8. **Trotzdem blieb echte Daten-Korruption bestehen** (bestätigt per
   Telemetrie: derselbe Slot zweimal hintereinander recallt, unterschiedliche
   `ch8`/`ch9`/`dA`-Werte) — Ursache war die syncFx-eigene „letzter
   gesendeter Stand"-Baseline (für die 8 FX-/Chaser-State-Snapshots) UND
   `track()`s parallele Pro-Kanal-Baseline, die beide nie mit
   Poll-getriebenen Änderungen nachgezogen wurden. Ein frisch recalltes
   Preset sah für `syncFx()`/`track()` deshalb wie eine unversendete lokale
   Änderung aus und wurde redundant zurück ans Gerät geschickt — landete
   dieser Echo verzögert *nach* einem zweiten, schnell folgenden Recall,
   überschrieb er dessen frisch geladene Kanäle mit den alten Werten. Fix:
   beide Baselines (`pr2.*` für `syncFx`, `chP['ch'+ch]` für `track()`)
   werden jetzt direkt im Poll-Merge auf den gerade übernommenen Wert
   nachgezogen, bevor der nächste Effect-Durchlauf sie vergleicht.
9. **Trotzdem blieb Dimmer FX spezifisch flackerig** (alle anderen FX-Typen
   waren nach Punkt 8 stabil). Zwei letzte, tiefer liegende Ursachen:
   - `tFetch`s Debounce-Queue kann den Callback eines wartenden (noch nicht
     gestarteten) Aufrufs stillschweigend verlieren, wenn ein dritter
     Aufruf für dieselbe Debounce-ID eintrifft, bevor der zweite startet
     (Queue hat nur einen Slot, wird einfach überschrieben). Verlorener
     Callback = `pendingGenRef`-Eintrag bleibt für den Rest der Session bei
     `Infinity` hängen, UND wird durch Punkt 8s eigene Reconciliation sogar
     „bestätigt" (die Baseline übernimmt den eingefrorenen falschen Wert),
     wodurch nie wieder ein neuer Sync-Versuch ausgelöst wird — ein
     selbstverstärkendes Deadlock. Fix: 3-Sekunden-Sicherheitsnetz
     (`armPending`) — löst einen hängengebliebenen `Infinity`-Wert nach
     spätestens 3 s automatisch, statt für den Rest der Session zu hängen.
   - `/set_all`s Dimmer-Kanal-Handling hat einen Seiteneffekt, den kein
     anderer Kanal hat: `if(i==CH_DIMMER) dimFX.stop();` — ein veralteter,
     noch unterwegs befindlicher `/set_all`-Echo (derselbe Race wie Punkt 8,
     nur diesmal über eine andere Route) konnte dadurch `dimFX.active`
     zurücksetzen, direkt nachdem ein Recall es gerade wieder aktiviert
     hatte. Das war der letzte, hartnäckigste Fall — die reine
     Frontend-Reconciliation aus Punkt 8 reichte hier nicht, weil der
     Race nicht „wird ein Echo verschickt", sondern „ein *bereits*
     verschicktes Echo landet spät" war, was am Frontend allein nicht mehr
     zuverlässig verhinderbar ist.

     **Finaler, robusterer Fix (Backend statt weiterer Frontend-Zeit-
     Tricks):** Generation-basierte Staleness-Ablehnung. `/recall` merkt
     sich seine eigene `stateGen` als `lastRecallGen`
     (`Moving_Head_Horizon.ino`). `/fx`, `/modfx`, `/colfx`, `/sgobfx`,
     `/rgobfx` und `/set_all`s `dimFX.stop()`-Zweig akzeptieren jetzt einen
     `&g=`-Parameter (die vom Frontend zuletzt gesehene `stateGen`,
     `lastKnownGenRef` in `data/index.html`) und lehnen die Anwendung ab
     (`isStaleWrite()`-Helper, `WebAPI.h`), wenn `g < lastRecallGen` — ein
     Request, der gebaut wurde, bevor das Frontend überhaupt vom letzten
     Recall wusste, kann diesen dadurch nicht mehr überschreiben, egal wie
     spät er tatsächlich ankommt. Das ist die einzige der neun Ursachen,
     die nicht rein durch Frontend-Reconciliation lösbar war, weil sie
     eine Race zwischen zwei bereits abgeschickten Requests ist, nicht
     zwischen „senden oder nicht senden".

**Debug-Instrumentierung, bewusst dauerhaft behalten** (wie `op`/`ot` und
`rawBPM`/`rawMs`/`loopMax` vorher schon): `stateGen`/`lastGenSource`/
`bumpGen()` (`Moving_Head_Horizon.ino`) plus `"gsrc"` in `/api/get_dmx`
(`WebAPI.h`) — zeigt live, welche Route die letzte State-Änderung
verursacht hat. War der eigentliche Schlüssel, um Punkt 8/9 überhaupt von
reinem Channel-Diff-Rätselraten zu unterscheiden — ohne dieses Feld wäre
insbesondere Punkt 9 (Race zwischen zwei bereits gesendeten Requests, nicht
zwischen Senden/Nicht-Senden) praktisch nicht diagnostizierbar gewesen.

**Verifikationsmethode:** durchgehend Live-`curl`-Polling gegen
`/api/get_dmx` alle ~150–200 ms über mehrere Minuten während der User live
zwischen Presets wechselte (u. a. gezielt Preset 1 ↔ 2 mit allen sieben
FX-Typen aktiv), Rohdaten mit Python nachanalysiert (Generation-Sprünge,
Channel-Diffs pro Sample). Mehrfach zeigte sich dabei, dass eine Diagnose,
die nur auf Kanal-Diffs zwischen zwei Polls basierte, irreführend war, weil
Pan/Tilt kontinuierlich aus dem ~30-ms-Smoothing-Loop driften — unabhängig
von jeder HTTP-Mutation. Erst `gsrc` (Punkt oben) machte die Diagnose
eindeutig. Nach Fix 9 vom User über einen längeren Testlauf (alle sieben
FX aktiv, wiederholtes schnelles Hin-und-Herschalten zwischen zwei Presets)
als „ist gefixt jetzt" bestätigt.

**Offen für eine künftige Session (User-Wunsch, evtl. mit mehr Reasoning-
Budget/Opus):** Die Architektur, die diese neun Bugs überhaupt erst möglich
gemacht hat — mehrere unabhängige, asynchrone Schreibpfade
(`syncFx`/`track()`-Echo, Poll-Merge, Joystick-Smoothing, NVS-Recall), die
alle denselben `dmxData[]`/Live-State ohne zentrale Ownership anfassen —
ist strukturell fragil geblieben, auch nach allen neun Fixes. Die Fixes
selbst sind alle chirurgisch und einzeln begründet, aber es sind jetzt
spürbar viele verschiedene Nebenläufigkeits-Sicherungsmechanismen parallel
im Einsatz (`pendingGenRef`/`isLocalDirty`, `armPending`-Sicherheitsnetz,
`pr2.*`/`chP.*`-Reconciliation, `&g=`/`lastRecallGen`-Staleness-Guard). Ein
sauberer Neuentwurf (z. B. ein einziger, klar besessener Server-State mit
echtem Request/Response statt Fire-and-forget-Echos, oder WebSockets statt
Poll+Echo) wäre die eigentliche strukturelle Lösung — siehe `backlog.md`
für den neuen Eintrag dazu.

---

## 2026-08-25 (2) — Der eigentliche Grund, warum die neun Fixes vom selben Tag nicht gereicht haben: eine gemeinsame Schreibpfad-Lücke statt N Einzelbugs

**Meldung des Users:** „Es gibt immer noch Probleme, wenn man Werte im UI
auswählt, die dann in weniger als einer Sekunde zurückgesetzt werden. Wählt
man den Wert erneut aus, wird er übernommen. Das betrifft alle Einstellungen
und Werte, z. B. Trigger-Modi in den FX, Static-Gobo-Slot und so weiter. Es
wirkt, als wäre der Bug in allem oder ein grundlegendes Sync-Problem, das
beim Programmieren des Fixtures sehr nervt."

Die Einschätzung des Users war exakt richtig, und sie war der Schlüssel: Es
waren **nicht** N Einzelbugs, sondern **ein gemeinsamer Schreibpfad, der
Edits verwirft**, plus **ein gemeinsamer Merge-Pfad, der sie zurücksetzt**.
Die neun Fixes vom selben Tag haben jeweils *ein konkretes Feld* geschützt
(die sieben `*FxRunning`-Booleans, `showRunning`, `presetActive`,
`presetNames`, `colorOff`, `panFine`/`tiltFine`) — also genau die Felder, die
damals symptomatisch aufgefallen waren. Die FX-*Parameter* und die manuellen
DMX-Kanäle, zusammen der Großteil dessen, was der Programmer-Tab überhaupt
editiert, hatten diesen Schutz nie. Deshalb las sich das Symptom jetzt als
„betrifft alles".

### Drei Root Causes

1. **Primär: die 300-ms-Totzone nach jedem Poll verwirft Edits ersatzlos.**
   Jeder Schreibpfad im State-Sync-Effect ist mit `!isReceiving.current`
   abgesichert — einem 300-ms-Fenster, das jede `/api/get_dmx`-Antwort öffnet.
   Der Guard sendet korrekterweise nicht und lässt die „zuletzt gesendet"-
   Baseline korrekterweise unangetastet. Aber: die Dependency-Liste des
   Effects ist `[state]`, und `isReceiving` ist eine **ref** — das Zurücksetzen
   auf `false` löst keinen Render aus, der Effect läuft also nie erneut.
   **Niemand hat den Edit je wiederholt.** Er blieb ungesendet liegen, bis
   dasselbe Feld zufällig noch einmal geändert wurde; der nächste Poll hat
   derweil den alten Backend-Wert darüber gemerged. Bei 2 s Poll-Intervall
   sind das rund 15 % aller Edits. Der Kommentar an dieser Stelle beschrieb
   diese Fehlerklasse bereits wörtlich („silently dropped with no retry until
   this exact field happened to change again") — die Baseline-Hälfte war
   gefixt, die fehlende Aufweck-Hälfte nicht.
2. **Der Poll-Merge überschreibt FX-Parameter ohne jeden Dirty-Gate.**
   `isLocalDirty()` wurde nur auf die acht Running-Booleans plus
   `presetNames`/`presetActive` angewendet; jeder Parameter derselben Gruppe
   (`next.fxTr = d.fTr ?? prev.fxTr` usw.) wurde ungefiltert gemerged. Da
   `syncFx` per `armPending(runningKey)` nur den *Running*-Key armiert, blieb
   der Start/Stop-Button korrekt gelatcht, während die Regler daneben
   zurücksprangen. Schlimmer: der `pr2.*`-Baseline-Rewrite direkt danach hat
   die zurückgesetzten Werte als „zuletzt gesendet" verbucht — damit stimmten
   State und Baseline auf dem *alten* Wert überein, der Diff sah „nichts zu
   senden", und der Edit war **endgültig verloren** statt nur um einen Zyklus
   verzögert. Genau das ist der Grund, warum ein zweites Auswählen nötig war.
3. **Manuelle Kanäle hatten überhaupt keinen Generation-Schutz.** `/set_all`
   hat weder `bumpGen()` aufgerufen noch eine Generation zurückgegeben: der
   Aufruf war `server.send(200, "OK")` — die Zwei-Argument-Überladung, die
   `"OK"` als *Content-Type* sendet und den Body leer lässt. Live am Gerät
   verifiziert (`Content-Type: OK`, `Content-Length: 0`). Das Frontend hatte
   also nichts zu lesen, selbst wenn es gewollt hätte, und armierte
   entsprechend auch nichts. Static-Gobo-Slot, Farbe, Fokus, Zoom, Strobe,
   Prisma, Makro und Gobo-Index waren damit ungeschützt.

### Gleiche Fehlerklasse, mitgefixt

- `prism`/`frost` nutzten `?? 0` statt `?? prev.x` — eine fehlende oder
  abgeschnittene Antwort kippte den Toggle auf `false`, statt ihn zu lassen.
- `presetActive` wurde ohne `d.pr != null`-Guard zugewiesen: fehlte das Feld,
  las sich `undefined > 0` als `false` und der Slot-Indicator wurde **aktiv
  gelöscht** — ein zweiter, vom Generation-Race unabhängiger Weg zum
  „no slot active"-Flackern.
- `setBpm(d.bpm)` war ungegated (Tap-Tempo sprang zurück), und `if (d.bpm)`
  hat eine legitime 0 verschluckt.
- `setMuted`/`setMicOn` wurden **innerhalb** des `setState`-Updaters
  aufgerufen (unreiner Reducer; feuert unter StrictMode-Doppelaufruf oder
  verworfenem Concurrent-Render doppelt) und ebenfalls ungegated.
- `/chaser` sendete kein `&g=` und hatte keinen `isStaleWrite()`-Guard — die
  einzige FX-Gruppe ganz ohne Staleness-Schutz.

### Separater Bug, ebenfalls gefixt: Joystick-Config wurde still überschrieben

Die neun `/joy_cfg`-Werte (Speed, Kurve, Momentum, Pan/Tilt-Reverse, die vier
Pan/Tilt-Limits) werden nach NVS persistiert und beim Boot restauriert — aber
**keine Route hat sie je zurückgeliefert**. Ein frisch geladener Browser-Tab
hielt daher die hartcodierten Defaults aus dem State-Initialisierer, und die
erste beliebige State-Änderung ließ den `joyKey`-Diff feuern und überschrieb
die gespeicherte Geräte-Config mit genau diesen Defaults. Stiller Config-
Verlust bei **jedem Reload**, unabhängig vom Revert-Bug.

### Ausgeschlossen

`useTelemetry` (500-ms-Loop auf `/api/state`) schreibt **nicht** in den
geteilten `state` — nur Ping/Qualität und die Beat-LEDs. Der Backlog-Punkt
„zwei unsynchronisierte Polling-Loops" ist hier also *keine* Ursache; es gibt
genau einen Merge-Pfad.

### Umsetzung

- **`deferSync()` + `syncTick`** (Frontend): Statt einen Edit im
  isReceiving-Fenster zu verwerfen, wird ein einziger, idempotenter 320-ms-
  Timer gesetzt, der `syncTick` erhöht; `syncTick` steht jetzt in der
  Dependency-Liste des Effects, der Edit wird also nach dem Fenster gesendet.
  Ein Timer für den ganzen Effect, nicht einer pro Feld. Statisch geprüft:
  **alle sechs** Schreibpfade mit `isReceiving`-Guard rufen jetzt `deferSync()`.
- **Gating pro FX-Gruppe** statt pro Feld: `fxDirty`/`dimDirty`/… werden einmal
  aus dem ohnehin vorhandenen `runningKey` berechnet und gaten sowohl die
  Parameter der Gruppe **als auch** deren `pr2.*`-Baseline. Das ist die
  wichtigere Hälfte — ohne sie wird der Edit gelöscht statt verzögert.
- **`chDirty`/`'channels'`** für die 13 getrackten Kanäle, armiert vom
  `/set_all`-Batch. Pan/Tilt (+Fine) bleiben bewusst ungegated: sie liegen nie
  im Batch und müssen dem Joystick weiter folgen.
- **Firmware:** `/set_all`, `/beat`, `/masterdim`, `/smooth`, `/trans`,
  `/autofade`, `/unmute`, `/hwaudio` liefern jetzt die Post-Write-Generation
  statt eines nackten `"OK"`; `/chaser` bekam den `isStaleWrite()`-Guard; neue
  Route `/api/joycfg` als Read-back (eigene One-Shot-Route statt zusätzlicher
  Felder in `/api/get_dmx`, das schon ~960 Bytes aus ~50 sequenziellen
  `String +=` baut und alle 2 s gepollt wird).

### Nebenbefund: OTA hat noch nie funktioniert

`ArduinoOTA` war eingebunden und `ArduinoOTA.handle()` lief in jedem
Loop-Durchlauf — **`ArduinoOTA.begin()` wurde aber nirgends aufgerufen**, es
gab also nie einen OTA-Listener und `handle()` tat nichts. `README.md` hat OTA
trotzdem als Feature beworben. `begin()` ist jetzt in `setup()` ergänzt (nur im
Station-Modus), die README-Zeile korrigiert. Konsequenz für diese Session:
Das Deployment ging **nicht** remote — es braucht einmalig ein USB-Flash
(Firmware + Filesystem); ab dann ist OTA real nutzbar.

### Verifikation

`pio run` und `pio run -t buildfs` sauber. Zusätzlich alle acht
`<script type="text/babel">`-Blöcke mit dem **mitgelieferten** Babel
(`data/vendor/babel.min.js.gz`) transformiert — dieselbe Transformation, die
das Gerät beim Laden macht; es gibt keinen Build-Schritt, ein Syntaxfehler
würde sich sonst erst als leere UI am Gerät zeigen. Die Diagnose zu Root
Cause 3 wurde vor dem Fix live am laufenden Gerät bestätigt
(`curl -D -` auf `/set_all` → `Content-Type: OK`, `Content-Length: 0`).
**Noch offen: der Praxistest am Fixture nach dem USB-Flash** (siehe
`handoff.md`).

### Nachtrag zur selben Session: geflasht und live nachgemessen

Der oben als „noch offen" notierte Punkt ist teilweise erledigt — der User gab
grünes Licht für USB-Flashen, also wurde direkt deployt:

```
pio run -t upload   --upload-port /dev/cu.usbmodem1101
pio run -t uploadfs --upload-port /dev/cu.usbmodem1101
```

Firmware nach `0x10000`, Filesystem nach `0x290000`, beides mit Hash-Verify.
**NVS blieb erwartungsgemäß unberührt** — die Presets „Sky Moover"/„yellow
three" waren nach dem Reboot unverändert vorhanden (genau der Unterschied zum
`firmware.factory.bin`-an-`0x0`-Unfall vom 2026-08-20).

Danach die drei Firmware-Änderungen direkt am laufenden Gerät nachgemessen:

- **`/set_all`** antwortet jetzt `Content-Type: text/plain`,
  `Content-Length: 1` mit einer echten Generation. Vor dem Fix am selben Gerät
  gemessen: `Content-Type: OK`, `Content-Length: 0`. Damit ist Root Cause 3
  vorher *und* nachher belegt, nicht nur aus dem Code abgeleitet.
- **`/api/joycfg`** liefert
  `{"spd":2000,"crv":2.00,"mom":50,"prv":0,"trv":0,"pmin":0,"pmax":255,"tmin":0,"tmax":255}`.
- **FX-Parameter-Round-trip:** `/sgobfx` mit Trigger-Modus 2, Sync 4, Hold 1500,
  Start/End 2/5 geschrieben (bei `a=0`, also inaktiv — keine Lichtänderung am
  Fixture) → in `/api/get_dmx` exakt so wieder ausgelesen, `gen` 25→26,
  `gsrc` = `sgobfx`, und die Response war die neue Generation `26`. Genau der
  Wert, gegen den das Frontend den Poll jetzt gated. Testwerte anschließend
  wieder auf den Ursprungsstand zurückgesetzt.

**OTA wurde ebenfalls verifiziert statt nur behauptet:** Die Partitionstabelle
ist echtes Dual-OTA (`app0`/`app1` je 1280K plus `otadata`), die Firmware
belegt 1216K. Ein kompletter Firmware-Upload über WiFi
(`espota.py -i 192.168.8.113 -p 3232 -f .pio/build/supermini/firmware.bin`)
lief durch („Done...") und das Gerät kam sauber wieder hoch. Die
`README.md`-Zeile zu OTA ist damit nicht nur korrigiert, sondern auch belegt —
künftige Iterationen brauchen kein USB mehr.

**Weiterhin offen:** der eigentliche Bedien-Test im Browser (15× Trigger-Modus
umstellen, Static-Gobo-Slot, Regler, Joystick-Config über einen Reload hinweg,
plus Regressionstest der neun Fixes vom selben Tag). Der braucht echte Klicks
und ließ sich nicht automatisieren — alles davor ist gemessen, nicht vermutet.

---

## 2026-08-26 — Bedien-Test am Gerät: der Programmer-Sync-Fix ist abgenommen

Der am Vortag als „offen" notierte Praxistest wurde gefahren. Ergebnis: **vom
User abgenommen** („soweit alles gut").

### Methode (relevant für künftige Sync-Bugs)

Die primäre Fehlerart des behobenen Bugs — ein **verworfener** Edit — ist
geräteseitig grundsätzlich *unsichtbar*: der Wert ändert sich einfach nicht,
es gibt nichts zu messen. Deshalb wurde nicht „beobachtet", sondern gezählt:
der User machte im Browser eine **abgezählte** Menge Änderungen, während
parallel ein Recorder `/api/get_dmx` alle 600 ms abfragte und jede
geräteseitige Zustandsänderung mit Zeitstempel, `gen` und `gsrc` in eine
JSONL-Datei schrieb. Damit wird die Frage objektiv beantwortbar:
**N Klicks rein = N Writes an?**

Nachahmenswert, falls dieses Problemfeld nochmal aufkommt — Skript-Kern: alle
FX-Parameter, Running-Flags und manuelle DMX-Kanäle pollen, aber die
FX-getriebenen Kanäle (`1` Dimmer, `9` Gobo-Rotation, `11` Prisma-Rotation)
**ausschließen**, sonst ertrinkt das Log in Rauschen von laufenden Effekten.

### Ergebnis

| Test | Pfad | Ergebnis |
|---|---|---|
| FX-Trigger/Sync/Hold | `syncFx`, `gsrc: sgobfx` | 10 Aktionen → 10 angekommen |
| Static-Gobo-Slot | `/set_all`, `gsrc: set_all` | 9 Aktionen → 9 angekommen |
| Joystick-Config über Reload | `/api/joycfg` | überlebt |
| Leerlauf ~45 s | — | null Writes, `gen` konstant bei 113 |

Einzelbefunde:

- **Jede** Zustandsänderung hatte eine eigene, lückenlos hochgezählte
  Generation. Es hat sich also nichts ohne echten HTTP-Write verändert —
  genau der Unterschied zum alten Verhalten, bei dem der Poll Werte
  zurückschrieb.
- Die 9 Gobo-Slot-Änderungen kamen alle mit `gsrc: set_all` und je eigener
  Generation an. Das ist der direkte Beleg für Root Cause 3: vor dem Fix hat
  `/set_all` überhaupt keine Generation vergeben.
- **~45 s Leerlauf ohne einen einzigen Write** belegen den Baseline-Lockstep:
  eine untätige UI echot gepollte Werte nicht mehr zurück. Ein anfangs
  auffälliger Burst (~64 `set_all`-Bumps in 30 s) war *nicht* die früher
  vermutete Echo-Schleife, sondern schlicht Slider-Drags des Users — er hörte
  auf, sobald der User aufhörte.
- `/api/joycfg` meldete nach dem Reload `spd 652, crv 4.20, mom 12, pmax 68,
  tmax 153`, also klar abseits der UI-Defaults, die der alte Code beim ersten
  Klick nach einem Reload zurückgeschrieben hätte.

**Ehrliche Einordnung der Aussagekraft:** Unter dem alten Code hatte jede
Änderung ~15 % Chance, in die 300-ms-Totzone zu fallen. Bei 19 Änderungen
wären ~3 Verluste zu erwarten gewesen; null Verluste hätten damals eine
Wahrscheinlichkeit von rund 5 % gehabt. Das ist ein deutliches, aber für sich
genommen kein zwingendes Signal — zusammen mit der Beobachtung des Users, dass
im Browser nichts mehr zurücksprang, als Abnahme gewertet.

### Nicht getestet

**Test 5 (Preset-Regression)** — schnelles Wechseln zwischen Presets mit allen
sieben FX aktiv — wurde übersprungen. Falls im normalen Betrieb doch wieder
„no slot active" flackert, Parameter zwischen Presets überlaufen oder ein FX
beim Recall ausbleibt: dort gezielt ansetzen, es ist derselbe geteilte
Schreib-/Merge-Pfad.

### Nebenwirkung des Tests

Die Pan/Tilt-Limits stehen seit dem Joystick-Test auf `pmax 68` / `tmax 153`
und sind persistent in NVS. Das schränkt den Bewegungsbereich real ein —
notiert, damit das nicht später als Mechanik- oder Movement-FX-Fehler
fehldiagnostiziert wird.

---

## 2026-08-26 (2) — Movement-FX „hackt" bei unterschiedlichen Size-Werten: kein Bug, aber ein anderer daneben

**Meldung:** „Wenn ich Size Start und Size End auf unterschiedliche Werte setze,
gibt es z. B. beim Clover hackende bzw. ruckelnde Movements."

### Ursache: Mode 0 ist ein Sägezahn — der Sprung ist beabsichtigt

`MOD_MODES` bietet `0 = Forward (Sawtooth)`, `1 = Up/Down (Ping-Pong)`,
`2 = Reverse (Decay)`. Bei Mode 0 (und ebenso Mode 2) rampt `mVal` linear und
**springt am Zyklusende schlagartig zurück**. `currentSize` folgt direkt:

```c
currentSize = (szSt + (szEn - szSt) * mVal) / 100.0f;
```

Solange `szSt == szEn` ist, ist dieser Sprung unsichtbar — die Größe ändert sich
ja nicht, egal was der Modulator macht. Genau deshalb fällt es erst bei
unterschiedlichen Werten auf. Beim Clover (Typ 3) skaliert die Sprunghöhe
zusätzlich mit `|sin 2p|`, fällt also je nach Musterposition mal klein und mal
brutal aus — was den unregelmäßigen, „hackenden" Eindruck erzeugt.

**Live gemessen** (Clover, Size 15→70, modSp 1000 ms, `op`/`ot` mit ~48 Hz
abgetastet, Diskontinuität = Schritt > 3× Median *und* > 2,5× beider Nachbarn):

| Modus | Diskontinuitäten in 10 s | Abstände |
|---|---|---|
| 0 Sawtooth | **9** | 1,01 / 0,98 / 1,02 / 1,00 / 0,99 / 1,01 / 1,00 s |
| 1 Ping-Pong | **0** | — |

Die Abstände treffen exakt `modSp` — damit ist der Modulator-Wrap zweifelsfrei
die Quelle. Größter Einzelsprung: **17.728 Einheiten Pan in einem Frame**, gut
ein Viertel des Gesamtbereichs.

**Kein Code-Fix.** Ein Sägezahn *soll* springen; für Dimmer und Gobo-/Prisma-
Rotation ist genau das der gewünschte Effekt. Für Positionsdaten ist praktisch
nur Ping-Pong brauchbar. Ein erster Messversuch mit Schwelle „> 6× Median" fand
*nichts* und schien die These zu widerlegen — der Sprung lag mit 4,6× knapp
darunter, weil er zufällig an einer Stelle mit kleinem `|sin 2p|` fiel. Lehre:
bei einem Muster, dessen Radius selbst schwankt, keine absolute Schwelle auf die
Schrittweite legen, sondern gegen die *lokalen Nachbarn* prüfen.

### Dabei gefunden und gefixt: drei von sechs Kurven waren im Movement wirkungslos

`Modulator::getLFO()` implementierte alle sechs Kurven, `MovementEngine::process()`
hatte davon eine **unvollständige Kopie** und behandelte nur Quadratic und Sine.
`Cubic`, `Gauss` und `Random` fielen still auf Linear zurück — obwohl beide Panels
dasselbe Dropdown (`MOD_CURVES`) benutzen. Man konnte sie auswählen, sie taten nichts.

Fix (Variante 2 nach Rücksprache mit dem User):
- Neue gemeinsame Funktion `lfoShape(p, m, c, allowRandom)` in `FX_Engine.h`;
  `Modulator::getLFO()` delegiert dorthin, `MovementEngine` benutzt sie ebenfalls.
  **Eine Definition statt zweier Kopien** — genau die Duplizierung war die Ursache
  dafür, dass beide auseinanderlaufen konnten.
- `allowRandom = false` für Movement: `Random` würfelt bei *jedem* Aufruf neu, was
  als Dimmer-Flackern brauchbar ist, die Size eines Musters aber pro Frame neu
  setzen und das Fixture durchschütteln würde. Im UI neu `MOVE_CURVES` (= `MOD_CURVES`
  ohne Random) nur für das Movement-Panel. Alte Szenen mit `fMC = 5` fallen auf
  Linear zurück — exakt das Verhalten, das sie vorher schon hatten.

**Vorher/Nachher live gemessen** (Typ 1 Kreis, dessen geometrischer Radius konstant
ist, sodass der gemessene Radius direkt `currentSize` entspricht; Ping-Pong, 15→70):

| Kurve | Mittelwert vorher | Mittelwert nachher |
|---|---|---|
| 0 Linear | 13671 | 13782 |
| 2 Cubic | 13809 | **9105** |
| 4 Gauss | 13874 | **12125** |

Vorher lagen alle drei innerhalb von 204 Einheiten (~1,5 %, Messrauschen) — also
nachweislich identisch. Nachher 4677 Einheiten Spanne, und die Formen stimmen:
Cubic drückt zum unteren Ende, Gauss betont die Mitte. Min/Max trafen mit ~4915
bzw. ~22937 exakt die konfigurierten 15 % bzw. 70 % von 32767.

### Nebenwirkung des Flashens (wichtig zu wissen)

Der Filesystem-Upload rebootet das Gerät, und **der Live-FX-Zustand liegt nicht in
NVS** — nur Presets und Chaser-Szenen werden persistiert. Nach dem Reboot standen
die Movement-Parameter daher wieder auf den Struct-Defaults (`fT=1`, Size 10..10,
`modSp=1000`), die zuvor eingestellte Live-Konfiguration war weg. Das ist
bestehendes Verhalten, kein Regressionsschaden — aber beim Flashen mitdenken:
vorher in einen Preset speichern, sonst ist die Live-Einstellung nach dem Reboot
verloren.

### Plausibilitätsprüfung des restlichen FX-Codes (2026-08-26, auf Wunsch des Users)

Nach dem Kurven-Fund gezielt nach *derselben Fehlerklasse* gesucht — „UI bietet
etwas an, das die Firmware nicht oder anders umsetzt". Ergebnis: außer den bereits
gefixten Kurven **nichts Weiteres gefunden**. Geprüft und für sauber befunden:

| Geprüft | Ergebnis |
|---|---|
| `MOVEMENT_TYPES` 1–12 ↔ `switch` in `getValues()` | vollständig, plus sicherer `default` |
| `TRIGGERS` 0–4 ↔ `checkAudioTrg` | 0/1 intern, 2/3/4 = Bass/Mid/High, alle abgedeckt |
| Rad-Maps ↔ Klemmungen ↔ UI-Listen | 20/10/7 durchgehend konsistent, Reihenfolge Voll-/Splitfarbe korrekt |
| Shake-Speed/-Range | UI 0,2–10,0 bzw. 1–5 und 0–100 % = Firmware-Klemmungen |
| Movement Size/Speed | UI 1–100 = Firmware `constrain(..., 1, 100)` |
| Chaser-Slots | UI-Werte 0–9 („Slot 1–10") = Firmware 0–9 |
| Modulator-`speed` | ist ms-Periode, deckt sich mit dem Slider |
| `runStep`-Map-Zugriffe | alle `constrain(..., 0, mapLen-1)`; `shakeBase` 226 + 5·5 + 4 = exakt 255 |
| `SceneData` vollständig | alle 57 Felder werden gespeichert *und* zurückgeladen |
| `dt`-Ersatz bei `dt <= 0` | in der Praxis unkritisch: gemessene Sägezahn-Periode 0,98–1,02 s bei konfigurierten 1000 ms |

**Ein eigener Fehlalarm, dokumentiert als Warnung für die Zukunft:** Aus
`MovementEngine`s `constrain(sync, 0, 7)` und der globalen `syncBeats[7]` wurde
zunächst ein Out-of-Bounds-Zugriff geschlossen — mit dem Zusatz, die
`MOVE_SYNCS`-Labels seien invertiert. **Beides falsch.** Die Messung (Typ 8
Pan-Sweep, Nulldurchgänge bei 120 BPM) ergab exakt die versprochenen Werte:
sy=0 → 1,00 Beats/Umdrehung, sy=3 → 8,07. Grund: `updateEngines()` übergibt der
Movement-Engine `moveSyncBeats[8]`, nicht `syncBeats[7]` — der gleichnamige
Funktionsparameter hatte in die Irre geführt. Der User bestätigte anschließend auch
die Design-Absicht: Bewegungen müssen länger als einen Beat dauern dürfen, Bruchteile
eines Beats schafft die Mechanik nicht. Lehre: bei zwei ähnlich benannten Tabellen
**die Aufrufstelle** prüfen, nicht den Parameternamen — und im Zweifel messen, bevor
man einen „Speicherfehler" meldet.

### Nachtrag 2026-08-27: Test 5 (Preset-Regression) nachgeholt und bestanden

Der einzige noch offene Nachweis aus dem 2026-08-26-Test ist erbracht: schnelles
Preset-Wechseln mit aktiven FX zeigte laut User „keine Auffälligkeiten" — kein
„no slot active"-Flackern, keine zwischen Presets überlaufenden Parameter, kein
beim Recall ausbleibendes FX. Damit ist die Abnahme des Programmer-Sync-Fixes
vollständig, und die neun Fixes vom 2026-08-25 sind implizit mitgeprüft, weil sie
denselben geteilten Schreib-/Merge-Pfad benutzen. **Kein offener Bug im Projekt.**

---

## 2026-08-27 — Echte FFT statt „fake FFT": Frequenztrennung für Low/Mid/High

**Anlass:** Der User will die Mid-/High-Trigger tatsächlich nutzen können (z. B.
Strobe auf Hi-Hat) und fragte, ob KISS FFT ressourcenmäßig in Frage kommt.

### Warum die bisherige Lösung das nicht konnte

Die drei „Bänder" waren `bassEnergy = envSlow`, `midEnergy = envMid - envSlow`,
`highEnergy = envFast - envMid` — also **Differenzen dreier Envelope-Follower mit
verschiedenen Zeitkonstanten**. Das trennt nach *Anstiegsgeschwindigkeit*, nicht
nach Frequenz: Hi-Hat, Snare und ein Klick sind dafür ununterscheidbar, „Bass" ist
schlicht der geglättete Gesamtpegel. Genau deshalb war 2026-08-20 der Bug möglich,
dass `midEnergy` strukturell ~0 war — bei echten Frequenzbändern kann das nicht
passieren.

### Zur Bibliotheksfrage: KISS FFT ist nicht schneller

Dass KISS FFT auf einem 8-Bit-Mega läuft, heißt nur, dass sie *klein* ist. Sie ist
Mixed-Radix und generisch (beliebiges N, eigener State, Dispatch pro Butterfly); eine
fest auf N=256/Radix-2 zugeschnittene Integer-FFT ist kleiner und schneller.
**Entscheidend ist nicht die Bibliothek, sondern Fixed-Point vs. Float.** Der C3 ist
RV32IMC ohne FPU — eine Float-FFT konkurriert direkt mit der Soft-Float-Last der
Movement-Engine. Das ist mit hoher Wahrscheinlichkeit auch die Erklärung für den
gescheiterten FFT-Versuch des Users vor Monaten („ESP stieg aus, Fixture bewegte sich
nicht mehr").

### Vorab gemessen statt geschätzt

Mit einer temporären Sonde im Build gemessen, bevor irgendetwas umgebaut wurde:
eine N=256-Fixed-Point-FFT kostet **+718 Bytes Flash und +2 KB RAM**. Die fertige
Implementierung inkl. Umbau und Instrumentierung: **+2.434 Bytes Flash
(1.202.887 → 1.205.321), +3.368 Bytes RAM**. Ressourcen waren also nie das Argument —
weder dafür noch dagegen.

### Umsetzung

- **N=256 @ 8 kHz** → 31,25 Hz/Bin, 32 ms Frame. Bänder: Bass Bin 1–5 (31–156 Hz),
  Mid 6–38 (187–1187 Hz), High 80–127 (2500–3968 Hz).
- **8 kHz bleibt.** Die zwischenzeitliche Idee „2 kHz reicht" wurde verworfen: sie
  würde das High-Band komplett löschen, und genau das ist der gewünschte Hi-Hat-Trigger.
- **Alles Integer:** Q15-Twiddles, Hann-Fenster in Q15, `>>1` pro Stage gegen
  int16-Überlauf, Magnitude per Alpha-Max-Beta-Min statt `sqrtf` (128 emulierte
  Wurzeln pro Frame wären teurer gewesen als die ganze FFT). Verifiziert: im Code von
  `fftRun()` steht keine einzige Float-Operation; `cosf`/`sinf` laufen ausschließlich
  einmal beim Boot in `fftInitTables()`.
- **Eingangs-Shift `>>16`** statt `>>14`: 24-Bit-Daten links im 32-Bit-Wort ergeben so
  exakt den int16-Bereich ohne Clipping. Der Legacy-Wert 14 würde int16 überlaufen —
  dort harmlos, weil nur `abs()` daraus gelesen wird, für eine FFT fatal.
- **Eigene dynamische Schwellen für Mid und High.** Vorher wurden beide gegen die
  *Bass*-Schwelle gemessen. Mit echten Bändern trägt ein Hi-Hat viel weniger Energie
  als eine Kick, die bass-abgeleitete Schwelle läge dauerhaft über dem gesamten
  High-Band und der Trigger würde nie auslösen — also genau der Anwendungsfall.
  Jetzt feuert jedes Band, wenn es über seinen *eigenen* gleitenden Mittelwert steigt.
- **Nicht blockierend:** `i2s_read` mit Timeout 0. Ein unvollständiger Frame wird
  übersprungen statt abgewartet — ein blockierender Read hier würde DMX und Bewegung
  anhalten, also genau die Fehlerart von damals. DMA-Ring auf 4×256 Samples (~128 ms
  Reserve) vergrößert, Poll-Kadenz auf 32 ms = exakt die Frame-Dauer, damit Verbrauch
  und Produktion sich decken.
- **Überlaufschutz:** Bandmittelwert wird vor dem Gain-Shift auf 200000 geklemmt,
  Shift auf 0–10 begrenzt. Ohne das könnte ein lauter Passus int32 überlaufen und als
  negative Energie („Stille") erscheinen.

### Rückfallschalter und Messbarkeit

- `/audio_tune?fft=0` schaltet **ohne Reflash** auf das alte Envelope-Verfahren
  zurück; `fft=1` zurück. Zustand steht in `/api/audio_debug` (`"fft"`).
- `/audio_tune?fg=N` setzt die Bandverstärkung (Shift 0–10, Default 4). Sie hängt vom
  realen Mikrofonpegel ab und war ohne Gerät nicht kalibrierbar — bei Werten nahe null
  erhöhen, bei Anschlag verringern.
- **Neue Kostenmessung** in `/api/state`: `audUs`/`audMax` (Audio-Poll),
  `fftUs` (nur die FFT), `engUs`/`engMax` (`updateEngines()`, die Soft-Float-Last),
  alle in µs, Spitzenwerte über ein 5-s-Fenster. Der Fenster-Reset liegt im
  Haupt-Loop, damit er auch bei abgeschaltetem Mikrofon weiterläuft.

**Status: kompiliert und geprüft, aber noch nicht auf Hardware getestet** — das Gerät
war an diesem Tag nicht verfügbar. Testplan siehe `handoff.md`.

### Nachtrag 2026-08-28: FFT geflasht und am Gerät gemessen — bestanden

Per OTA geflasht (Firmware allein, `data/` unverändert). Vorher/nachher unter Last
gemessen, jeweils mit aktiver Movement-FX (Clover, Size 60, Speed 20):

| | vorher | nachher |
|---|---|---|
| `loopMax` | Median 13, Max 14 ms | Median 13, Max 17 ms |
| Bewegungs-Aussetzer (Schritt > 5× Median) | 2 | **keine** |
| FFT pro Frame (`fftUs`) | — | **397 µs** |
| Audio-Poll gesamt (`audUs`) | — | 463 µs alle 32 ms = **~1,45 % CPU** |
| `updateEngines()` (`engUs`) | — | 274 µs, Spitze **2701 µs** |

**Das Fixture läuft flüssig** — im Lasttest kein einziger Ausreißer, vorher zwei.
Die Fehlerart des früheren Float-FFT-Versuchs tritt nicht auf.

**Korrektur einer eigenen Schätzung:** vorab waren 150–300 µs für die FFT
veranschlagt, gemessen sind es 397 µs. Unkritisch, aber die Schätzung lag daneben.

**Wichtigster Nebenbefund:** `updateEngines()` hat Spitzen bis **2701 µs** — rund
das Siebenfache der FFT. Damit ist erstmals belegt, dass die Soft-Float-Mathematik
der Bewegung der eigentliche Kostenträger ist und nicht das Audio. Das stützt den
Backlog-Punkt „Output-Build-Kadenz" mit einer echten Zahl statt einer Vermutung.

### Frequenztrennung verifiziert

10 s Aufzeichnung bei Raumgeräusch über `/api/audio_debug`:

```
lo (Bass)  112…1085     mi (Mid)  72…319     hi (High)  15…179
Korrelation:  lo/mi 0,47   lo/hi 0,32   mi/hi 0,33
```

Niedrige Korrelation bedeutet, dass die Bänder sich **unabhängig voneinander**
bewegen — genau das, was das alte Verfahren strukturell nicht konnte (dort lagen
alle drei nahe 1,0, weil sie aus demselben Gesamtpegel abgeleitet waren).

**Rückfallschalter in beide Richtungen getestet** — und er machte das alte Problem
in einer Zeile sichtbar: mit `?fft=0` meldete das Gerät `lo=1522` bei `mi=44,
hi=57`, Mid und High also praktisch tot; mit `?fft=1` dann `lo=2949, mi=152,
hi=101`. Bandverstärkung `fg=4` passt (Werte weder nahe null noch am Anschlag),
eine Kalibrierung war nicht nötig.

**Offen: Feinabstimmung mit echter Musik.** Im Test lief nur Raumgeräusch. Mid hatte
0 Treffer (Schwelle 301 bei Maximum 319 — erwartbar), High dagegen 9 Treffer aus
reinem Rauschen. Falls High in Stille zu oft feuert: `?htd=3` oder `?nf=200`.

---

## 2026-08-28 (2) — Beat-Detection für Drum & Bass: Spectral Flux und ein Fehl-Lock

**Meldung:** „So richtig geil läuft der Beat-Detector nicht, gerade Drum & Bass ist
schwer." Track lief laut User mit **178 BPM** (= 337 ms pro Beat).

### Erst gemessen, bevor an Reglern gedreht wurde

Ein Sensitivity-Sweep zeigte, dass die Regler **nicht** der Hebel sind: über den
gesamten Bereich blieb die Trefferzahl bei 22–26 pro 16 s, während die Abstände mit
einer Standardabweichung von 200–594 ms um einen Median von ~550 ms streuten. Bei
der ursprünglichen Einstellung `sens=15` lag die Schwelle sogar *über* dem Signal
(`th`=9222 gegen `lo`=6709) — der Faktor ist `2,0 − sens/100`, also 1,85.

**Ursache:** Der Detektor vergleicht den *absoluten Pegel* des Bassbands gegen dessen
gleitenden Mittelwert. Drum & Bass hat einen durchgehend rollenden Sub-Bass, der
diesen Mittelwert permanent hoch hält — der Kick ragt kaum heraus. Für diese Musik
ist der Detektortyp strukturell falsch, unabhängig von jeder Einstellung.

### Fix 1: Spectral Flux

Statt „Pegel über Mittelwert" jetzt die **Zunahme** der Magnitude von Frame zu Frame
(`fftFlux()`), negative Änderungen verworfen. Gehaltener Sub-Bass hat nahezu keinen
Flux, ein Kick-Anschlag eine deutliche Spitze. Kosten: eine Subtraktion pro Bin — die
FFT lief ohnehin schon. Der Rohflux geht **ungeglättet** in den Detektor; ein Onset
*ist* eine Einzelframe-Spitze, die Attack/Decay-Hüllkurve würde genau sie verschmieren.

**A/B am selben Track gemessen** (wahrer Beat 337 ms):

| Modus | Treffer | Abstand (Median) | Streuung |
|---|---|---|---|
| Pegel | 14 | 1396 ms | 1144 ms |
| **Flux** | **43** | **349 ms** | 243 ms |

349 ms gegen 337 ms — der Flux-Detektor findet den Puls, der Pegel-Detektor nicht.
Umschaltbar über `/audio_tune?flux=0|1`.

### Fix 2: Der BPM-Wert konnte sich nie erholen

Trotz sauberer 349-ms-Intervalle meldete `globalBPM` weiter 131. Grund war das
Aufnahme-Gate `bestError < currentInterval / 5`: bei `globalBPM=131` (458 ms) und
einem echten Intervall von 349 ms beträgt die Abweichung 109 ms bei 91 ms Toleranz —
**die korrekten Intervalle wurden verworfen, weil sie dem falschen BPM widersprachen.**
Ein sich selbst verstärkender Fehl-Lock; der einzige Ausweg war ein exakter Stand von
`BPM_DEFAULT_FALLBACK`. Die Oktav-Korrektur half nicht, weil das Verhältnis 1,43 war,
kein Faktor 2.

Jetzt wird **jedes plausible Intervall** in die Historie geschrieben (Ausreißer
verwirft ohnehin der 16er-Median), und bei mehr als `BPM_RELOCK_PERCENT` (20 %)
Abweichung schnappt `globalBPM` direkt auf den Median statt sich mit 5 % pro Sample
heranzukriechen. Live verifiziert: BPM bewegt sich seither frei (151 → 137) statt
festzuhängen.

Zusätzlich: `BPM_MAX_LIMIT` von 180 auf 200 (178 lag praktisch auf der Grenze), und
`tuneDynThreshSmoothShift` ist endlich über `/audio_tune?dts=` erreichbar — der
Parameter war bis dahin überhaupt nicht von außen einstellbar.

### Was noch NICHT gelöst ist

Der BPM-Wert landet weiterhin nicht auf 178. Die Intervalle liegen über alle
Sensitivity-Stufen bei 395–464 ms, also konstant beim 1,17–1,38-fachen des Beats, bei
einer Streuung von nur ~80 ms — also ein eigenes, falsches Raster, kein Rauschen.
Vermutung: Flux feuert auch auf Zwischenschläge, und die 280-ms-Sperre
(`MIN_BEAT_INTERVAL_MS`) fasst sie tempo-blind zusammen.

**Offener Widerspruch, ehrlich vermerkt:** eine unabhängige Autokorrelation über den
Bass-Flux (von außen abgetastet) findet die stärkste Periodizität bei 68 BPM (882 ms)
und ein Cluster um 115–117 BPM — **kein 337-ms-Raster**. Entweder lief zu diesem
Zeitpunkt ein anderer Track als die genannten 178 BPM, oder der Abschnitt hatte keinen
durchgehenden Kick im Bassband. Das ist vor weiteren Schlüssen zu klären.

**Nächster Kandidat, falls es weitergehen soll:** ein echter Tempo-Tracker statt
Median über Intervalle — Ringpuffer der Flux-Werte (4 s = 128 Frames) und
Autokorrelation über die Lags für 60–200 BPM, einmal pro Sekunde. Kosten grob
16k Integer-Operationen/s, also vernachlässigbar. Das ist das Standardverfahren und
löst genau das verbliebene Problem (Puls finden statt Einzelabstände mitteln).

### 2026-08-28 (3) — Tempo-Tracker plus manueller Oktav-Schalter

Der Median über Onset-Abstände scheitert an synkopiertem Material grundsätzlich.
Ersetzt durch einen **Autokorrelations-Tracker** über einen 6-s-Ringpuffer des
Bass-Flux: harmonische Summe (Lag plus 2×/3×/4×) findet die Puls-Familie,
parabolische Interpolation verfeinert den Peak (bei 31 Frames/s liegen benachbarte
Lags bei 176 BPM sonst 17 BPM auseinander).

**Der entscheidende Kniff war, die Score-*Kurven* zu glätten, nicht die
Entscheidung.** Mit Auswertung pro Sekunde sprang die Ausgabe bei gleichbleibendem
Material zwischen 179, 89 und 72; mit exponentiell gemittelten Per-Lag-Scores
(~10 s Evidenz) hält sie einen Wert. Zusätzlich behoben: `>>4`/`>>6`-Skalierungen
hatten die Autokorrelation auf einstellige Werte gestaucht, sodass Quantisierungs-
rauschen die Oktave entschied statt des Signals.

**Korrektur einer eigenen Behauptung.** Die geplante automatische Oktav-Wahl war mit
einer Offline-Messung begründet (plain(176)=13211 gegen plain(88)=402). Auf den
Gerätedaten ist es **umgekehrt**: 9466 bei 89 BPM gegen **−1101** bei 179 — bei
341 ms ist die Autokorrelation negativ. Ursache ist der schnelle Leaky-Mean im Gerät,
der anders hochpassfiltert als der globale Mittelwert meiner Offline-Auswertung. Im
Bassband dieses Tracks gibt es schlicht keine 176er-Periodizität; die Kicks liegen
echt auf dem Halftime-Raster. Der Tracker misst korrekt, die Prämisse der Heuristik
war falsch.

**Deshalb: kein Automatismus, sondern ein Schalter.** `/audio_tune?tmul=0|1|2`
(wie gemessen / ×2 / ÷2) plus ein Panel im AUDIO-Tab. Begründung im Code und im UI-
Text: Jede Regel, die aus 89 automatisch 178 macht, macht aus 90er-Hip-Hop 180 — der
User hatte genau das zu Recht ausgeschlossen. Der Override wird **ignoriert statt
geklemmt**, wenn das Ergebnis aus `[BPM_MIN_LIMIT, BPM_MAX_LIMIT]` fiele; live
verifiziert: bei gemessenen 134 lässt ×2 den Wert unverändert (268 wäre zu hoch),
÷2 liefert 67.

Der Tracker folgt dem Material nachweislich: im Verlauf des Tests wanderte er von 89
auf 156 auf 178, als der Abschnitt Kicks auf dem schnellen Raster bekam.


---

## 2026-08-31 — AUDIO-Panel als echtes Diagnosewerkzeug, und Audio-Tuning endlich persistent

**Anlass, Wort des Users:** „zum debuggen taugt das panel aktuell nicht so gut." Dazu die
konkrete Beobachtung, dass die alte „fake FFT" den Bass subjektiv besser erkannt habe.

### Der Bass-Eindruck hatte einen realen Kern — aber anders als vermutet

Im Legacy-Pfad war „Bass" **gar kein Bassband**: `s = abs(raw_samples[i] >> 14)` ist die
Breitband-Amplitude, und `envSlow` deren langsame Hüllkurve. Der alte Bass-Trigger feuerte
also auf alles Laute — Snare, Synth, Vocals. Das wirkt lebendig, war aber Lautstärke- und
keine Basserkennung. Das neue Band ist echte 31–156 Hz und damit deutlich selektiver.
Gegen „schlechter" spricht die Messung vom 2026-08-28 (Pegel: 14 Treffer/20 s bei 1396 ms
Median; Flux: 43 Treffer bei 349 ms).

### Gefunden: sechs Regler im Panel waren wirkungslos

Im aktiven Pfad (FFT + Flux) werden `envSlow/envMid/envFast` **direkt** aus dem Flux gesetzt,
die Attack/Decay-Shifts also komplett übersprungen. Damit taten `fa`, `fd`, `ma`, `md`, `sa`,
`sd` nichts — dieselbe Fehlerklasse wie die drei toten Movement-Kurven vom 2026-08-26:
Bedienelemente, die etwas versprechen, das die Firmware nicht einlöst. Sie werden jetzt
ausgegraut und als INACTIVE markiert, sobald der Detektor auf Flux steht, mit Begründung im
Panel statt im Code.

### Neu im Panel

- **Spektrum-Anzeige:** alle 128 Bins über den neuen Endpunkt `/api/spectrum`, logarithmisch
  skaliert mit langsam abfallendem Peak-Hold. Die drei Bänder liegen als eingefärbte Flächen
  darüber, mit Grenzfrequenzen beschriftet — man sieht also direkt, welche Bins als Bass, Mid
  und High gewertet werden. Eigener Endpunkt, weil 128 Zahlen `/api/audio_debug` etwa
  verdoppeln würden und beide unterschiedlich schnell gepollt werden.
- **Mic-Eingangspegel** mit Peak-Hold und **Clipping-Anzeige** (`pk`/`clip`). Das ist die eine
  Zahl, die die skalierten Graphen prinzipiell nicht zeigen können: übersteuert der Eingang,
  hilft keine Schwelleneinstellung mehr.
- **Skalierung umschaltbar:** pro Band (neuer Default), gemeinsam, oder absolut. Die frühere
  gemeinsame Normierung war der Grund, warum Mid und High flach am Boden lagen — sie tragen
  schlicht viel weniger Energie — und warum Stille wie Musik aussah.
- **Beat-Marker für alle drei Bänder** statt nur Bass: grün über die volle Höhe, Mid als kurzer
  gelber Balken oben, High darunter in Cyan. Die Frage „feuert Mid überhaupt jemals?" war vorher
  am Graphen nicht zu beantworten.
- **Eigene Schwellenlinien je Band** (`th`, `thM`, `thH`), jede in der Farbe ihres Bandes.
- **Bandgrenzen zur Laufzeit einstellbar** (`bbl/bbh/bml/bmh/bhl/bhh`, in FFT-Bins à 31,25 Hz,
  mit Hz-Beschriftung). Motiv: viele Kicks tragen Energie deutlich über die voreingestellten
  156 Hz hinaus, die bisher im Mid-Band landete.
- **Alle wirksamen Parameter als Controls:** Sensitivity, Noise Floor, Bandverstärkung,
  Schwellen-Nachführung (`dts`), Mid-/High-Trim, plus die Schalter FFT / Flux / Tracker / Oktave.

### Audio-Tuning überlebt jetzt den Reboot

Bis hierher war die gesamte Audio-Kette **runtime-only** — jeder Flash und jeder Stromausfall
hat alles auf die Compile-Defaults zurückgesetzt, genau die Falle, die beim Live-FX-Zustand
noch offen ist. 19 Ints und 4 Bools werden jetzt in NVS (`sys`) gespeichert und beim Boot in
`setupAPI()` geladen.

**Wichtig dabei: die Schreibvorgänge sind entprellt.** Ein Slider feuert pro Rastschritt einen
Request; würde jeder davon NVS schreiben, wären das hunderte Flash-Erase-Zyklen pro
Tuning-Session. Änderungen setzen nur ein Dirty-Flag, `flushAudioPrefs()` im Loop schreibt
frühestens 1,5 s nach der letzten Änderung einmal. Beim Laden werden die Bandgrenzen zusätzlich
geklemmt, damit ein defekter NVS-Wert kein invertiertes Band und keinen Zugriff außerhalb des
Spektrums erzeugen kann.

**Status: kompiliert und syntaxgeprüft, aber nicht auf Hardware getestet** — das Gerät war an
diesem Tag nicht erreichbar, Flashen erst Montag. Firmware 92,5 % Flash, `data/` 783 KB von
1408 KB.

### Nachtrag 2026-08-31: Absturz des AUDIO-Tabs (selbst verursacht) und Verifikation am Gerät

**Meldung:** „wenn ich audio tab abrufe kommt nichts im browser."

**Ursache war ein Fehler aus dem Commit vom 2026-08-28** (Tempo-Panel): der Panel-Block
stand **außerhalb** der `tune`-Absicherung und griff direkt auf `tune.tmul` zu. `tune` ist
beim ersten Render `null` (wird erst vom ersten `/api/audio_debug`-Poll gefüllt), also warf
die Komponente sofort einen TypeError und React brach den gesamten Renderbaum ab — leere
Seite, zuverlässig bei jedem Aufruf. Der Fehler blieb bis hierher unentdeckt, weil die
Tempo-Arbeit danach ausschließlich per `curl` verifiziert wurde und niemand den Tab im
Browser geöffnet hat. **Lehre: eine UI-Änderung ist nicht verifiziert, solange sie nur über
die API geprüft wurde.**

Die neu gebaute Fassung hat den Fehler nicht — sämtliche `tune.*`-Zugriffe liegen innerhalb
von `{tune && (…)}`; gegengeprüft, bevor geflasht wurde.

**Am Gerät verifiziert (Firmware + Filesystem per USB):**

- `/api/spectrum` liefert 128 Bins à 31 Hz; mit eingeschaltetem Mikrofon tragen 78 davon
  Energie — die Kette bis zur Anzeige steht.
- Bandgrenzen und Mic-Pegel erscheinen korrekt in `/api/audio_debug`.
- **NVS-Persistenz mit echtem Neustart geprüft**, nicht angenommen: Testwerte gesetzt
  (`bbh=7`, `nf=250`, `fg=5`, `sens=70`, Mikrofon an), 3 s gewartet (Entprellung 1,5 s),
  per OTA neu gestartet — nach dem Boot waren **alle fünf Werte unverändert vorhanden**.
  Anschließend auf die Standardwerte zurückgesetzt.
- Presets „Sky Moover"/„yellow three" haben beide Flash-Vorgänge überstanden.

### 2026-08-31 (2) — „Dimmer FX trifft den Beat nicht": drei Ursachen, live gemessen

Meldung des Users bei laufender Musik mit **150 BPM**: Dimmer-FX trifft den Beat nicht,
mit Global-BPM etwas besser, auf Kickdrum unbrauchbar. Zusatzinfo: lauter geht nicht,
das Mikrofon sitzt bereits 10 cm vom Lautsprecher.

**Ursache 1 — Eingangspegel viel zu niedrig, Schwelle über dem Signal.** Gemessen:
Mic-Peak median 1231 / max 3136 von 32767, also **9,6 % Aussteuerung**. Bassband median
384 gegen Schwelle median 840 — die Schwelle lag also die meiste Zeit *über* dem Signal,
nur die lautesten Kicks kamen durch. Da physisch kein Pegel mehr zu holen war, jetzt
**digitale Eingangsverstärkung** (`ig`, 0–5 = ×1…×32), angewendet vor der FFT. Live
eingemessen: ×8 ist die höchste Stufe ohne Clipping (Peak max 24688, 0 Clips), das
Bassband steigt damit von 384 auf ~4960 und liegt sauber über der Schwelle.

**Ursache 2 — Sensitivity konnte die Schwelle nie unter den Mittelwert bringen.** Die
Abbildung war `2,0 − sens/100`, erreichte also bestenfalls Faktor 1,0. Bei sparsamen
Onsets liegt der laufende Mittelwert aber dicht an den Peaks. Jetzt `2,0 − sens×0,015`,
Bereich also 2,0…0,5. Der im Sweep beste Wert liegt bei **Faktor 0,95** — vorher schlicht
nicht einstellbar:

| sens | Faktor | Abstand (Median) | vs. 400 ms Beat |
|---|---|---|---|
| 55 | 1,18 | 418 ms | 1,05 |
| **70** | **0,95** | **404 ms** | **1,01** |
| 85 | 0,73 | 361 ms | 0,90 |
| 100 | 0,50 | 342 ms | 0,86 |

**Ursache 3 — der Tempo-Tracker meldete 164 statt 150.** 9 % Fehler, bei BPM-Sync also
ein ganzer Beat Versatz pro 4 Sekunden. Grund war die Lag-Auflösung: bei 32 ms pro Frame
ist ein 400-ms-Beat 12,5 Frames, benachbarte Lags liegen **~16 BPM** auseinander. Fix:
die Periode wird jetzt aus der höchsten brauchbaren **Oberwelle** bestimmt (Peak bei k·L
suchen, verfeinern, herunterteilen) — bei Lag 25 ist der Fehler halb so groß, bei 37 ein
Drittel. Ergebnis: **acht Messungen hintereinander exakt 150**.

Die erste Fassung dieses Fixes änderte gar nichts, wegen zweier eigener Denkfehler: das
Suchfenster war mit ±1 Frame zu eng (liegt die Basis 1,1 Frames daneben, ist der Peak bei
k=2 schon 2,2 Frames entfernt), und die Plausibilitätstoleranz von 0,25 Frames verwarf
genau die Korrektur, um die es ging. Fenster wächst jetzt mit k, Toleranz 1,2 Frames.

**Kein Bug, aber eine Falle für den Bediener:** Bei Audio-Trigger (`trigger >= 2`) läuft
der Modulator weiter mit `speed` als Periodendauer, der Beat setzt nur `phase = 0`. Mit
`speed = 2000 ms` und Kicks alle 400 ms erreicht die Phase pro Beat nur 0,2 — der Dimmer
durchläuft also **nur 20 % seines Bereichs**. Für Audio-getriggerte Modulatoren muss
`speed` ungefähr der Beatlänge entsprechen (400 ms bei 150 BPM), sonst sieht es aus, als
täte der Effekt nichts.

### 2026-08-31 (3) — Audio-Trigger: Sync-Teiler wirkte nie, und die Beat-Uhr erbte den Jitter

Weitere Meldungen desselben Tests, alle mit laufender Musik gegengemessen.

**Der Sync-Teiler galt bei Audio-Triggern überhaupt nicht.** Der User hatte
„Kickbass + 1 Beat" eingestellt — richtig gedacht, aber die Firmware wertete `sync` nur
bei `trigger == 1` aus. Bei Audio-Triggern lief der Modulator mit `speed` als
Periodendauer, der Kick setzte lediglich `phase = 0`. Verschärfend: die UI blendet den
Speed-Regler bei jedem Nicht-Manuell-Trigger aus (`{trigger === 0 ? <slider> : <sync>}`),
der wirksame Wert war also gar nicht erreichbar und stand aus einem Preset auf 2000 ms.
Bei Kicks alle 400 ms erreichte die Phase damit nur 0,2 — der Dimmer durchlief ein
Fünftel seines Bereichs, was als „flackert mit gedimmten Zwischenstufen" ankam. Dieselbe
Fehlerklasse wie die toten Kurven und die toten Regler: ein Bedienelement, das etwas
verspricht, das die Firmware nicht einlöst. Audio-Trigger ankern jetzt an den Beat-Zähler
und laufen über `syncBeats[sync]`.

**Erster Versuch davon war falsch** und wurde sofort vom User widerlegt: bei „4 Beats"
flashte gar nichts mehr, weder auf Kick noch auf Hi-Hat. Grund: ich verankerte bei
*jedem* Treffer neu, wodurch ein Zyklus über mehrere Beats nie über 1/n hinauskam. Jetzt
wird nur neu verankert, wenn der Zyklus zu ≥90 % durchgelaufen ist.

**Die Beat-Uhr selbst erbte den Detektor-Jitter.** `beatsElapsedTotal` ist
`beatCount + (now − lastBeatTime)/interval`, und der Audio-Pfad setzte `lastBeatTime`
bei *jedem* erkannten Onset hart auf `now`. Bei ~180 ms Streuung sprang damit die Phase
jedes BPM-synchronen Effekts — deshalb feuerte auch „Global BPM" wahllos. Ersetzt durch
eine **Phasenregelung**: Periode kommt vom Tracker, ein Onset korrigiert die Phase nur,
wenn er nahe der Vorhersage liegt (±30 %), und dann nur um ein Viertel des Fehlers.
Weit daneben liegende Onsets triggern weiterhin FX, ziehen aber die Uhr nicht mehr mit.
Eine Klemme verhindert, dass `lastBeatTime` in die Zukunft rutscht — das würde im `.ino`
wegen der vorzeichenlosen Differenz sofort überlaufen.

**Messergebnis Kickbass (18 s, Beat 390 ms):** Abstand-Median 355 → **398 ms**, Streuung
187 → **107 ms**. Der Kick-Trigger trifft das Raster jetzt.

### ⚠️ Weiterhin offen: der Tempo-Tracker rastet auf der punktierten Viertel ein

Global BPM bleibt unbrauchbar, aber **nicht** wegen der Phasenregelung: über 40 s gemessen
steht `trackedBPM` fast durchgehend auf **98**, mit gelegentlichen Sprüngen auf 147 —
und 147/98 = **exakt 1,5**. Der Tracker lockt also auf das 3:2-Verhältnis statt auf den
Beat. Weil `globalBPM` dabei zwischen beiden Werten springt, ändert sich die Beatlänge im
laufenden Betrieb und die Phase springt mit; das erklärt die 61 % Rasterabweichung im
Global-BPM-Test.

Die harmonische Summe verwirft die punktierte Viertel nur, wenn deren Vielfache *nicht*
aufgehen — bei 3:2 geht aber jedes zweite auf (1,5 · 2 = 3 Beats), sie bekommt also
Rückhalt. Der nächste Schritt ist daher eine Verhältniswahl, die **nur ganzzahlige**
Beat-Vielfache belohnt, plus Hysterese gegen das Umspringen. Bewusst nicht mehr blind
nachgepatcht — das gehört sauber entworfen.

**Praktische Empfehlung bis dahin: Kickbass + 1 Beat verwenden.** Das ist gemessen gut
und hängt gar nicht am Tempo-Wert, weil es sich an echten Onsets verankert.

### 2026-08-31 (4) — Tempo-Schätzung: drei Fehlversuche, und was die Messung am Ende zeigte

Der User fragte zu Recht, wie einfache Systeme (Pioneer DJM-500) das lösen und ob unser
Tracker nicht eine PLL sein müsste. Die Architektur *ist* eine PLL — Phase und Periode.
Die Phasenregelung steht (siehe oben), die **Periodenschätzung** war das Problem.

**Drei Verfahren wurden probiert, alle am Gerät gemessen, alle verworfen:**

1. **Autokorrelation mit harmonischer Summierung.** Rastete auf der punktierten Viertel
   ein: las 98 BPM für einen Track, den der User als 143 tappte (143 × ⅔ = 95). Harmonische
   Summierung kann dieses 1,5-Verhältnis nicht verwerfen, weil jedes zweite Vielfache
   aufgeht und es damit echten Rückhalt bekommt.
2. **Perioden-Sweep mit Ganzzahl-Bewertung der Intervalle.** Kippte in die Gegenrichtung
   und wählte die *kürzeste* Periode des Bereichs (200 BPM), weil eine kurze Periode fast
   jedes Intervall als irgendein Vielfaches erklärt. Bester und zweitbester Score lagen bei
   602 zu 601 — die Trennschärfe war praktisch null.
3. **Median der Intervalle mit Faltung.** Scheiterte, weil der Detektor auch zwischen den
   Beats feuert: der häufigste Abstand (330 ms) *ist* schlicht nicht der Beat (420 ms).

**Aktuell im Gerät: Phasentest über die Onset-Zeitpunkte** (Fourier-Komponente über die
Onset-Zeiten statt über Audio, phaseninvariant, Integer-Sinustabelle). Auf sauberem Techno
liefert er zeitweise exakt richtige Werte — 140 BPM bei echten 142 —, schwankt aber über
75 s zwischen 138 und 185 (Median 169).

**Der eigentliche Befund kam aus dem kontrollierten Test.** Auf monotonem Techno mit
142 BPM (= 423 ms) zeigte das Intervall-Histogramm einen Nebencluster bei 300–350 ms und
eine Onset-Rate von **2,67/s statt 2,37/s** — der Detektor feuert häufiger als der Beat.
Dieser Nebencluster bildet ein konkurrierendes Raster bei ~170 BPM, und genau darauf
rastet der Schätzer immer wieder ein. Ein Sensitivity-Sweep bestätigte es direkt:

| sens | Faktor | Onsets/s | Intervall-Median | tracked BPM |
|---|---|---|---|---|
| 30 | 1,55 | 2,55 | 401 ms | **148** |
| 45 | 1,33 | 1,09 | 430 ms | 182 |

Bei geringerer Empfindlichkeit kommt etwa ein Onset pro Kick, und der Tempo-Wert wandert
von ~170 auf 148 bei echten 142. **Nicht der Schätzer ist das Problem, sondern die
überzähligen Onsets** — und die optimale Empfindlichkeit ist materialabhängig (für den
D&B-Track war 70 am besten, für Techno eher 30), taugt also nicht als fester Wert.

**Designierter nächster Schritt (nicht mehr blind gepatcht):** Die Onsets im Phasentest
**mit ihrer Flux-Stärke gewichten**, statt jeden Onset gleich zu zählen. Ein schwacher
Zwischenschlag kann dann die Kicks nicht mehr überstimmen, und die Empfindlichkeit muss
nicht mehr pro Genre passen. Das ist das Standardvorgehen und adressiert genau den
gemessenen Mechanismus. Erst auf Techno verifizieren, dann gegen Breakbeat härten.

**Methodische Lehre dieser Session:** Ich habe den Schätzer anfangs direkt an
Drum & Bass mit synkopiertem Breakbeat entwickelt — dem schwierigsten denkbaren Fall — und
dabei gegen ein bewegliches Ziel gemessen, weil die Musik zwischen den Messungen wechselte
(176, 178, 150, 143, 142). Beides zusammen hat mehrere Iterationen gekostet, die bei einem
kontrollierten Signal nicht nötig gewesen wären. Erst der einfache Fall, dann härten.

**Praktisch nutzbar bis dahin:** Tempo per Tap setzen und Audio nur die Phase korrigieren
lassen — die Phasenregelung ist gemessen gut (Kickbass 398 ms bei 390 ms Beat, Streuung
von 187 auf 107 ms halbiert). So arbeiten Lichtpulte üblicherweise ohnehin.

**Nachtrag zum Einwand des Users** („der Sweep kann nicht die Lösung sein, das ist pro
Track anders"): völlig richtig, und so war er auch nicht gemeint — der Sweep war die
*Diagnose*, die zeigen sollte, dass die überzähligen Onsets die Ursache sind und nicht der
Schätzer. Die Konsequenz daraus ist eingebaut: der Phasentest **gewichtet die Onsets jetzt
nach ihrer Stärke** (Abstand zur eigenen Schwelle, 16 = genau auf Schwelle, gedeckelt bei
255) und normiert über die Gewichtssumme. Ein schwacher Zwischenschlag zählt damit weiter
mit, kann die Kicks aber nicht mehr überstimmen — die Empfindlichkeit muss nicht mehr pro
Genre passen. **Kompiliert, aber nicht getestet**: die Anlage war aus.

**Zweiter Einwand des Users:** das alte Energiekonzept sei besser gewesen. Für das
*Auslösen* ist das plausibel — ein Kick dominiert die Gesamtenergie, und ein
Breitband-Transient ist robuster als ein schmales 31–156-Hz-Band, das auch auf Bassläufe
anspricht. Für die *Tempo*-Erkennung sprach die Messung dagegen (Pegel: 14 Treffer/20 s bei
1396 ms Median; Flux: 43 bei 349 ms gegen 337 ms Beat). **Wichtige Einschränkung:** dieser
Vergleich lief mit dem alten Eingangspegel von 9,6 % Aussteuerung. Das alte Verfahren hat
nie von den heutigen Fixes profitiert — Eingangsverstärkung, erweiterter
Sensitivity-Bereich, eigene Schwellen pro Band. Beide Pfade sind weiterhin zur Laufzeit
schaltbar (`?flux=0` = Pegel-Erkennung, `?fft=0` = ursprüngliches Breitband-Energiekonzept),
der faire Vergleich steht also als erster Test der nächsten Sitzung an — ohne eine Zeile
Code.

---

## 2026-09-01 — Gemischter Ansatz: Detektor pro Band, adaptive Schwelle, 16 kHz, GUI-Umbau

Der User schlug vor, für den Bass die Bin-Pegel zu summieren (also Energie) und mit
einstellbarer Attack/Release-Zeit zu einem Envelope-Detektor zu machen, und fragte, ob das
für Höhen und Mitten ebenso gehe. Die Antwort: für Bass ja, für Mitten nein — und genau
daraus folgt ein **gemischter Ansatz**.

- **Bass → Energie.** Der Kick dominiert das Band, sein Pegel *ist* das Ereignis. Eine
  Hüllkurve darauf ist exakt das, was klassische Hardware macht.
- **Mitten → Flux.** Vocals, Flächen und Bass-Obertöne liegen *dauerhaft* im Band, ein
  Pegeldetektor sieht dort immer „viel". Nur der Anstieg der Snare unterscheidet sie.
- **Höhen → wählbar.** Becken klingen aus (Flux), geschlossene Hi-Hats sind kurz (Energie).

Umgesetzt als `tuneDetBass/Mid/High`, per API (`?db= ?dm= ?dh=`) und in der GUI wählbar.
`?flux=` bleibt als Sammelschalter erhalten. Damit sind auch die Attack/Decay-Regler wieder
sinnvoll: sie gelten für jedes Band, das auf Energie steht.

### Die Sensitivity ist nicht mehr materialabhängig

Der berechtigte Einwand des Users war, dass ein Regler, den man pro Track nachzieht, keine
Lösung ist. Die Schwelle war `Mittelwert × Faktor` — und genau deshalb genrespezifisch: in
gleichförmiger Musik liegt der Mittelwert dicht an den Spitzen (nichts löst aus), in
dynamischer weit darunter (alles löst aus). Deshalb kam als bester Wert 30 für Techno und
70 für D&B heraus.

Jetzt: **Schwelle = Mittelwert + k × mittlere absolute Abweichung**, wobei die Sensitivity
k setzt. Das ist der Varianzterm aus Patins klassischem Energie-Beatdetektor, nur mit MAD
statt Varianz (kein Quadrieren, keine Skalierungsprobleme). Die Schwelle skaliert damit
selbsttätig mit der Dynamik des Materials.

### 16 kHz statt 8 kHz

Auf die Frage nach Luft über 4 kHz: die gab es nicht — 4 kHz war Nyquist. Jetzt 16 kHz mit
N=512. Die **Bin-Breite bleibt bei 31,25 Hz**, alle konfigurierten Bandgrenzen behalten also
ihre Bedeutung; nur der höchste Bin wandert von 127 auf 255 und der nutzbare Bereich auf
8 kHz. Das ist der Bereich, in dem Hi-Hats und Becken tatsächlich liegen — vorher war das
High-Band an seiner nützlichsten Stelle abgeschnitten. Kosten: ~900 µs statt 400 µs pro
Frame, weiterhin unter 3 % CPU. Nebenbei läuft das Mikrofon damit näher an seinem
spezifizierten Taktbereich.

Bass-Untergrenze zusätzlich von Bin 1 auf Bin 2 (62 Hz) gesetzt: Bin 1 ist Rumpelbereich,
und der DJM-500 filtert laut Service-Manual etwa 50–150 Hz.

### Was die DJM-500-Analyse bestätigt hat

Der User lieferte die Architektur des 1995er Mixers: Mono-Summierung, analoger Bandpass
~50–150 Hz, Hüllkurvendetektor mit **gleitendem Mittelwert als Schwelle**, Komparator →
Puls, dann im Mikrocontroller ein **Pulse-Window von 150–200 ms** gegen Doppeltrigger,
Periodenmessung und **Interpolation über ganzzahlige Vielfache** bei ausgelassenen Kicks.

Das deckt sich bis ins Detail mit dem, was hier entstanden ist — und bestätigt vor allem,
dass kein FFT und kein Flux nötig ist, um einen Kick zu finden. Unser Pulse-Window liegt bei
280 ms (`MIN_BEAT_INTERVAL_MS`), also deutlich träger als die 150–200 ms des DJM; das ist ein
Kandidat für den nächsten Test.

### GUI

- **Sync-Fehler behoben:** `setTune((prev) => prev || …)` seedete genau einmal. Alles, was
  über die API gesetzt wurde (oder aus einem zweiten Browserfenster kam), erreichte die
  Bedienelemente nie — das Panel zeigte Werte, die gar nicht liefen. Jetzt werden die
  Gerätewerte bei jedem Poll übernommen, außer für Regler, die man gerade angefasst hat
  (1,5 s Schonfrist, dieselbe Disziplin wie im Rest der App).
- **Weniger Scrollen:** die Bedienelemente liegen in vier Klappbereichen, und alle Werte mit
  festem Raster sind jetzt Dropdowns mit sprechenden Labels („Bass attack · fast (÷4)")
  statt Regler. Ein Dropdown braucht eine Zeile statt drei und sagt mehr aus.
- **Erklärter Signalweg:** der Bereich „Detection chain" beschreibt in zwei Sätzen, was
  Analysis, Detector, Tempo und Octave jeweils tun und warum Bass und Mitten
  unterschiedliche Detektoren brauchen — vorher waren es vier unkommentierte Schalter.

**Status: kompiliert und syntaxgeprüft, nicht auf Hardware getestet** — die Anlage war aus.
Firmware 92,8 % Flash, RAM 18,3 %.

### Nachtrag 2026-09-01: geflasht und am Gerät verifiziert

Firmware und Filesystem per USB aufgespielt. Messwerte gegen die Vorhersagen:

| | vorhergesagt | gemessen |
|---|---|---|
| FFT pro Frame (N=512) | ~900 µs | **892 µs** |
| Audio-Poll gesamt | — | 948 µs = **3,0 % CPU** |
| `loopMax` | nicht schlechter | **8 ms** (vorher 13–14) |
| Spektrum | 256 Bins bis 8 kHz | 256 Bins bis 7936 Hz |

Die Loop ist mit der doppelt so großen FFT sogar ruhiger geworden. `/api/spectrum` liefert
das Array vollständig (256 Werte geparst) — die Puffervergrößerung von 1100 auf 2600 Byte
war nötig, mit der alten Größe wäre das JSON stillschweigend abgeschnitten worden und hätte
bis zur Schnittstelle trotzdem gültig ausgesehen.

Detektor-Vorgaben stehen wie beabsichtigt (Bass=Energie, Mid=Flux, High=Flux). Die
**Bandgrenzen kamen aus NVS** und haben die neuen Compile-Vorgaben überschrieben — korrektes
Verhalten, aber mit der Folge, dass das High-Band weiterhin bei Bin 127 (≈4 kHz) endet und
den neu verfügbaren Bereich bis 8 kHz nicht nutzt. Wer ihn will, zieht die obere Grenze im
AUDIO-Tab hoch. Presets haben beide Flash-Vorgänge überstanden.

### 2026-09-01 (2) — Einstellungen live optimiert, und zwei echte Fehler beim Tap

Der User bat, die Einstellungen über die API zu optimieren, weil „BPM jetzt aus Mid und High
zu kommen scheint, weil Bass mit Energie einfach zuläuft".

**Die Beobachtung war exakt richtig, und es waren zwei Ursachen:**

1. **Der Eingang war voll übersteuert.** Input-Gain stand auf ×32, Mic-Peak konstant bei
   32768 — also 100 % Aussteuerung, alle Transienten plattgedrückt. Unter der Bedingung kann
   keine Einstellung funktionieren. Eingemessen: **×16** ist die höchste Stufe ohne Clipping.
2. **Hüllkurve und Schwelle liefen im Gleichschritt.** Bass-Decay ÷16, Schwellen-Nachführung
   ebenfalls ÷16 — die Schwelle zog also exakt so schnell nach, wie die Hüllkurve abfiel, und
   der Bass konnte sie prinzipiell nie kreuzen (gemessen: Bass 16346 gegen Schwelle 24770).
   Genau das ist das „läuft zu". **Regel daraus: die Hüllkurve muss deutlich schneller
   abfallen, als die Schwelle nachgeführt wird.** Eingemessen: Decay ÷2 gegen Schwelle ÷256,
   Sensitivity 100.

Ergebnis: Onset-Rate 2,28/s bei einem 147er-Track, Tracker-Modalwert **exakt 147** (vom User
bestätigt). Nach zusätzlich verschärfter Hysterese (vier Bestätigungen statt zwei, plus 30 %
Score-Vorsprung gegenüber dem gehaltenen Wert) liegt der Anteil im Zielbereich 144–150 bei
**73 %**, vorher rund 55 %. Die verbleibenden Ausreißer gehen nach 182–197.

### Tappen war wirkungslos — und das erklärt „sprunghaft"

Der User merkte an, Handtapping solle Vorrang haben und wirke sehr sprunghaft. Der Befund war
gravierender als die Formulierung vermuten ließ: `globalBPM = trackedBPM` lief in **jedem**
Audio-Frame, also ~31× pro Sekunde. Ein getappter Wert überlebte damit rund 32 ms — bei
eingeschaltetem Mikrofon war Tappen schlicht **ohne Wirkung**.

Jetzt setzt ein Tap `tempoTapLock`, und der Tracker überschreibt nicht mehr. Live verifiziert:
getappte 123 BPM hielten über 12 s, während der Tracker weiterhin 197 meldete; nach
`?tap=0` übernimmt wieder die Automatik. Die Audio-Erkennung liefert dabei weiterhin die
**Phase** über die Beat-Uhr-Regelung — Tempo von Hand, Phase vom Signal, wie an einem Pult.

Zusätzlich war die Tap-Auswertung selbst unnötig empfindlich: sie bildete den **Mittelwert**
der Intervalle, ein einzelner verrutschter Schlag zog ihn also sofort mit, und schon zwei Taps
lieferten ein Ergebnis. Jetzt Median statt Mittelwert, Ausreißer über 35 % Abweichung werden
verworfen, mindestens drei Taps, 3-s-Fenster, und die Obergrenze auf 200 angehoben (sie stand
noch auf 180, obwohl die Firmware längst 200 erlaubt).

In der GUI ist das Tempo-Dropdown jetzt dreistufig: *tapped (holds)* / *auto tracker* /
*interval median*, mit Hinweistext, wenn der Tap-Wert gehalten wird.

### 2026-09-01 (3) — BPM je Band gemessen: Mid ist das sauberste, nicht der Bass

Auf die Frage des Users, was die drei Bänder einzeln an Tempo hergeben und ob es ein
Vielfaches von 147 ist, direkt gemessen (Onset-Abstände je Band über 30 s):

| Band | Rate | Abstand | ergibt | Streuung | Bezug zu 147 |
|---|---|---|---|---|---|
| low | 2,40/s | 364 ms | 165 BPM | 84 % | ~1×, 12 % daneben |
| **mid** | 4,73/s | 206 ms | 291 BPM | **23 %** | **exakt 2×, 1 % daneben** |
| high | 4,33/s | 210 ms | 286 BPM | 36 % | 2×, 3 % daneben |

**Das Mid-Band ist mit Abstand das regelmäßigste** und liegt auf exakt dem doppelten Beat,
also auf Achteln. Der Bass ist trotz Optimierung das *unregelmäßigste* Band. Da der
Tempo-Tracker bisher **ausschließlich** aus Bass-Onsets gespeist wird, arbeitet er damit auf
der schlechtesten verfügbaren Quelle — das ist der wichtigste Befund dieser Messreihe.

### Die untere Grenzfrequenz war zu tief (Vorschlag des Users, bestätigt)

Der User schlug vor, die untere Bandgrenze auf 80–100 Hz anzuheben. Direkt gegeneinander
gemessen, dieselbe Passage:

| Bass-Band | Rate | Abstand | Streuung | Rasterfehler |
|---|---|---|---|---|
| 62–187 Hz | 0,93/s | 683 ms | 159 % | 33 % |
| 93–250 Hz | 1,60/s | 418 ms | 54 % | 2 % |
| **125–281 Hz** | **1,80/s** | **407 ms** | **34 %** | **0 %** |

Der Abstand trifft mit 407 ms die 408 ms des 147er-Beats exakt. Bemerkenswert: das Optimum
liegt **deutlich höher** als die 50–150 Hz des DJM-500 — offenbar sitzt der Anschlag des
Kicks höher als sein Grundton, und im Grundtonbereich drängelt die Basslinie, die den
gleitenden Mittelwert oben hält und die Schwelle mitzieht.

### Methodischer Vorbehalt

Mehrere Folgemessungen waren **nicht vergleichbar**, weil die Musik zwischen den Läufen
wechselte: dieselbe Einstellung lieferte einmal 2,40 Onsets/s bei 34 % Streuung und wenige
Minuten später 0,98/s bei 155 %. Ein Vergleich Bass-Energie gegen Bass-Flux fiel dadurch
ebenfalls aus. Für belastbares Feintuning braucht es eine über mehrere Minuten konstante
Passage; alles andere misst die Musik, nicht die Einstellung.

**Nächster Schritt, aus der Messung abgeleitet statt geraten:** den Tempo-Tracker aus dem
**Mid-Band** speisen (oder aus dem jeweils regelmäßigsten), statt aus dem Bass. Die
Oktav-Faltung im Tracker kann den Faktor 2 auflösen — die Mid-Onsets liegen ja sauber auf
dem halben Beat.

### 2026-09-01 (4) — Onset-Erkennung auf Sample-Ebene: die eigentliche Ursache

Der User: „egal wie, wir sind immer noch voll daneben. der djm hat das einfach besser gemacht."
Zu Recht — und beim erneuten Durchgehen der DJM-Architektur zeigte sich ein **struktureller**
Unterschied, der fünf Anläufe am Tempo-Schätzer erklärt:

**Der DJM erkennt in kontinuierlicher Zeit, wir erkannten in 32-ms-Blöcken.** Sein Komparator
schaltet in dem Moment, in dem die Hüllkurve die Referenz kreuzt. Unsere Erkennung lief auf
FFT-Frames und konnte Onsets deshalb nur alle 32 ms zeitstempeln — bei einem 462-ms-Beat sind
das ±7 % Jitter, bevor irgendein Schätzer anfängt. Ein Kick-Anschlag dauert 5–20 ms, passt
also komplett in einen Frame und wird vom Frame-Mittel verschmiert. **Alle fünf Schätzer
bekamen dieselben verrauschten Zeitstempel** — das war nie mit einem besseren Schätzer zu
beheben.

Jetzt nachgebaut, wörtlich als DJM-Kette, aber pro Sample bei 16 kHz: Bandpass als Differenz
zweier Ein-Pol-Tiefpässe, Hüllkurvengleichrichter mit schnellem Attack und langsamem Release,
Komparator gegen eine langsam nachgeführte Referenz, Wiederscharfschaltung erst nach Rückkehr
zur Referenz, plus Pulse-Window. Zeitstempel kommen aus einem **Sample-Zähler** statt aus
`millis()` — vom Audiotakt abgeleitet und damit frei vom Jitter der Abfrageschleife.

**Ergebnis, gemessen an einem 130-BPM-Track:**

| | Frame-basiert | Sample-basiert |
|---|---|---|
| Onset-Streuung | 82–155 % | **14 %** |
| Onset-Rate | 0,98–2,40/s schwankend | **2,19/s** (Ziel 2,17) |
| globalBPM-Schwankung | 52 BPM | **7 BPM** |

Der Tempo-Wert ist damit zum ersten Mal stabil. Die Pulse-Window-Länge wurde eingemessen:
200 ms fängt Achtel (Streuung 17 %, Rasterfehler 52 %), **410 ms** trifft die Viertel
(Streuung 7 %, Rasterfehler 2 %).

**Kostenoptimierung dabei:** die erste Fassung brauchte 6,8 % CPU, weil zwei **64-Bit-Divisionen
pro Sample** darin standen (Zeitstempel und Schwellenberechnung) — auf RV32 softwareemuliert.
Schwelle als Q8-Multiplikation vorberechnet, Zeitstempel nur noch berechnet wenn tatsächlich
etwas auslöst: **4,3 %**.

### Fehler im eigenen Entwurf: die Tap-Sperre war persistiert

`globalBPM` stand nach dem Flashen zu 100 % auf 120, obwohl der Tracker korrekt 144 lieferte.
Ursache: ich hatte `tempoTapLock` in NVS gespeichert. Das Gerät bootete also mit gesperrtem
Tempo — aber der getappte Wert, dessentwegen die Sperre existiert, überlebt den Neustart
nicht. `globalBPM` blieb damit für immer auf seinem Startwert 120, während der funktionierende
Tracker nie durchkam. Eine Sperre zu persistieren, deren Auslöser es nicht tut, ist schlicht
falsch — sie startet jetzt immer offen.

### Verbleibend

Der Wert sitzt stabil, aber systematisch bei 138 statt 130 (6 % zu hoch). Der Phasentest
findet die **Achtel**-Periode (218 ms) als stärksten Peak und verdoppelt sie auf 436 ms; ein
kleiner Fehler auf der halben Periode verdoppelt sich dabei mit. Der Fix wäre, nach dem Falten
am gefalteten Wert nachzujustieren statt einfach zu verdoppeln — nicht mehr blind gebaut,
sondern als nächster Schritt notiert.

### 2026-09-01 (5) — Richtigstellung: der Sample-Detektor war ein freilaufender Oszillator

Der Eintrag 2026-09-01 (4) meldete einen Durchbruch bei der Onset-Erkennung: Streuung von
82–155 % auf 14 %, Rate 2,19/s bei einem Ziel von 2,17. **Diese Zahlen waren ein Artefakt.**
Der User bestätigte das Tempo als 133 BPM (nicht 130), und ein Sweep über die Pulse-Window-Länge
zeigte dann das hier:

| Sperre | 200 | 240 | 280 | 320 | 360 | 410 |
|---|---|---|---|---|---|---|
| Onset-Median | 217 | 267 | 307 | 350 | 399 | 454 |

**Der Median ist bei jeder Einstellung die Sperre plus rund 40 ms.** Der Detektor erkannte
nichts — er feuerte in dem Moment wieder, in dem die Sperre aufging. Dass 410 ms so gut
passte, lag ausschließlich daran, dass ich die Sperre zuvor gegen das Beat-Intervall
eingemessen hatte: 410 + 40 = 450 ≈ 451 ms. Die geringe Streuung war die Gleichmäßigkeit
der Sperre, nicht die der Musik. Ein Detektor, dessen Rate man über die Sperre einstellt,
misst die Sperre.

**Ursache:** die Referenz, gegen die der Komparator vergleicht, hatte mit `sdRefShift = 11`
eine Zeitkonstante von 2¹¹ Samples = **128 ms**. Bei einem Beat von 451 ms folgt sie dem Kick
mit, den sie als Ausreißer erkennen soll, und löscht genau den Kontrast aus. Der DJM mittelt
über Sekunden. Erst mit `brf = 13` (512 ms) fiel die Rate von 4,9 auf 2,4/s, **ohne** dass die
Sperre band (die stand auf 150 ms) — dort kam die Rate zum ersten Mal aus der Erkennung.
Auch die Empfindlichkeit war wirkungslos: bei `sens = 100` ist der Schwellenfaktor exakt 1,0,
die Schwelle also gleich der Referenz.

### Zwei Fehler, die nicht von der Musik abhängen — behoben und verifiziert

**Der Pegelmesser maß nach der Begrenzung.** Ein einziges gesättigtes Sample im Frame nagelte
die Anzeige auf Vollausschlag, und sie konnte sich danach nicht mehr bewegen, egal wie weit
die Verstärkung heruntergedreht wurde. Sie zeigte weder den echten Pegel noch Headroom; ein
hart geclipptes Sample wurde zudem doppelt als Clipping gezählt. Jetzt vor der Begrenzung
gemessen — der Wert darf über Vollausschlag hinauslaufen, denn genau dieser Überschuss ist
die nützliche Information. Verifiziert: die Anzeige verdoppelt sich exakt pro Stufe
(3484, 7352, 14048, 27264, 54928, 95904).

**Damit wurde sichtbar: der Eingang stand auf `ig = 5`, also 293 % Vollaussteuerung mit 269
geclippten Samples pro Frame.** Sämtliche Spektren und Bandpegel dieser Session wurden von
einem hart übersteuerten Signal abgelesen. `ig = 3` liefert 83 % ohne jedes Clipping und ist
jetzt Standard.

**Das Fenster des Phasentests ist jetzt einstellbar** (`tw`, Standard 24 s statt fest 10 s).
Die Periodenauflösung eines Phasentests ist etwa P²/D — bei 450 ms Beat löst ein 10-s-Fenster
nur ~20 ms auf, 428 und 452 ms fallen also in dieselbe Zelle. Auf dem Gerät bestätigt: bei
5 s streut `tLag` über 412–436, bei 28 s steht ein einziger Wert.

### Methodik: das eigentliche Hindernis

Zum zweiten Mal in dieser Session sind Messreihen daran gescheitert, dass gegen laufende Musik
gemessen wurde. Dieselben Einstellungen lieferten im Abstand von Minuten 2,42 Onsets/s und
0,96/s bei 320 % Streuung; `brf = 14` einmal 0,04/s und einmal exakt null. Jeder Sweep dauert
Minuten, der Track läuft weiter, und damit ist keine einzige dieser Zahlen belastbar — auch
die guten nicht. **Vor der nächsten Detektor-Einmessung muss eine feste Referenzaufnahme
stehen** (Loop oder Metronom bei bekanntem Tempo), sonst wiederholt sich das ein drittes Mal.
Das stand bereits als Vorsatz in Commit 7328427 und wurde übersprungen.

### 2026-09-01 (6) — Der Komparator hatte nie eine Referenz

Der User, zu Recht ungehalten: „du fummelst da einfach dauernd an was rum. und es wird nichr
besser … was kann so schwer daran sein einfach unten im bass iwas mit komparator zu bauen …
schwellwert rollend mit durchstechendem energy muss ein bass/kick sein … bpm rolling mittelung
über 2-3s … range 60-200bpm."

Genau das ist jetzt gebaut. Beim Durchrechnen seiner Beschreibung fielen **drei Fehler** auf,
die erklären, warum keine der Einmessungen der letzten Tage etwas bedeuten konnte.

**1. Die rollende Schwelle existierte nicht.** `sdRef += (sdEnv - sdRef) >> sdRefShift` ist eine
Integer-Schiebung: ist die Differenz kleiner als `2^sdRefShift`, ergibt sie exakt 0 und die
Referenz steht still. Bei Schiebung 14 ist die Totzone 16384 — die Referenz blieb bei null
kleben, die Schwelle wurde damit konstant 8, und die Wiederscharfschaltung `sdEnv < sdRef`
verlangte, dass ein Betrag unter null fällt: **nach dem allerersten Onset konnte der Detektor
nie wieder scharf werden.** Das war die gemessene Rate von 0,00/s bei `brf=14`. Bei schneller
Referenz blieb umgekehrt nur die Sperre als ratenbestimmende Größe übrig — daher der Median
„Sperre plus 40 ms" bei jeder Einstellung. Referenz wird jetzt mit 8 Bit Zusatzgenauigkeit
geführt.

**2. Es gab gar keine Hüllkurve.** Der Release stand auf `sdRel = 7`, kommentiert als „~50ms".
2⁷ Samples bei 16 kHz sind **8 ms**. Bei 8 ms folgt der Wert der gleichgerichteten
Bandpassschwingung selbst, und die schwingt mit 40–160 Hz über jede Schwelle. Jetzt ~128 ms.

**3. Das Band war zu breit.** 80–320 Hz ließ Basslinie und untere Mitten durch, von denen jede
Überschreitung ein Fehl-Beat wurde. Jetzt 40–160 Hz, wie beim DJM.

Dazu ein Fehler im neu gebauten Schätzer selbst: das Intervall wurde mit dem präzisen
Sample-Uhr-Zeitstempel gefüllt, das Zeitfenster aber gegen `millis()` geprüft. Zwei Uhren — die
Sample-Uhr läuft nach, also galt jedes Intervall sofort als abgelaufen und es kamen nie drei
zusammen. Getrennt: Abstand auf der präzisen Uhr, Verfall auf der Wanduhr.

**Fünf Schätzer sind rausgeflogen** (geglättete Median-Historie, Autokorrelation mit
Harmonischen-Summierung, Intervall-Histogramm, Onset-Phasen-DFT, Hysterese). Zwei davon
schrieben `globalBPM` unabhängig aus derselben Funktion, es gewann der zuletzt laufende. Jetzt
genau ein Schreiber und genau ein Verfahren: Abstände messen, alles außerhalb 60–200 BPM
verwerfen, Median über ein Zeitfenster. Das Verwerfen am Eingang ist es, was jede Oktav- und
Faltungslogik dahinter überflüssig macht.

**Messung mit allem drin:** 1,92 Onsets/s, Median 456 ms = 131,5 BPM gegen wahre 133 — und
**kein einziger Abstand unter 200 ms** bei einer Sperre von 120 ms, die Sperre bestimmt die Rate
also nachweislich nicht mehr.

### Noch offen, und warum

Der Schwellenfaktor ist nicht fertig eingemessen. Er ist der eine verbliebene Regler, und beim
Versuch, ihn einzustellen, lieferte dieselbe Einstellung im Abstand von Minuten 427 ms und
747 ms — der Track war in einen anderen Abschnitt gelaufen. Das ist zum dritten Mal in dieser
Session dieselbe Sackgasse. **Ohne feste Referenzaufnahme (Loop oder Metronom, mehrere Minuten
konstant) ist dieser letzte Schritt nicht durchführbar**, und weitere Sweeps gegen laufende
Musik erzeugen nur wieder Zahlen, die beim nächsten Track nicht mehr gelten.

Nebenbei aufgefallen: `sens` liegt auf `/hwaudio`, nicht auf `/audio_tune`. Frühere
Empfindlichkeits-Sweeps dieser Session waren wirkungslos und ihre Schlussfolgerung
(„die Schwelle wirkt nicht") entsprechend falsch.

### 2026-09-01 (7) — Fremde Implementierungen gelesen, drei Dinge übernommen, eines verworfen

Der User lieferte drei Quellen: `Steppschuh/Micro-Beat-Detection`, `gibbedy/BeatDetector`,
`stengerh/foo_bpm` sowie das Stichwort Collins, „A Comparison of Sound Onset Detection
Algorithms with Emphasis on Psychoacoustically Motivated Detection Functions" (AES 118, 2005,
hinter Paywall — stattdessen die frei zugängliche Nachfolgearbeit Dixon, „Onset Detection
Revisited", DAFx-06, ausgewertet).

**Übernommen:**

*Onset am Gipfel statt an der Flanke* (Dixon, Bedingung 1 seines Peak-Pickers). Unser Detektor
feuerte beim Schwellendurchgang. Ein Durchgang wandert mit dem Pegel — lautere Passage, früherer
Durchgang — ein Gipfel nicht. Peak-zu-Peak gemessene Abstände sind damit deutlich
wiederholgenauer, und Wiederholgenauigkeit ist hier das ganze Problem. Kausal umgesetzt: ab
Schwellenüberschreitung dem Maximum folgen, festschreiben wenn die Hüllkurve zurückfällt, und
mit der Zeit **des Gipfels** stempeln — der gemeldete Zeitpunkt verspätet sich also nicht, nur
die Kenntnis davon.

*Schwelle im Dynamikbereich* (gibbedy): `Untergrenze + Anteil × (Spitze − Untergrenze)` statt
`Mittelwert × Faktor`. Der Regler wird dimensionslos — eine Position zwischen leise und laut,
keine Verstärkung — und muss deshalb bei Pegelwechsel nicht nachgestellt werden. Genau daran sind
sämtliche bisherigen Einmessversuche gescheitert. Dazu gibbedys Zusatzbedingung „> 2 ×
Mittelwert", damit in beatlosen Passagen nicht das Eigenrauschen zerlegt wird.

*Median statt Mittelwert als Bezugsgröße* (foo_bpm). Ein Mittelwert wird von genau den Peaks nach
oben gezogen, gegen die er messen soll; ein Median nicht. Das mit einem immer langsameren Mittel
zu umgehen war der Weg, der in den eingefrorenen Referenzwert geführt hat (Eintrag 6).

*Konsistenzprüfung vor der BPM-Ausgabe* (gibbedy). Er verlangt, dass alle jüngsten Intervalle
zum neuesten passen, sonst hält er den alten Wert. Hier als Streuung um den Median formuliert,
damit ein einzelner Ausreißer eine sonst saubere Messung nicht blockiert. Ein Median liefert
sonst immer eine Zahl, auch aus bedeutungslosen Abständen.

*Varianz-Gate* (Steppschuh): ein Beat verlangt eine bewegte Hüllkurve, nicht nur eine hohe. Eine
gehaltene Bassnote überschreitet die Schwelle dauerhaft — das waren die Zusatz-Onsets zwischen
den Kicks. Und dessen *weiche* Sperre statt einer harten: eine abklingende Schwelle kann kein
eigenes Raster bilden, eine harte Sperre nachweislich schon (Eintrag 5).

**Verworfen, gegen die eigene erste Einschätzung:** Klapuris logarithmische bzw. relative
Differenzfunktion. Sie klingt überzeugend und wurde hier zunächst als „für uns fundamental"
bezeichnet — Dixon hat genau das gemessen: *„Empirical tests favoured the use of the L1-norm
here over the L2-norm, and the linear magnitude over the logarithmic (relative or normalised)
function proposed by Klapuri."* Ebenso verworfen: foo_bpms quadratische, binhalb gewichtete
Flux-Variante. In Dixons großem Datensatz (106.054 Onsets) erreicht schlichter Spectral Flux das
beste F-Maß (0,964) **und** den kleinsten Zeitfehler (8,8 ms) von acht Verfahren. Unsere
bestehende lineare Summe positiver Differenzen ist damit bereits die empirisch bevorzugte Wahl.

**Ebenfalls nicht übernommen:** foo_bpms Gittersuche über Tempo × Phase (Offline-Analyse eines
ganzen Tracks, für uns pro Schleifendurchlauf nicht bezahlbar), Steppschuhs FHT-Frontend (unserer
16-kHz-Festkomma-FFT unterlegen) und dessen handgefittete Magnitudenkurve
(`-1.05`, `*10`, `pow(x,3)/3-1.25`) — Konstanten ohne Prinzip, die zwischen Aufbauten nicht
übertragen. In beiden Fremdprojekten stecken zudem Fehler, die man beim Abschreiben mitnähme:
Steppschuh zieht in `calculateMagnitudeChangeFactor` 0,1 vom bereits begrenzten Overall-Faktor
ab, mitten im Block für den First-Faktor; gibbedy berechnet in Zeile 30 eine Glättung des
Maximums und überschreibt sie in Zeile 31 sofort.

**Stand:** kompiliert, Flash 93,4 %. **Auf Hardware unverifiziert** — Gerät ab 2026-09-02 wieder
verfügbar. Prüfplan siehe `handoff.md`.

## 2026-09-02

### Der Detektor wird ohne Hardware testbar

Der User: „kannst du den beat detektor nicht iwie abstrahieren und mit fake musik testen ohne den
mikrocontroller? … die verarbeitungsgeschwindigkeit oder limitation des controllers kannst du
doch auch easy abbilden." Die produktivste Idee der Session — und eine, die früher hätte kommen
müssen.

Nichts wurde abstrahiert: `sim/` übersetzt das **echte** `Audio_Engine.h` unverändert nativ,
gegen ein nachgebautes `Arduino.h` und einen nachgebauten I2S-Treiber, dessen Ring mit der echten
Abtastrate gegen eine simulierte Uhr gefüllt wird. Modelliert sind DMA-Ringtiefe, Blockstruktur
und der Jitter der Hauptschleife (~1 ms, gelegentlich 15 ms). Der Kunsttrack enthält absichtlich
das, was auf echter Musik gestört hat: eine gehaltene Basslinie zwischen den Kicks, Hats und eine
Snare in den unteren Mitten.

**Drei echte Fehler in den ersten Läufen:**

1. *Das Dynamik-Tor war unerfüllbar.* `sdMinLevel = floor × 2` — übernommen aus
   `gibbedy/BeatDetector` — verlangte 20382, während die Hüllkurve im ganzen Fenster nur 16977
   erreichte. Gibbedys Mittelwert ist eine FFT-Bin-Magnitude, die zwischen Beats fast auf null
   fällt; unsere Bezugsgröße ist der Median einer Hüllkurve mit langem Abfall, die das nie tut.
   Die Regel wurde übernommen, ohne zu prüfen, ob die zugrundeliegende Größe vergleichbar ist —
   exakt der Fehler, vor dem im Eintrag davor bei den Magic Numbers gewarnt wurde.
2. *Die weiche Sperre klang nie ab.* `sdBoost -= (sdBoost - 256) >> 11` schiebt in Q8 einen
   Wertebereich von 768 um 11 Bit — das Ergebnis ist immer 0. Die Schwelle blieb für immer auf dem
   Vierfachen, **der Detektor war nach dem ersten Onset dauerhaft taub.** Dieselbe
   Integer-Totzone, die Stunden zuvor die Komparator-Referenz eingefroren hatte, in anderer Form
   wieder eingebaut. Jetzt in Q16 geführt und erst bei der Verwendung auf Q8 gekürzt.
3. *Die Abfallzeit war für schnelle Musik falsch.* Über 90–174 BPM gemessen: 16 ms Release hat
   den schlechtesten Fall F = 0,939, die 128-ms-Variante 0,656 — letztere verpasste bei 174 BPM
   jeden zweiten Beat und meldete die halbe Geschwindigkeit. Das revidiert eine Änderung vom
   Vortag, deren Begründung damals **nicht falsch war**: mit eingefrorener Referenz feuerte ein
   kurzer Release tatsächlich durchgehend. Mit Median-Bezug und Dynamikbereich-Schwelle gilt das
   nicht mehr. Wieder eine Schlussfolgerung, die nur wegen eines Bugs richtig aussah.

Stand danach: F = 0,988 bei 130 BPM, 100 % Treffergenauigkeit, 6,9 ms Zeitfehler, Tempo über
90–174 BPM auf 2 BPM genau, null verworfene Samples, null Uhrenabweichung. Damit ist auch der
Abhol-Fix vom Vortag **belegt** statt nur plausibel. Der Auto-Gain hält F zwischen 0,965 und 0,988
über einen 50-fachen Pegelbereich, während feste Verstärkung schon beim 2,5-fachen auf 0,233
einbricht.

### Blindtest an einem echten Track — und ein Fehler, den nur die Gegenprobe fand

Der User lieferte `trance-drop-kick-bass_xyzbpm.wav` (Tempo im Dateinamen verschleiert) mit der
Aufgabe, die BPM aus Bass, Mid und High zu bestimmen. Erster Durchlauf: überzeugende 129,9 BPM
aus allen drei Bändern.

**Falsch.** Der Ladeblock für die Datei stand im Quelltext hinter der Modus-Verzweigung, die
Datei wurde nie geladen, und gemessen wurde der Synthesizer mit seinem Vorgabewert 130. Aufgefallen
ist es nur, weil parallel eine unabhängige Autokorrelation in numpy auf der Originaldatei
widersprach.

Die Lehre gehört ins Protokoll: **Einigkeit zwischen den drei Bändern hatte null Beweiswert**,
weil alle drei denselben Schätzer und denselben Intervallfilter benutzen. Drei übereinstimmende
Antworten aus einer Quelle sind eine Antwort. Der Simulator bricht jetzt ab, wenn eine Datei
genannt, aber nicht geladen wurde.

Nach der Korrektur: alle drei Bänder 414 ms = **144,9 BPM**. Unabhängig bestätigt durch
Autokorrelation der Originaldatei: Spitze bei 415 ms (144,6 BPM, r = 0,932), Rohanschläge bei
207 ms — also Achtel. Saubere Hierarchie 207 / 414 / 827 ms. **Der Track hat 145 BPM.**

### Mid und High bekommen eigene Detektoren

Frage des Users: ob Dimmer-FX auf die Highs und Movement auf den Kick gehen. Die Verdrahtung gab
es längst (jeder FX kann von `bass`/`mid`/`high` getriggert werden) — aber **nur der Bass lief auf
der neuen Kette.** Mid und High entschieden weiter über den Frame-Vergleich gegen ein geglättetes
Mittel, also genau den Pfad, der diese Session über als untauglich nachgewiesen wurde. Deshalb saß
ein Dimmer auf „high" nie sauber auf den Hats.

Der Detektorzustand ist jetzt eine Struktur mit drei Instanzen über denselben Samples; Verstärkung
und Begrenzung werden pro Sample einmal gerechnet und geteilt. Die bestehenden globalen Namen
bleiben als **Referenzen** auf die Bass-Instanz, damit API, Debug-JSON und Simulator unverändert
weiterlaufen.

Entscheidend war die **bandspezifische Erholzeit**. Im ersten Versuch feuerten alle drei mit
derselben Rate — sie hatten die Kick-Sperre geerbt (4× Schwelle, 128 ms), die für Hats viel zu
träge ist. Mit rollengerechten Werten (Bass 4,0×/128 ms, Mid 3,0×/64 ms, High 2,0×/32 ms) am
145-BPM-Track gemessen:

| Band | Rate | Abstand | |
|---|---|---|---|
| Bass | 2,35/s | 414 ms | Viertel — Movement |
| Mid  | 4,75/s | 207 ms | Achtel |
| High | 4,33/s | 207 ms | Achtel — Dimmer |

`globalBPM` bleibt bei 144, weil der Tempo-Schätzer allein am Bass hängt. Die Phasenanalyse (jetzt
feste Ausgabe des Simulators) zeigt: **nur 1–2 % der Mid/High-Onsets fallen auf die Kick-Position**,
der Rest sitzt offbeat dazwischen. Das ist allerdings eine Eigenschaft des Materials, nicht des
Detektors — bei Hats auf der Zählzeit fielen die Bänder zusammen.

### Aufräumen: was nichts mehr liest, wird nicht mehr gerechnet

- **Die FFT ist keine Erkennung mehr.** Alle drei Bänder kommen aus der Sample-Raten-Kette, die
  auf Rohsamples arbeitet. Übrig als Verbraucher: die AUDIO-Tab-Anzeige und der Rückfallpfad. Sie
  läuft jetzt nur noch, solange `/api/audio_debug` abgefragt wird (2-Sekunden-Lizenz). Gemessen war
  sie mit 1127–1207 µs von 1212–2438 µs pro Frame der größte Einzelposten.
- **Mid und High laufen nur bei tatsächlichem Routing.** Der `.ino` prüft zweimal pro Sekunde die
  sieben FX-Objekte — er ist die einzige Stelle, die es kann, weil die FX-Globals nach dem
  Einbinden von `Audio_Engine.h` deklariert werden.
- **Der tote Phasen-DFT-Apparat ist entfernt** (`tempoTrackerEval`, `tempoPlainAvg`,
  `tempoHarmAvg`, `onsetRing`, `onsetW`, `pushOnset`, `tempoSinTab`). Rund 1,1 KB Arrays plus ein
  Schreibvorgang pro Onset in einen Ring, den seit der Ablösung niemand mehr auslas.

Netto fährt der Normalfall — live spielen, Browser zu, Movement auf Kick, Dimmer auf Hats — jetzt
**zwei Detektoren und keine FFT**, wo vorher ein Detektor und die FFT bedingungslos liefen. Im
Simulator verifiziert, der genau diese Ruhekonfiguration ausführt: Bandergebnisse und der
90–174-BPM-Durchlauf sind identisch.

Beinahe wäre das schiefgegangen: die Übersetzung des Simulators schlug nach dem Löschen der
Ringpuffer fehl, und das **alte Binary** lieferte weiter Ergebnisse, die fast als Bestätigung
durchgegangen wären. Erst nach `rm simbeat` und Neubau waren die Zahlen echt.

### Weitere Korrekturen

- **Der Pegelmesser unterscheidet jetzt zwei Fehler.** Auf den Einwand des Users („wenn es clippt,
  sehen wir einfach 32768, also wissen wir nicht, wie weit wir runter müssen?!"): für *unsere*
  Übersteuerung stimmt der Einwand nicht — `s = (raw >> 16) << gain` ist eine int32-Rechnung, die
  nicht sättigt, belegt durch einen gemessenen Spitzenwert von 95904 (293 %). Für Sättigung **am
  Mikrofon** stimmt er vollständig: dort kommt `raw_samples` bereits oben abgeschnitten an, der
  Überschuss ist unbekannt, und Herunterregeln heilt nichts. Wird jetzt getrennt gezählt (`rclip`),
  als `MIC SAT` angezeigt, und die automatische Bereichswahl hält in dem Fall still.
- **Automatische Bereichswahl** für den Eingangspegel, bewusst asymmetrisch: runter nach ~1 s und
  gleich mehrere Stufen (weil der Überschuss messbar ist), hoch erst nach ~20 s und einstufig (weil
  ein Breakdown normale Musik ist). Stabil, weil das Fenster 25–92 % (Faktor 3,7) breiter ist als
  eine Stufe (Faktor 2). Bei einem Stufenwechsel wird der gesamte Filterzustand mitskaliert, sodass
  im Moment des Wechsels nichts passiert. Der gewählte Wert wird **nicht** persistiert — er stellt
  sich in Sekunden wieder ein, und NVS-Schreibzugriffe bei jeder Pegeländerung wären Verschleiß.
- **`/api/spectrum` ist in `/api/audio_debug?spec=1` aufgegangen.** Der Webserver arbeitet
  Anfragen einzeln aus der Hauptschleife ab, bei diesen Nutzlasten dominiert also der Fixkostenanteil
  pro Anfrage. Zwei Endpunkte mit 15 und 10 Hz waren die Ursache der Sekundenausschläge in der
  Ping-Anzeige. Ein Endpunkt trägt jetzt beides, der Tab läuft mit **25 Hz bei weniger Anfragen**.
- **`scripts/check_ui.sh`** transpiliert alle Babel-Blöcke mit dem mitgelieferten Babel. Es gibt
  keinen Build-Schritt für die UI, also prüft sonst nichts — die Fehlerklasse hat schon einmal den
  AUDIO-Tab geleert. Fing beim ersten Einsatz sofort eine doppelte `micOn`-Deklaration ab, die den
  Header weiß gemacht hätte.
- **Clipping- und Pegelanzeige im Header**, auf allen Tabs, gespeist aus der ohnehin laufenden
  Telemetrie — also ohne einen einzigen zusätzlichen Request.

### 2026-09-02 (2) — Der Test am Fixture

Erster geführter Durchlauf am echten Gerät nach dem Umbau. Aufteilung: der User macht das
Physische und beurteilt das Sichtbare, die Messung läuft über die API.

**Was der Prüfplan bestätigt hat:**

- **Uhrenabweichung 0 ppt** über 100 s. Der Abhol-Fix vom Vortag hält auf Hardware — das Gerät
  verliert kein Audio mehr, und die Sample-Uhr taugt als Zeitbasis. Das war das Abbruchkriterium.
- **Kadenz-Umbau real gemessen**, per Laufzeitschalter im A/B: `engUs` 283 µs (alt) gegen 137 µs
  (neu) bei 320 Schleifendurchläufen/s, also **9,0 % → 4,0 % der Wanduhr**. Meine Vorhersage lautete
  10,6 Punkte, real sind es 5 — Faktor 2 zu optimistisch, weil ich die Schleifenrate mit 421 statt
  320 angesetzt und die gesamten 274 µs der Assemblierung zugerechnet hatte. Tatsächlich sind nur
  ~158 µs Assemblierung, der Rest ist Integration, die bewusst jede Schleife läuft.
- **Bedarfsgesteuerte FFT**: mit geschlossenem AUDIO-Tab `fftUs = 0`, Schleife bei 322 Hz; mit
  offenem Tab 2458 µs und 186 Hz. Die Einsparung ist damit belegt, nicht nur plausibel.
- **Erkennung statt Oszillation**: Onset-Median 385 ms bei einer Sperre von 60 ms.
- **Bandtrennung** 2,3× und 2,79× auf Material mit Hi-Hats.

**Der Prüfstein Block E — zwei von drei Tracks, und ein sauber diagnostizierter Strukturfehler.**
Ohne eine einzige Einstellung anzufassen: Techno 156 ✓, House 119 ✓, Hip-Hop 98 ✗ (gemeldet 128).
Der Fehler ist erklärt und nicht behebbar durch Einstellung: bei 98 BPM ist der Beat 612 ms, der
Detektor rastet auf 454 ms ein — **¾ Beat auf 1 % genau**. Der Median aufeinanderfolgender
Kick-Abstände *kann* auf synkopiertem Material nicht den Beat finden, weil die Abstände dort
selbst ¾, ½ und 1¼ Beats sind. Details und der nötige Ansatz stehen im Backlog.

**Was der Test nebenbei aufgedeckt hat — teils gravierender als das Geplante:**

- **Der Joystick hatte keinen Totmannschalter.** Ein verlorenes Loslass-Paket ließ den Kopf bis an
  den mechanischen Anschlag fahren: gemessen fuhr `/joy_in?x=0.2` ohne Stopp die Pan-Achse in unter
  drei Sekunden von der Mitte auf null, und sie stand nur, weil der Weg zu Ende war. Behoben
  beidseitig — Lebenszeichen alle 150 ms aus der Oberfläche, Selbstabschaltung nach 500 ms im Gerät.
- **Drei unabhängige Bremsen wirkten auf dieselbe Bewegung**, ohne voneinander zu wissen: unsere
  Momentum-Glättung, die fixture-eigene auf CH5 (ab Werk auf 128, also halb angezogen) und eine
  Sollgeschwindigkeit ohne jeden Bezug zur Mechanik. Die Frage des Users „ist an CH5 nicht unser
  max speed gekoppelt?" hat die zweite überhaupt erst aufgedeckt.
- **Pan/Tilt-Geschwindigkeit gemessen**: 330°/s Pan, 165°/s Tilt, beide 40000 Einheiten/s. Das
  Verfahren dorthin war lehrreich — der erste Ansatz leitete die Grenze daraus ab, wo das
  Nachlaufen verschwindet, und lag damit um **Faktor 2 zu niedrig**: kein Nachlauf beweist nur,
  dass die Mechanik *mindestens* so schnell ist. Der User hörte sofort, dass der Kopf langsamer
  geworden war. Das taugliche Messgerät war sein Ohr am Motor.
- **Der SYNC-Knopf im PROGRAMMER-Tab war nie verdrahtet** — gleiche Optik wie der funktionierende
  im LIVE-Tab, aber ohne `onClick`.
- **TAP und SYNC quittierten den Druck nicht.** Bei TAP besonders schlecht, weil man beim Tappen
  aufs Licht schaut und nicht auf den Bildschirm.
- **Der Auto-Gain konnte sich bei 0 verklemmen.** Die Pegeluntergrenze, die das Hochdrehen in
  Stille verhindert, verglich den Pegel *nach* der Verstärkung. Einmal von einer lauten Stelle
  heruntergeregelt, sah normale Musik dann wie Stille aus — weil die Verstärkung niedrig war, also
  genau das, was korrigiert werden sollte. Der Detektor war auf 0,7 Onsets/s ausgehungert und das
  Tempo wurde Rauschen. Die Grenze wird jetzt bei voller Verstärkung beurteilt.

**Methodischer Rückfall, dreimal am selben Tag.** Schwellenfaktor, dann `agr=30`, dann fast wieder:
jedes Mal Werte an *einem* laufenden Track eingestellt und als allgemeingültig behandelt, jedes Mal
beim nächsten Track als trackspezifisch entlarvt. Der Simulator existiert genau dafür und wurde
dabei nicht benutzt. Die Regel für künftige Sessions: **Parametersuche gehört in `sim/`, am Gerät
wird verifiziert, nicht gesucht.**

### 2026-09-02 (3) — Tempo-Anzeige: Tap als Anker, Band statt Sprünge, und ein Reset

Nach dem Prüfplan kam die Praxis, und die förderte drei Dinge zutage, von denen keines eine
Frage der Erkennung war — die läuft laut User „bei House erstmal gut". Es ging durchweg um das,
was mit dem Messwert danach passiert.

**Der Tap sperrte den Tracker dauerhaft aus.** Ein einziger Tap setzte `tempoTapLock`, und der
einzige Weg zurück war `/audio_tune?tap=0` — nur im AUDIO-Tab erreichbar. Tappen macht man
mitten im Set, auf das Licht schauend; ein Tap kostete also die automatische Verfolgung für den
Rest des Abends, aus einem Tab heraus, der keinen Rückweg anbietet. **Das war meiner zu finden:**
ich hatte genau diese Sperre am Vortag in der Hand, festgestellt, dass sie fälschlich in NVS
landete, das behoben — und nie gefragt, ob Sperren überhaupt der richtige Entwurf ist. Der
Kommentar im Code behauptete es, und ich habe die Behauptung als Begründung akzeptiert.

Neu: **ein Tap ankert, er sperrt nicht.** Er ist weit bessere Auskunft über die *Stufe der
Taktleiter* als über die Zahl — und auf der falschen Stufe zu landen ist die charakteristische
Schwäche des Trackers (Hip-Hop 98 → gemeldet 132, also 4/3). Der Tracker misst also weiter, und
sein Ergebnis wird auf das nächstliegende einfache Verhältnis (½, ⅔, ¾, 1, 4/3, 3/2, 2) zum
getappten Wert gefaltet, sofern es innerhalb von 8 % passt. Passt gar nichts, hat sich das Tempo
wirklich geändert und der Anker verfällt nach 20 Auswertungen. Arbeitsteilung: **der Mensch ist
besser in der Stufe, das Gerät im Feinwert.**

Der Modus ist jetzt explizit, wird persistiert und startet auf Auto. Der TAP-Knopf trägt ihn:
kurzer Druck tappt, langer Druck schaltet um, und er leuchtet, solange automatisch verfolgt wird.

**Zwei Fehler beim Bauen, beide lehrreich.** Der NVS-Lesezugriff landete fünfzehn Zeilen
*unterhalb* von `prefs.end()`, wo ein Lesevorgang nicht scheitert, sondern still den Vorgabewert
liefert — der Modus sah gespeichert aus und kam nach jedem Neustart als „auto" zurück. Und der
Halte-Timer für den langen Druck lag im Abschluss einer Funktion, die bei *jedem* Rendern neu
läuft; ein Tap ändert die BPM, löst ein Rendern aus, und das folgende `pointerup` stammte aus
einem neuen Abschluss, dessen Timer null war. Der alte lief weiter und schaltete um: **zwei
schnelle Taps schalteten die Automatik ab.** Also ausgerechnet beim Tempo-Tappen, das per
Definition aus schnellen Folgedrücken besteht.

**Das Tempo sprang: 120 → 83 → 136 innerhalb von Sekunden.** Der User formulierte die fehlende
Regel: *„ein Wechsel von 120 auf 83 ist einfach unrealistisch. also ±15 % okay, ansonsten per
Hand neues Grund-BPM tappen."* Genau so gebaut — der gemeldete Wert lebt in einem Band um den
etablierten, außerhalb wird nicht veröffentlicht. Ergänzt um eine Neuerfassung nach 30 Sekunden
durchgängiger Abweichung, damit die Anzeige nach einem echten Trackwechsel nicht stumm festhängt.

**Und dann die eigentliche Ursache, die alles davor überlagert hatte.** Ein zweiter Schreiber auf
`globalBPM` ging am Band vorbei:

```c
if (now - lastBassTime > SILENCE_TIMEOUT_MS) {   // 2500 ms
    globalBPM = BPM_DEFAULT_FALLBACK;            // 120
```

Nach 2,5 Sekunden ohne Kick wurde das Tempo auf einen fest verdrahteten Wert gesetzt und die
Intervall-Historie gelöscht. Ein House-Breakdown läuft routinemäßig länger. **Das „fällt in
ruhigen Stellen ab" war also kein Messfehler, sondern ein Reset** — und die 120 waren beliebig:
auf einem 120er-Track unsichtbar, auf jedem anderen falsch. Bei Stille ist das zuletzt Gehörte
die beste verfügbare Information, und fürs Licht auch die gewünschte: der Puls läuft im
Breakdown im Tempo des Tracks weiter, bereit für den Drop.

Gemessen gegen einen bestätigten 118–122-BPM-House-Track: Schwankung **68 → 28 BPM**, gemeldeter
Wert zu über der Hälfte fest auf 119. Der Rest liegt planmäßig innerhalb des ±15-%-Bandes.

### Methodik: zweimal gegen eine ungültige Wahrheit gemessen

Eine Messreihe über 90 s ergab „Schwankung 4 BPM" — abgefragt wurde dabei nur `/api/state`, wo
der Pegel gar nicht enthalten ist. Ob überhaupt Musik lief, war damit unbelegt; der User äußerte
den Verdacht, und widerlegen ließ er sich nicht mehr. Kurz darauf verglich ich eine Messung gegen
„120 BPM" aus einer früheren Nachricht, während längst ein anderer Abschnitt lief — auch das
entwertete der User zu Recht mit *„vielleicht waren es auch einfach echte 111"*.

**Regel daraus: Wahrheit wird zum Zeitpunkt der Messung erhoben, nicht erinnert.** Der Tap-Anker
taugt dafür als Instrument — der User tappt ein paar Takte, und `tAnchor` gegen `tBPM` auf
`/api/state` liefert Wahrheit und Messung im selben Moment, ohne Rückfrage. Und jede
Tempomessung führt Pegel und Onset-Rate mit, sonst ist sie nicht deutbar.
