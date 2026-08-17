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
