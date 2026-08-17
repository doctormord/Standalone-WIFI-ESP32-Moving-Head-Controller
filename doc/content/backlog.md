# Horizon Light Controller — Backlog

> Lebendes Dokument — wird bei jedem Stand aktualisiert (nicht angehäuft).
> Erledigte Punkte wandern nach `history.md` und werden hier entfernt.
> Chronologischer Kontext (wann was gefunden/gefixt/regressiert wurde) steht
> in `history.md`.

## 🛠 Technische Schulden (Tech Debt)

**2026-08-16, `/ultrareview` (Cloud-Multi-Agent):** Hauptorchestrator und
mehrere Teil-Agenten sind an einem Account-Session-Limit gescheitert, drei
Teilreviews liefen durch. Die meisten Funde wurden noch am selben Tag
gefixt (siehe „Kürzlich geklärt"), drei bewusst zurückgestellt:

- **`SceneData`-NVS-Blob hat kein Versions-Tag.** Nur ein `sizeof()`-
  Gleichheitscheck schützt vor Format-Drift — erkennt Größenänderungen,
  aber nicht Layout-Änderungen bei gleicher Größe (z. B. zwei gleich große
  Felder vertauscht). **Bewusst nicht gefixt:** Ein Versions-Feld
  hinzuzufügen ändert `sizeof(SceneData)` und würde alle *aktuell* im
  Blob-Format gespeicherten Presets beim nächsten Boot auf Defaults
  zurückfallen lassen (der Legacy-Fallback-Pfad kennt nur das ganz alte
  Einzel-Key-Format, nicht den heutigen Blob mit altem Layout) — ein
  echter Breaking Change für existierende Geräte, kein chirurgischer Fix.
  Bräuchte eine durchdachte Migration, keinen Schnellschuss.
- **`/api/get_dmx` baut die JSON-Antwort über ~50 sequenzielle
  `String +=`-Aufrufe.** Jeder `String(int)`/`String(float)`-Zwischenwert
  ist eine eigene Heap-Allokation trotz `reserve()`. Eine Umstellung auf
  `snprintf` in einen festen Puffer oder ArduinoJson wäre der eigentliche
  Fix, ist aber eine echte Restrukturierung dieses zentralen Endpunkts,
  keine punktuelle Änderung — bewusst nicht in derselben Session gemacht,
  in der auch viele andere Dateien angefasst wurden.
- **Zwei unsynchronisierte Frontend-Polling-Loops** (`useTelemetry` alle
  500 ms gegen `/api/state`, `App` alle 2000 ms gegen `/api/get_dmx`) mit
  überlappenden Feldern. Zusammenlegen wäre eine Architekturänderung am
  Polling-Modell, kein chirurgischer Fix — bewusst zurückgestellt.

- **Index-Konvention inkonsistent.** `executePreset` nutzt
  `chaserScenes[slot-1]` (1-basiert, Guard 1–10), `executeChaserSlot` nutzt
  `chaserScenes[slot]` (0-basiert, Guard 0–9). Seit 2026-08-15 sind beide
  gegen ungültige Werte abgesichert (siehe „Kürzlich geklärt"), die
  *Inkonsistenz der Konvention selbst* bleibt aber bestehen — Foot-Gun beim
  nächsten Refactor, wenn jemand die Guards kopiert statt neu herleitet.
  Vereinheitlichen wäre ein reiner Architektur-Cleanup, kein Bugfix mehr.
- **`jogBend` ist toter Code.** In `/jog` gesetzt (`WebAPI.h`), nirgends
  in `FX_Engine.h` oder der DMX-Ausgabe gelesen. Der Jog-Regler bewegt das
  Fixture aktuell **nicht** (auch in `README.md` als offener Punkt
  vermerkt). Seit 2026-08-17 snappt der UI-Regler nach Loslassen wieder
  sichtbar auf Mitte zurück (siehe „Kürzlich geklärt" — war ein separater
  UI-Bug, kein Hinweis auf die fehlende DMX-Wirkung). Weiterhin offen:
  Entweder Feature fertigbauen (`jogBend` tatsächlich in `updateEngines()`
  als Pan/Tilt-Offset verrechnen) oder Endpunkt + UI entfernen.
- **`fadeDuration` global geteilt** zwischen Mute-Fade (`/autofade`) und
  Dip-to-Black-Load (`triggerLoad` überschreibt mit `currentFadeTime/2`).
  Fragile Kopplung, entkoppeln (z. B. getrennte Variable für Dip-Fades).
- **Output-Build-Kadenz (Perf-Hebel, noch nicht nötig).** `updateEngines()`
  baut Output-Buffer + `getValues()` in *jedem* Loop-Durchlauf (Hunderte Hz),
  gesendet wird nur alle 30 ms (~15× Overhead). In den
  `if (now - lastDmxOut >= 30)`-Block ziehen → Movement-Soft-Float-Last
  um ~Faktor 15 senken. Gefahrlos, da `.process()` dt-basiert integriert und
  `getValues()` zustandslos ist. Bei 2 Fixtures unkritisch (siehe
  `handover.md` → Performance & Skalierung), aber der Hebel, der 8 Fixtures
  + Hardware-Joystick sorgenfrei macht.
- **DMX-TX ohne `uart_wait_tx_done`.** Bei vielen Fixtures (~22 ms Frame bei
  512 Kanälen) könnte der nächste Break (alle 30 ms) einen laufenden Frame
  anschneiden. Bei 1–2 Fixtures unkritisch.
- **Rotationsmatrix pro Fixture neu berechnet.** `cosf(rRad)/sinf(rRad)` in
  `MovementEngine::getValues()` ist über alle Fixtures identisch (gleiches
  `rot`), wird aber pro Fixture neu berechnet — einmal pro Frame reicht.
- **NVS-Speicher überwachen.** Presets/Chaser-Szenen als gepackter
  `SceneData`-Blob via `prefs.putBytes()`. Bei vielen Änderungen über Zeit
  ggf. Defragmentierung/Reset nötig.
- **Flash-Auslastung bei 90,3 %** (1.183.197 von 1.310.720 Bytes der
  App-Partition, gemessen via `pio run` am 2026-08-15). Wenig Puffer für
  neue Features (z. B. WebSocket-Migration, Fixture-Library). Vor größeren
  Erweiterungen ggf. Partitionsschema prüfen/anpassen (aktuell Standard-OTA-
  Schema mit zwei App-Partitions).
- **Legacy-I2S-API deprecated.** `Audio_Engine.h` nutzt `driver/i2s.h`
  (`i2s_read`, `i2s_driver_install` klassisch) — ESP-IDF markiert das als
  deprecated zugunsten von `driver/i2s_std.h`. Kompiliert aktuell noch
  (nur Warnung), sollte aber vor einem künftigen ESP-IDF-/
  arduino-esp32-Versionssprung migriert werden, falls die alten APIs
  entfernt werden.

## 🚀 Zukünftige Features (Feature Requests)

- **Hardware-Joystick via ADS1115 (I²C):** 16-bit ADC, schreibt direkt
  `joyInputX/Y` in der Firmware statt per HTTP-Roundtrip (entlastet die CPU).
  Pflicht: nicht-blockierend lesen (rate-limited Poll 20–40 ms wie
  `pollAudioEngine()`, hohe Datenrate bis 860 SPS ≈ 1,2 ms, oder
  Single-Shot-Statemachine) — sonst stallt der Loop und DMX-Timing bricht.
  Pin-Budget am C3 beachten: 4/5/6 = I²S (Audio), 7 = DMX-TX → 2 freie GPIOs
  für SDA/SCL wählen. Details in `handover.md`.
- **Preset-Engine-Split (complete / movement / color / effects):** Movement
  live aus einem Slot recallen, ohne Color/Dimmer/Effekte zu verstellen.
  `SceneData` ist per Präfix schon gruppiert (`f*` Movement, `c*/sg*/rg*`
  Color/Gobo, `d*/gr*/pr*` Modulatoren). Performance-Impact praktisch null
  (reine Architektur). Knackpunkt: `executePreset` resettet aktuell global
  (`centerPan/Tilt16`, `dimSmoothTarget`, `joySmoothX/Y`, `mapIsMoving`) —
  ein Movement-only-Recall darf statische DMX-/Color-Werte und Dimmer nicht
  anfassen. Details in `handover.md`.
- **WebSocket Integration:** HTTP-Polling (`/api/state` alle 500 ms,
  `/api/get_dmx` alle 2000 ms) auf WebSockets umstellen für Echtzeit-Feedback
  ohne wiederholten String-/Heap-Overhead beim JSON-Bauen.
- **Fixture Library (Profile):** Lampen-Profile statt hartcodiert
  18-Channel Pro Beam 280 (`CH_*`-Defines + `wheelMap`/`sGoboMap`/`rGoboMap`).
- **React Code-Splitting:** Monolithische `data/index.html` in Komponenten +
  echten Build-Schritt (z. B. Vite) statt In-Browser-Babel. Der AP-Only-
  Offline-Grund dafür ist seit 2026-08-15 durch lokales Hosting von React/
  Babel entfallen (siehe „Kürzlich geklärt") — verbleibender Nutzen wäre
  nur noch echtes Modulsystem (kein Cross-Scope-Bug-Risiko wie bei den
  `COLORS`-Findings) und kleinere Bundles. Kein akuter Grund mehr.
- **Erweitertes Art-Net:** Multi-Universe-Support, besseres HTP/LTP-Merging
  zwischen Art-Net-Input und laufenden FX.

## 🐛 Bekannte kleine Issues (Low Priority)

- **WLAN Reconnect:** Bei sehr schwachem Signal reagiert der C3 trotz
  `WiFi.setAutoReconnect(true)` manchmal träge.
- **UI State Sync:** Zwei gleichzeitig offene Browser-Fenster überschreiben
  sich beim Auto-Sync (Polling) teilweise gegenseitig.
- **Statische Gobo-/Rotating-Gobo-Nummern stimmen laut Hardware-Test nicht.**
  User-Report 2026-08-17: „Gobo 6 static kommt nicht". `SGOBOS`/`sGoboMap`
  (`data/index.html`, `Moving_Head_Horizon.ino`) sind seit Einführung
  unverändert und intern konsistent (Frontend CH7-Wert == Backend-Map-Wert,
  per `git log` verifiziert) — das ist also keine Code-Regression, sondern
  vermutlich eine Diskrepanz zur echten DMX-Personality des Fixtures
  (Pro Beam 280). Braucht entweder das Fixture-Datenblatt oder einen
  manuellen DMX-Sweep auf CH7 (0–255 langsam durchfahren, echte
  Gobo-Wechsel-Grenzen live am Gerät notieren), um `SGOBOS`/`RGOBOS`/
  `sGoboMap`/`rGoboMap` korrekt zu kalibrieren. Nicht blind fixbar.
- **Chaser-„Shake" (`STEPFX_SCRATCH_OFFSET = 183`) erzeugt laut Hardware-
  Test keinen sichtbaren Shake-Effekt, läuft „einfach durch".** Der Wert
  183 wurde am 2026-08-16 selbst als Platzhalter benannt (vorher unbenannte
  Magic Number `183, // fixture-specific`), nie gegen echte Hardware
  verifiziert. `runStep()` addiert ihn aktuell nur als Konstante auf den
  jeweiligen Wheel-Schritt-Wert (`map[idx] + 183`, im selben Hold-Time-Takt
  wie normales Chasen) — das erzeugt keinen echten Shake (schnelles Zittern
  innerhalb eines Hold-Intervalls), sondern verschiebt die Chase-Sequenz
  nur in eine andere DMX-Zone. Die statische „Shake"-Option in
  `SGOBO_BASES`/`RGOBO_BASES` (Werte 211+/193+) ist vermutlich näher an der
  echten Fixture-Shake-Zone, aber ob/wie sich das mit laufendem Chasing
  kombinieren lässt, ist ohne Fixture-Datenblatt oder Hardware-Sweep nicht
  sicher zu sagen. Nicht blind fixbar.

## ✅ Kürzlich geklärt (kein Bug) / kürzlich gefixt

- **`colFX`/`sgobFX`/`rgobFX` fehlten als globale Deklarationen (Build-Blocker).**
  Gefixt am 2026-08-15: `StepFX colFX, sgobFX, rgobFX;` im `.ino` ergänzt.
  Siehe `history.md` (2026-08-15) für die Fundgeschichte (Root Cause anhand
  einer älteren Backend-Version aus `Moving_Head_redesign.zip` verifiziert).
- **Frontend-„Redesign" (`Moving_Head_redesign.zip` → `data/index_claude.html`)
  geprüft — kein Merge nötig.** War ein visuelles React-Prototyp-Vorstadium
  (Fake-Telemetrie, tote Buttons) der aktuellen `data/index.html`. Das
  aktuelle Frontend ist die vollständig verdrahtete, dichter geschriebene
  Weiterentwicklung davon (u. a. echtes OTA, echte Telemetrie, DOM-optimierte
  Beat-LEDs statt React-Re-Render). Keine verlorene Funktionalität gefunden.
  Details in `history.md` (2026-08-15).
- **Preset Save/Recall vollständig?** Ja — alle 57 `SceneData`-Felder und
  alle 18 DMX-Kanäle round-trippen korrekt über beide Recall-Pfade
  (`executePreset`, `triggerSceneFX`/`executeChaserSlot`). Einzige Nuance:
  Step-FX (Color/Gobo) springen beim Recall auf `startVal` zurück, LFO/
  Movement behalten ihre laufende Phase weiter — **bewusst so gewollt**
  (organischerer Look beim Wiederaufrufen), kein Fix nötig. Details in
  `history.md` (2026-06-14).

**2026-08-15, `/code-review max` + Fixes:** Vollständiger Review (Backend +
Frontend, kreuzverifiziert) fand 15 Findings; 14 wurden noch in derselben
Session gefixt und per `pio run` gegenkompiliert (Details siehe
`history.md`). Nur das FX-Engine-Duplikations-Item oben blieb bewusst offen.

- **`/save_patch`-Overflow.** Kein serverseitiger Check auf die
  Fixture-Anzahl `n` gegen das feste `Fixture fixtures[8]`-Array — `n>8`
  hätte Speicher hinter dem Array überschrieben. Gefixt: `n` auf 1–8
  geklammert.
- **`syncBeats[]` Out-of-Bounds (Regression, erneut gefixt).** Wieder
  ungeklammert in `FX_Engine.h:68`/`:117`, `runStep`-Trigger und
  Chaser-Step-Trigger — plus jetzt auch an allen Setzstellen in `WebAPI.h`
  (`/fx`, `/modfx`, `/colfx`, `/sgobfx`, `/rgobfx`, `/chaser`,
  `/chaser_cfg`). Gefixt an allen Stellen (Lese- *und* Schreibpfad), nicht
  nur punktuell wie in der Juni-Session.
- **Slot-Index ohne Bounds-Check (Regression, erneut gefixt).** `/save`,
  `executePreset`, `executeChaserSlot` validieren jetzt wieder ihren
  jeweiligen Slot-Bereich.
- **StepFX-Map-Index unbounded.** `runStep` prüfte `currentIdx` nur gegen
  das UI-gesetzte `endVal`, nicht gegen die reale Array-Größe von
  `wheelMap`/`sGoboMap`/`rGoboMap`. Gefixt: `runStep` bekommt jetzt die
  echte Map-Länge und klammert Index *und* `startVal`/`endVal` dagegen;
  zusätzlich klammern `/colfx`, `/sgobfx`, `/rgobfx` `st`/`en` jetzt auch am
  Eingang (0–19 / 0–9 / 0–6).
- **`dimSmoothVal` konnte NaN/Infinity erzeugen.** `/smooth` klammert jetzt
  auf 0–100 (auch beim NVS-Load in `setup()`).
- **`hwAudioSensitivity` konnte negative Schwelle erzeugen.** `/hwaudio`
  klammert `sens` jetzt auf 0–100.
- **`/joy_cfg` erlaubte invertierte Pan/Tilt-Limits.** `min > max` wird
  jetzt erkannt und vertauscht.
- **`/modfx` fiel bei unbekanntem `pfx` still auf `pRotFX` zurück.**
  Gefixt: `pfx` wird jetzt explizit gegen `"dim"`/`"gr"`/`"pr"` geprüft,
  alles andere macht nichts (`m == nullptr`).
- **`executePreset` setzte `lastStepTime` von Color/Gobo-StepFX nicht
  zurück.** Im Gegensatz zu `triggerSceneFX` (Chaser-Pfad) lief der
  Hold-Timer nach einem Preset-Recall mit dem alten Zeitstempel weiter.
  Gefixt: `executePreset` setzt jetzt `lastStepTime = millis()` für alle
  drei StepFX.
- **Frontend: Movement-FX Mode/Curve syncten nie vom Gerät.** Der
  `/api/get_dmx`-Poll schrieb `d.fMM` in ein totes `fxMM`-Feld statt in
  `state.fxMode`, und las `d.fMC` (Curve) gar nicht. Dadurch konnten
  unrelated Edits die echten Mode/Curve-Werte des Geräts stillschweigend
  überschreiben. Gefixt in `data/index.html`.
- **Frontend: Farb-Chaser-Dropdown falscher Scope.** `ChaserFx` band sich
  an ein 10-Einträge-`COLORS` aus dem falschen Lexical Scope statt an eine
  1:1-Abbildung von `wheelMap[20]` — jede Farbe außer der ersten landete auf
  der falschen Wheel-Position. Gefixt: neues, korrekt 1:1 zugeordnetes
  `COLOR_STEPS` im richtigen Scope.
- **Frontend: Fixture-Count-Anzeige sprang auf 1 zurück.** Der `/api/
  get_dmx`-Poll überschrieb den einmalig korrekt von `/api/patch` gesetzten
  Wert bei jedem Tick. Zeile ersatzlos entfernt.
- **Frontend: Followspot-„Color"-Dropdown war wirkungslos.** Schrieb in ein
  nirgends gelesenes `state.color`-Feld statt in `colorBase`/`colorOff`
  (das tatsächlich an `CH.COLOR` gesendet wird). Gefixt: Dropdown steuert
  jetzt `colorBase` mit einer lokal (scope-korrekt) deklarierten Farbliste.
- **Deutsche Kommentare** in `FX_Engine.h`, `Audio_Engine.h` und
  `data/index.html` ins Englische übersetzt (CLAUDE.md-Regel).

**2026-08-15, Fortsetzung — FX-Engine-Feldkopien dedupliziert.** Der Review
hatte hier von drei duplizierten Stellen gesprochen; beim genauen Nachlesen
waren es nur zwei: `executeChaserSlot()` und der Chaser-Fade-Ende-Block
riefen schon vorher beide `triggerSceneFX()` auf, nur `executePreset()`
hatte eine eigene, ~28 Zeilen lange Parallel-Implementierung (las aus einer
lokalen `SceneData`-Kopie statt aus `chaserScenes[]`). `executePreset()`
ruft jetzt ebenfalls `triggerSceneFX(slot - 1)` auf statt die Felder
nochmal selbst zu kopieren — alle drei Recall-Pfade laufen jetzt über eine
einzige Implementierung. Kleine, dokumentierte Verhaltens-Nuance: Der
`currentIdx`/`lastStepTime`-Reset für inaktive Color-/Gobo-StepFX passiert
jetzt nur noch, wenn die jeweilige StepFX aktiv ist (wie schon immer bei
`triggerSceneFX`), vorher passierte er in `executePreset` unbedingt —
folgenlos, da `runStep()` inaktive StepFX ohnehin komplett überspringt.
Mit `pio run` gegenkompiliert (Flash-Nutzung sank minimal auf 90,5 %,
weniger Code). Details in `history.md`.

**2026-08-15, Fortsetzung — React/Babel lokal statt CDN.** `data/index.html`
lud React/ReactDOM/Babel bisher von `unpkg.com` — ohne Internet-Uplink (z. B.
WiFi-AP-Fallback am Venue ohne WLAN) blieb die UI leer. Jetzt gzip-komprimiert
unter `data/vendor/*.gz` auf LittleFS (React+ReactDOM production, Babel
Standalone — Babel allein ist unkomprimiert 2,4 MB, passt nur gzip in die
1.408-KB-Partition), ausgeliefert über drei neue Routen in `WebAPI.h`
(`/vendor/react.js` usw., mit explizitem `Content-Encoding: gzip`-Header).
`pio run -t buildfs` gegen die reale Partitionsgröße verifiziert (inkl.
Gegenprobe mit künstlich zu großer Datei, um zu bestätigen, dass Overflow
wirklich erkannt wird). Trade-off: React läuft jetzt im production- statt
development-Build (kleiner, aber ohne die ausführlichen Dev-Warnungen in der
Browser-Konsole). Details in `history.md`.

**2026-08-15, Fortsetzung — Stage-Map-Bild (Speichern/Laden/Alignment) geprüft.**
Upload → Canvas-Resize (250px) → JPEG Q0,3 → Base64 → `/save_map` →
`/map.json` auf LittleFS → `/load_map` beim nächsten Laden: technisch
korrekt, Bildgröße (typ. 3–20 KB) unkritisch für Speicher/RAM. Die
bilineare Interpolation für Tap-to-Move (`handleMapTap`) wurde
nachgerechnet — Eckpunkt-Reihenfolge (TL/TR/BR/BL) ist konsistent mit der
Interpolationsformel, mathematisch korrekt. Kein Bug gefunden, nur die drei
Robustheits-/UX-Punkte oben unter „Bekannte kleine Issues" neu aufgenommen.

**2026-08-15, Fortsetzung — die drei Stage-Map-Punkte gefixt.**
- **`/save_map`-Fehler-Feedback:** Handler prüft jetzt `LittleFS.open` (bei
  Fehler `500`) und ob `f.print()` wirklich alle Bytes geschrieben hat (bei
  Mismatch wird die unvollständige `/map.json` gelöscht statt korrupt
  liegenzubleiben, `500`). Fehlender Body → `400`. Beide Frontend-
  `fetch('/save_map', ...)`-Aufrufe (initialer Foto-Upload, "SAVE POINT"-
  Button) prüfen jetzt `r.ok` und zeigen bei Fehlschlag einen Fehler-Toast
  statt stillschweigend "gespeichert" zu suggerieren.
- **„Replace photo" Kalibrierpunkte:** `nextPoints` beim Foto-Upload ist
  jetzt immer der Default-Punktesatz (4 Ecken, 10 %/90 %, Center-Pan/Tilt)
  statt der alten `s.mapPoints` — jedes neue Foto verlangt sichtbar neue
  Kalibrierung, keine stille Fehlausrichtung mehr. Toast weist explizit
  darauf hin ("recalibrate the corners").

Mit `pio run` (Firmware) und `pio run -t buildfs` (LittleFS-Image gegen die
reale Partition) verifiziert, beides `[SUCCESS]`. Details in `history.md`.

**2026-08-15, Fortsetzung — alle 9 `/code-review`-Findings gefixt, plus ein
zusätzlicher Fund dabei.**
- **Blackout-Panic-Button:** nutzt jetzt `/bump?t=blackout` (Instant-Cut,
  wie Blinder/Strobe) statt `/set_all` (gesmoothter Dimmer-Pfad). Bestätigt
  per Code (`className="panic-row"`, eigener „Fade Out"-Button direkt
  daneben) als bewusst als Notaus gedacht.
- **„SYSTEM RESET"-Text korrigiert** (jetzt „RESET WIFI" mit ehrlichem
  Bestätigungstext) statt destruktive Funktionalität nachzurüsten, die der
  Text fälschlich versprach.
- **`triggerSceneFX`/`/save` wheelMap-Index geklammert** (`constrain` beim
  Laden aus `SceneData` und nochmal defensiv beim direkten Array-Zugriff
  in `/save`).
- **`/joy_cfg` klammert Pan/Tilt-Limits jetzt auf 0–255** vor dem Shift.
- **Chaser-Start/End-Slot wird vertauscht statt einzufrieren.**
- **`/save_patch` klammert die Fixture-Adresse auf 1–495.**
- **`/chaser_cfg` entfernt** — **dabei Zusatzfund:** es war nicht nur toter
  Code, sondern auch der *einzige* Pfad, der Chaser-Konfiguration in NVS
  persistierte. `/chaser` (der tatsächlich genutzte Endpunkt) setzte
  dieselben Variablen, speicherte aber nie — Chaser-Einstellungen haben
  also nie einen Neustart überlebt. Persistierung jetzt nach `/chaser`
  verschoben, nicht nur der tote Handler gelöscht.
- **`colFX.step`-Parität dedupliziert** in einen gemeinsamen Helper
  (`updateColFXStep()`), von `triggerSceneFX` und `/colfx` genutzt.
- **`/load_map` sendet jetzt `application/json`** statt `"json"`.

Mit `pio run` und `pio run -t buildfs` verifiziert, beides `[SUCCESS]`,
Flash-Nutzung minimal gesunken. Details in `history.md`.

**2026-08-16, `/ultrareview`-Findings gefixt (15 von 18).** Sechs davon
selbst am Code verifizierte, echte Bugs:

- **`/autofade` NaN-Risiko behoben:** Leaf-Guard (`fadeDuration > 0 ? ... :
  1.0f`, analog zum Chaser-Fade) plus Boundary-Clamp am Eingang (1–3.600.000 ms).
- **`updateEngines()`s `dt` jetzt nach oben geklammert** (`> 1.0f → 0.02f`),
  konsistent mit `FX_Engine.h`. Verhindert Bewegungs-Sprünge nach langen
  Stalls (z. B. dem bis zu 20s WLAN-Verbindungsaufbau in `setup()`).
- **`/set_all` klammert Kanalwerte jetzt auf 0–255** vor dem Byte-Cast.
- **`beatInterval`-Division jetzt mit Zero-Guard**, analog zum `/beat`-Handler.
- **Magic Channel-Numbers 13/14 → `CH_FOCUS`/`CH_ZOOM`.** Neue Konstanten
  ergänzt, an beiden Chaser-Crossfade-Stellen eingesetzt.
- **Frontend: `presetActive` resettet jetzt korrekt auf `null`**, wenn das
  Backend `pr=0` meldet, statt nur bei `pr>0` zu aktualisieren.

Plus neun weitere Duplikations-/Robustheits-/Effizienzpunkte: `executePreset`/
`/save`/`/set_all` nutzen jetzt durchgängig `CH_*`-Konstanten statt roher
Pan/Tilt-Kanalliterale; `/joy_cfg`s doppelte Min/Max-Swap-Logik in ein
gemeinsames Lambda gezogen, `spd`/`crv`/`mom` jetzt ebenfalls geklammert
(vorher nur `pmin`/`pmax`/`tmin`/`tmax`); Magic-Constant `183`
(StepFX-Scratch-Offset) als `STEPFX_SCRATCH_OFFSET` benannt; `/save`
aktualisiert `chaserScenes[]` jetzt direkt im Speicher statt aller 10
NVS-Slots neu zu laden; `setup()` liest Preset-Namen nicht mehr doppelt
(macht `loadAllChaserScenes()` ohnehin); doppelte `sinf`/`cosf`-Aufrufe in
den Movement-Shapes 3/5/10 in `FX_Engine.h` dedupliziert; `Audio_Engine.h`
berechnet Median/BPM-Smoothing nur noch, wenn tatsächlich ein neues Sample
geschrieben wurde; Frontend: Seiteneffekt (`fetch`) aus dem
`setState`-Updater der Stage-Map-„SAVE POINT"-Aktion herausgezogen (liest
`state` jetzt direkt aus dem Closure statt aus dem Updater-Parameter).

Drei Punkte bewusst zurückgestellt (siehe „Tech Debt" oben): `SceneData`-
Versions-Tag, `/api/get_dmx`-JSON-Bau-Rewrite, Polling-Loop-Merge — alle
drei sind echte Restrukturierungen mit höherem Risiko, keine chirurgischen
Fixes.

Mit `pio run` und `pio run -t buildfs` verifiziert, beides `[SUCCESS]`.
Details in `history.md`.

**2026-08-16, `/ultrareview` mit korrektem Diff-Scope nachgeholt — 8 von 8
Findings gefixt.** Alle selbst am Code verifiziert, bevor gefixt:

- **4 FX-Panels (Movement/Dimmer/Gobo-Rot/Prisma-Rot) an falsche State-
  Keys gebunden — größter Fund.** Trigger/Sync/Manual-Speed-Regler (und
  bei Movement zusätzlich Speed-/Size-Envelope) nutzten Langform-Keys
  (`fxTrigger`, `dimSync`, `grSpeed`, …), die sonst nirgends im Code
  vorkamen — der tatsächlich gesendete/empfangene State nutzt
  Kurzform-Keys (`fxTr`, `dimSy`, `grSp`, …). Diese Regler waren dadurch
  in beide Richtungen komplett wirkungslos (Änderung erreicht das Gerät
  nie, Anzeige zeigt nie den echten Geräte-Wert). Nur der Farb-/Gobo-
  Chaser war korrekt verdrahtet. Alle 16 betroffenen Keys auf die
  korrekten Kurzformen umgestellt.
- **`/chaser` restartete bei jeder Config-Änderung, nicht nur bei Ein/Aus.**
  `startFresh`-Guard ergänzt (analog zu `/fx`/`/modfx`) — ein Hold-Time-
  Regler mittendrin im Chaser-Lauf reißt die Sequenz nicht mehr ab.
- **Frontend `track()` verschluckte Kanal-Änderungen dauerhaft** (nicht
  nur verzögert), wenn sie ins 300ms-Zeitfenster nach dem Poll fielen —
  die Vergleichs-Baseline wurde aktualisiert, obwohl der Wert nie
  gesendet wurde. Baseline-Update jetzt nur noch im selben Zweig wie der
  tatsächliche Versand.
- **`/colfx`/`/sgobfx`/`/rgobfx` fehlte der Start>End-Swap-Guard**, den
  `/chaser` schon hatte — jetzt ergänzt, verhindert eingefrorene
  Farb-/Gobo-Chaser bei vertauschter Auswahl.
- **Pan/Tilt-Live-Anzeige fror während aktivem Movement-FX ein.** Frontend
  liest jetzt `d.cp`/`d.ct` (die immer aktuellen 16-Bit-Werte, auf
  8-Bit-UI-Skala umgerechnet) statt der 8-Bit-`dmxData`-Bytes, die
  `updateEngines()` bei aktivem Movement-FX gar nicht mehr schreibt.
- **`/save` prüft jetzt den NVS-Schreibvorgang** (Rückgabewert von
  `putBytes()`), bevor der In-Memory-Zustand aktualisiert wird — eigene
  Regression aus der Persistierungs-Optimierung der Vorrunde behoben.
- **Dip-to-Black bei ungültigem Slot spielt nicht mehr sinnlos ab.**
  `triggerLoad()` validiert den Slot jetzt *vor* dem Start des Fades,
  nicht erst danach.
- **Letzter deutscher Kommentar übersetzt** (`data/index.html`).

Mit `pio run` und `pio run -t buildfs` verifiziert, beides `[SUCCESS]`.
Details in `history.md`.

**2026-08-17, erster echter Hardware-Test — ein neuer Bug live gefunden
und gefixt.** `/vendor/react.js`/`react-dom.js`/`babel.js` sendeten
`Content-Encoding: gzip` **doppelt**: `WebServer::streamFile()` erkennt
`.gz`-Dateinamen selbst und setzt den Header automatisch, das eigene
`server.sendHeader("Content-Encoding", "gzip")` davor kam also zusätzlich
oben drauf. Per `curl -D -` am echten Gerät gesehen (`Content-Encoding:
gzip` zweimal) — laut HTTP-Semantik äquivalent zu `gzip, gzip`, was
Browser dazu bringen kann, den Body fälschlich zweimal zu entgzippen und
zu scheitern. Fix: die drei manuellen `sendHeader`-Aufrufe entfernt,
`streamFile()` übernimmt das für `.gz`-Dateien allein. Mit `curl
--compressed` (dekodiert wie ein Browser) verifiziert: Body dekodiert
jetzt sauber zu echtem React-Quellcode. Kein `pio run -t buildfs` nötig
(nur `WebAPI.h`, kein Dateisystem-Inhalt geändert).

**2026-08-17, Fortsetzung — 5 Bugs aus echtem Hands-on-Test gefixt.** User
hat live am Fixture getestet (nicht nur `curl`) und einen konkreten
Fehlerbericht mit 7 Punkten geliefert plus 2 Nachträge. Fünf davon
root-caused und gefixt:

- **`kill_fx`/FX-Stop bewegte Gobo-Rotation/Prisma-Rotation nicht auf 0.**
  `updateEngines()` schrieb `dmxData[9]`/`dmxData[11]` nur, solange
  `gRotFX.active`/`pRotFX.active` true war — beim Stoppen blieb der letzte
  FX-Wert einfach eingefroren stehen, der Motor lief sichtbar weiter.
  Gefixt: einmaliger Reset auf 0 exakt bei der Flanke aktiv→inaktiv
  (`gRotWasActive`/`pRotWasActive`), damit manuelle Steuerung danach nicht
  blockiert wird.
- **Dimmer-/Gobo-Rot-/Prisma-Rot-FX „extrem ruckelig" bei Zeiten ≠ 1 ms.**
  `Modulator::process()`s Phasenformel (`speed / 100.0f`) war für die
  Frontend-Defaults (2000) nicht kalibriert — ergab bei Default-Speed eine
  Zykluszeit von ~25 ms (40 Hz), viel zu schnell für einen Motor. Divisor
  auf `2000.0f` angehoben (gleiches Divisor/Default-Verhältnis wie bei der
  strukturell identischen, nachweislich funktionierenden
  `MovementEngine::process()`, die mit Divisor 100 und Default 100 eine
  saubere 0,5s-Zykluszeit ergibt) — Default-Speed liefert jetzt ebenfalls
  0,5s. `MovementEngine::process()` bewusst nicht angefasst.
- **Dimmer-FX „schaltet sich manchmal selbst aus", Color-Chaser „läuft
  manchmal weiter nach Stop oder geht selbst wieder an" — Sync-Race mit
  dem `/api/get_dmx`-Poll (User-Diagnose war korrekt).** Der 2s-Poll konnte
  eine gerade lokal umgeschaltete Running-Flag (`dimFxRunning`,
  `colFxRunning`, …) mit einer noch älteren, vom Gerät noch nicht
  aktualisierten Antwort überschreiben. `isReceiving` schützte bisher nur
  ausgehende Sends, nicht eingehende Poll-Werte. Gefixt: neuer
  `dirtyUntilRef`-Mechanismus — beim tatsächlichen Senden einer
  Running-Flag-Änderung wird das Feld für 2,5s (mehr als ein Poll-Zyklus)
  als „lokal frisch" markiert, der Poll überschreibt es in diesem Fenster
  nicht. Betrifft alle 8 Running-Flags (`fxRunning`, `dimFxRunning`,
  `grFxRunning`, `prFxRunning`, `colFxRunning`, `sgFxRunning`,
  `rgFxRunning`, `showRunning`).
- **Jog-Regler (Live-Tab) snappte nach Loslassen nicht auf Mitte zurück.**
  `JogDial` rief `onRelease` über ein komplett unsichtbares
  (`display:'none'`), separates `<input type="range">`-Dummy-Element auf —
  das nie echte Maus-/Touch-Events bekommt, weil unsichtbare Elemente vom
  Browser gar nicht erst interaktiv sind. Der eigentliche, sichtbare
  `RangeSlider` hatte gar keinen `onRelease`-Callback. Gefixt: `RangeSlider`
  bekommt ein echtes `onRelease`-Prop (ruft in `handleUp` auf), `JogDial`
  reicht es jetzt an den echten Regler durch, das tote Dummy-Element
  entfernt. (Wichtig: `jogBend` selbst bewegt weiterhin keine DMX-Kanäle —
  siehe „Tech Debt" oben, separates, schon vorher bekanntes Thema.)
- **Movement-„Curve"-Regler (virtueller Joystick/Pfeiltasten) ohne
  sichtbare Wirkung.** Root Cause: `joyInputX`/`joyInputY` sind bei
  Tastatur-Input und bei voll ausgelenktem virtuellem Joystick strukturell
  immer ein normalisierter Einheitsvektor (Betrag exakt 1) —
  `powf(1, curve)` ist für jeden Kurven-Wert 1, die Kurve konnte also nur
  bei diagonalen Tasten-Kombos oder partiellem Maus-Drag überhaupt etwas
  bewirken. Keine Regression (Formel seit Code-Konsolidierung unverändert,
  per `git log` verifiziert), sondern ein grundlegendes Vorher-Problem.
  Gefixt: Kurve wird jetzt auf den geglätteten Rampen-Wert (`joySmoothX/Y`,
  läuft bei jeder Eingabemethode durch alle Werte zwischen 0 und ±1)
  angewendet statt auf den rohen Input — ergibt eine echte, sichtbare
  Anfangsbeschleunigung unabhängig von Maus/Tastatur, ohne die
  Momentum-gesteuerte Ramp-Geschwindigkeit selbst zu verändern.

Zwei Punkte aus demselben Test bewusst **nicht** blind gefixt (Fixture-DMX-
Personality-Daten, nicht am Code verifizierbar) — siehe „Bekannte kleine
Issues" oben: Gobo-Nummerierung, Chaser-Shake-Offset.

Mit `pio run` und `pio run -t buildfs` verifiziert, beides `[SUCCESS]`, auf
dem echten angeschlossenen Gerät geflasht (`pio run -t upload` +
`-t uploadfs`) und per `curl` als online bestätigt. Details in `history.md`.
