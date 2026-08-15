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
