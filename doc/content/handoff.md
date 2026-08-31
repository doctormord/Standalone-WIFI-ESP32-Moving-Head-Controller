# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-09-01)
> **Zuerst flashen: nur die Firmware, `data/` ist unverändert — OTA reicht.**
> ```
> python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
>         -i 192.168.8.113 -p 3232 -f .pio/build/supermini/firmware.bin -r
> ```
> Auf dem Gerät läuft der Stand *vor* der Onset-Gewichtung; die UI ist aktuell.
>
> ### Ausgangslage
> Gelöst und gemessen: Eingangsverstärkung (`ig`, ×8 ohne Clipping), erweiterter
> Sensitivity-Bereich (Faktor bis 0,5), Sync-Teiler wirkt bei Audio-Triggern,
> Mehr-Beat-Zyklen laufen, Beat-Uhr ist phasengeregelt. Kick-Trigger trifft das
> Raster: 398 ms bei 390 ms Beat, Streuung von 187 auf 107 ms halbiert.
>
> **Offen ist allein die Tempo-*Schätzung*.** Drei Verfahren sind gemessen
> gescheitert (Details in `history.md` 2026-08-31 (4)). Der gemessene Mechanismus:
> der Detektor feuert 2,67×/s bei einer Beat-Rate von 2,37×/s, und dieser Überschuss
> bildet ein konkurrierendes ~170-BPM-Raster.
>
> ### Plan für morgen, in dieser Reihenfolge
>
> **1. Referenz herstellen.** Monotonen Techno mit bekanntem, konstantem BPM
> auflegen und laufen lassen (gestern: 142). Nicht mit Drum & Bass anfangen — das
> ist der schwierigste Fall, und an ihm zu entwickeln hat gestern mehrere
> Iterationen gekostet. Erst den einfachen Fall festnageln, dann härten.
>
> **2. Gewichtete Schätzung messen** (neu, kompiliert aber ungetestet). Die Onsets
> gehen jetzt mit ihrer Stärke gewichtet in den Phasentest, damit schwache
> Zwischenschläge die Kicks nicht überstimmen. Erwartung: der Wert bleibt in der
> Nähe des echten Tempos, statt zwischen 138 und 185 zu springen. Prüfen mit
> `tBPM` aus `/api/audio_debug` über 60–90 s.
>
> **3. Den fairen A/B-Vergleich fahren — ohne eine Zeile Code.** Der User hat
> eingewandt, das alte Energiekonzept sei besser gewesen, und das ist plausibel:
> ein Kick dominiert die Gesamtenergie, ein Breitband-Transient ist robuster als
> ein schmales 31–156-Hz-Band. Der frühere Vergleich lief aber mit 9,6 %
> Aussteuerung — das alte Verfahren hat nie von Eingangsverstärkung und
> erweitertem Sensitivity-Bereich profitiert. Beide Pfade sind zur Laufzeit
> schaltbar:
> - `curl "…/audio_tune?flux=0"` → Pegel-Erkennung im Band
> - `curl "…/audio_tune?fft=0"` → ursprüngliches Breitband-Energiekonzept
> - `curl "…/audio_tune?flux=1&fft=1"` → aktueller Stand
>
> Jeweils Onset-Rate (Ziel: eine pro Beat), Intervall-Median und `tBPM` vergleichen.
>
> **4. Falls die Schätzung weiter zickt: Tempo tappen.** Das ist kein Rückzug,
> sondern das übliche Vorgehen bei Lichtpulten — Tempo per Tap setzen, Audio
> korrigiert nur die Phase, und genau die funktioniert nachweislich. Damit ist der
> Live-Betrieb unabhängig von der Schätzung nutzbar.
>
> ### Nützliche Werte
> Audio-Tuning liegt in NVS und übersteht den Reboot. Startpunkt: `ig=3` (×8),
> `sens` materialabhängig (Techno eher 30, D&B eher 70 — genau diese Abhängigkeit
> soll die neue Gewichtung beseitigen). Alles im AUDIO-Tab sichtbar: Spektrum mit
> Bandbereichen, Eingangspegel mit Clip-Anzeige, Beat-Marker je Band.
>
> ### Danach
> Der **Preset-Engine-Split (Layer)** ist weiterhin der nächste Feature-Schritt und
> braucht kein neues NVS-Format — siehe `handover.md`.
>
> ## Vorherige Session (2026-08-26)
> **Kein offener Blocker. Gerät läuft mit dem aktuellen Stand, alles ist auf
> `future` gepusht.** Zwei abgeschlossene Themen an diesem Tag:
>
> **(1) Programmer-Tab-Sync-Bug** („Werte springen nach <1 s zurück, greifen erst
> beim zweiten Auswählen") — gefixt, geflasht und vom User am Gerät gegengetestet,
> abgenommen mit „soweit alles gut". Messprotokoll weiter unten.
>
> **(2) Movement-FX „hackt" bei unterschiedlichen Size-Werten** — **kein Bug**:
> Mode 0 ist ein Sägezahn, die Size springt am Zyklusende zurück (bei
> `szSt == szEn` unsichtbar, daher fiel es nur bei unterschiedlichen Werten auf).
> Live gemessen: 9 Sprünge in 10 s, Abstände exakt = `modSp`, bis 17.728 Einheiten
> Pan in einem Frame; mit Mode 1 (Ping-Pong) null. **Lösung für den User: Mode 1
> benutzen** — Mode 2 ist ebenfalls ein Sägezahn. Dabei aber ein echter Bug
> gefunden und gefixt: `MovementEngine` hatte eine unvollständige Kopie von
> `Modulator::getLFO()`, wodurch **Cubic, Gauss und Random im Movement wirkungslos**
> waren. Jetzt eine gemeinsame `lfoShape()` für beide Engines; `Random` für
> Movement bewusst ausgeschlossen. Anschließend der ganze übrige FX-Code auf
> dieselbe Fehlerklasse geprüft — **nichts weiter gefunden** (Tabelle in
> `history.md`).
>
> ⚠️ **Beim Flashen mitdenken:** Der Live-FX-Zustand liegt *nicht* in NVS. Jeder
> Reboot (auch durch einen Firmware-/Filesystem-Upload) setzt Movement/Dimmer/
> Gobo-/Prisma-FX auf die Struct-Defaults zurück. Vorher in einen Preset speichern,
> sonst ist die Live-Einstellung weg. Presets/Chaser/Patch sind davon nicht
> betroffen. Genau das ist am 2026-08-26 einmal passiert.
>
> **Stand der Arbeit:** auf `future` gemerged (Fast-Forward) und nach GitHub
> gepusht — lokaler und Remote-Stand sind identisch, Arbeitsbaum sauber.
> Geändert: `data/index.html`, `WebAPI.h`, `Moving_Head_Horizon.ino`, plus
> Doku (`handover.md`, `backlog.md`, `functions.md`, `README.md`, dieser
> Handoff, `history.md`). Entwickelt wurde in einem Worktree
> (`.claude/worktrees/prog-sync-fix`); der kann weg, alles ist auf `future`.
>
> ### Was gemacht wurde
>
> Der User meldete, dass im Programmer-Tab ausgewählte Werte binnen einer
> Sekunde zurückspringen und erst beim zweiten Auswählen übernommen werden —
> quer über **alle** Einstellungen (FX-Trigger-Modi, Static-Gobo-Slot, …) —
> und vermutete „ein grundlegendes Sync-Problem". Das war exakt richtig: es
> waren nicht N Einzelbugs, sondern **ein gemeinsamer Schreibpfad, der Edits
> verwirft**, plus **ein gemeinsamer Merge-Pfad, der sie zurücksetzt**. Die
> neun Fixes der Vorsession hatten jeweils nur *ein konkretes Feld*
> abgesichert; die FX-Parameter und die manuellen DMX-Kanäle — der Großteil
> dessen, was der Tab editiert — hatten diesen Schutz nie.
>
> Drei Root Causes, alle gefixt (Details in `history.md` 2026-08-25 (2)):
> 1. **Primär:** die 300-ms-`isReceiving`-Totzone nach jedem Poll verwarf
>    Edits *ersatzlos* — der Effect hängt an `[state]`, `isReceiving` ist aber
>    eine ref, deren Zurücksetzen keinen Render auslöst; es gab nie einen
>    Retry. → `deferSync()` + `syncTick`.
> 2. Der Poll-Merge überschrieb **alle** FX-Parameter ungegated und zog danach
>    die `pr2.*`-Baseline auf die zurückgesetzten Werte nach — der Edit war
>    damit *gelöscht*, nicht bloß verzögert. → Gating **pro Gruppe** (Felder
>    *und* Baseline).
> 3. `/set_all` hatte keinen Generation-Schutz und antwortete mit
>    `server.send(200, "OK")` (Zwei-Argument-Überladung → `"OK"` als
>    *Content-Type*, leerer Body; live am Gerät bestätigt). → bumpt und
>    liefert jetzt die Generation, Frontend armiert `'channels'`.
>
> Mitgefixt (gleiche Fehlerklasse): `prism`/`frost` (`?? 0` statt `?? prev`),
> `presetActive` ohne `d.pr != null`-Guard, ungegatetes `setBpm`,
> `setMuted`/`setMicOn` aus dem `setState`-Updater heraus, `/chaser` ohne
> `&g=`/`isStaleWrite()`. **Separat:** `/joy_cfg` hatte keine Read-back-Route —
> ein frischer Tab überschrieb die gespeicherte Joystick-Config des Geräts mit
> UI-Defaults (neue Route `/api/joycfg`).
>
> ### ⚠️ Wichtig: OTA hatte noch nie funktioniert — jetzt repariert und verifiziert
>
> `ArduinoOTA` war eingebunden und `handle()` lief im Loop, aber
> **`ArduinoOTA.begin()` wurde nie aufgerufen** — es gab nie einen Listener.
> `README.md` hat OTA trotzdem beworben (Zeile korrigiert). `begin()` ist
> jetzt in `setup()` ergänzt, und **OTA wurde danach real getestet**: ein
> kompletter Firmware-Upload über WiFi mit
> `espota.py -i 192.168.8.113 -p 3232 -f .pio/build/supermini/firmware.bin`
> lief durch („Done..."), das Gerät kam sauber wieder hoch. Die
> Partitionstabelle ist echtes Dual-OTA (`app0`/`app1`, je 1280K, plus
> `otadata`); die Firmware belegt 1216K. **Künftige Iterationen brauchen also
> kein USB mehr.**
>
> ### ✅ Deployment (erledigt am 2026-08-25)
>
> Per USB geflasht, Firmware *und* Filesystem:
> ```
> pio run -t upload --upload-port /dev/cu.usbmodem1101
> pio run -t uploadfs --upload-port /dev/cu.usbmodem1101
> ```
> NVS blieb erwartungsgemäß unangetastet (Firmware nach `0x10000`, FS nach
> `0x290000`) — die Presets „Sky Moover"/„yellow three" waren nach dem Reboot
> unverändert da. **Niemals** `firmware.factory.bin` an Offset `0x0` schreiben
> (wischt NVS — siehe `CLAUDE.md`).
>
> Backend-seitig direkt am Gerät verifiziert:
> - `/set_all` antwortet jetzt `Content-Type: text/plain`, `Content-Length: 1`
>   mit einer echten Generation (vorher: `Content-Type: OK`, leerer Body).
> - `/api/joycfg` liefert die persistierte Joystick-Config zurück.
> - Ein FX-Parameter-Write round-trippt korrekt: `/sgobfx` mit Trigger-Modus
>   2 / Sync 4 / Hold 1500 → in `/api/get_dmx` exakt so wieder ausgelesen,
>   `gen` hochgezählt, `gsrc` = `sgobfx`, Response = die neue Generation
>   (Testwerte danach wieder auf den Ursprungsstand zurückgesetzt).
>
> ### ✅ Bedien-Test am Gerät (2026-08-26, bestanden)
>
> Methode: der User hat im Browser eine **abgezählte** Menge Änderungen
> gemacht, während parallel ein Recorder `/api/get_dmx` alle 600 ms abfragte
> und jede geräteseitige Zustandsänderung mit Zeitstempel, `gen` und `gsrc`
> mitschrieb. So ließ sich objektiv „N Klicks rein = N Writes an" zählen —
> nötig, weil die primäre Fehlerart (verworfener Edit) geräteseitig
> *unsichtbar* ist: der Wert ändert sich schlicht nicht.
>
> | Test | Ergebnis |
> |---|---|
> | FX-Trigger/Sync/Hold (`syncFx`-Pfad, `gsrc: sgobfx`) | 10 Aktionen → 10 angekommen |
> | Static-Gobo-Slot (`/set_all`-Pfad, `gsrc: set_all`) | 9 Aktionen → 9 angekommen |
> | Joystick-Config über Browser-Reload | überlebt ✅ |
> | Leerlauf ~45 s | **null** Writes, `gen` konstant |
> | Spontane Reverts | keine |
>
> Wichtige Einzelbefunde:
> - **Jede** Zustandsänderung hatte einen eigenen, lückenlos hochgezählten
>   `gen` — es hat sich also nichts ohne echten HTTP-Write geändert. Genau das
>   war beim alten Bug anders (der Poll schrieb Werte zurück).
> - Die ~45 s Leerlauf ohne einen einzigen Write belegen den
>   Baseline-Lockstep: eine untätige UI echot gepollte Werte nicht mehr zurück.
> - `/api/joycfg` meldete nach dem Reload `spd 652, crv 4.20, mom 12,
>   pmax 68, tmax 153` — klar abseits der Defaults, die der alte Code beim
>   ersten Klick nach dem Reload zurückgeschrieben hätte.
> - Einordnung: Unter dem alten Code hatte jede Änderung ~15 % Chance, in die
>   300-ms-Totzone zu fallen. Bei 19 Änderungen wären ~3 Verluste zu erwarten
>   gewesen; null Verluste hätten damals eine Wahrscheinlichkeit von ~5 %.
>   Zusammen mit dem „geht" aus dem Browser als Abnahme gewertet.
>
> **Test 5 (Preset-Regression) am 2026-08-27 nachgeholt: bestanden.** Schnelles
> Preset-Wechseln mit aktiven FX zeigte laut User „keine Auffälligkeiten" — kein
> „no slot active"-Flackern, keine überlaufenden Parameter, kein fehlendes FX beim
> Recall. Damit ist die Abnahme des Sync-Fixes vollständig; die neun Fixes vom
> 2026-08-25 sind mitgeprüft, da sie denselben Pfad benutzen. Falls doch je wieder
> etwas auftaucht: `gsrc` in `/api/get_dmx` zeigt die zuletzt schreibende Route —
> sie liest jetzt häufiger `set_all`, das ist erwartet.
>
> ### Sonst offen (unverändert aus früheren Sessions)
> 1. Mic-Sensitivity subjektiv mit echter Musik über den AUDIO-Tab prüfen.
> 2. Mic-BPM-Oktave-Korrektur noch nicht mit echtem Audio verifiziert.
> 3. Zeit-Rampen für den Shake („wa-wa-wosh"), falls weiterhin gewünscht.
> 4. Separates Followspot-Joystick-Profil?
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Movement-Zenit-„Acht" (siehe `backlog.md` → „Bekannte kleine Issues").
> 7. Übrige Tech-Debt-Punkte in `backlog.md` — insbesondere der
>    State-Sync-Neuentwurf (ein besessener Server-State bzw. WebSockets). Die
>    Lehre aus dieser Session steht dort als Nachtrag: bei diesem Problemfeld
>    den *geteilten Pfad* fixen, nicht das gemeldete Feld.

## Aktueller Status

Zielhardware: **ESP32-C3 Supermini** (Fixture: SHEHDS 160W 3in1 GOBO /
„Pro Beam 280", Pan 540°/Tilt 270°). Das Gerät läuft unter
`movinghead.local` / `192.168.8.113` **mit dem Stand dieser Session**
(Firmware + Filesystem am 2026-08-25 per USB geflasht, danach zusätzlich ein
OTA-Upload zur Verifikation). Der Movement-FX-„Acht statt Kreis"-Effekt am
Zenit-Winkel bleibt unverändert (kein Software-Fix aktiv) — in dieser Session
nicht angefasst.

Verifikation: `pio run` und `pio run -t buildfs` sauber; alle acht
`<script type="text/babel">`-Blöcke mit dem mitgelieferten Babel aus
`data/vendor/` transformiert (dieselbe Transformation wie am Gerät — es gibt
keinen Build-Schritt, ein Syntaxfehler zeigte sich sonst erst als leere UI).
Root Cause 3 wurde vor dem Fix live am Gerät bestätigt, alle drei
Firmware-Änderungen danach live nachgemessen, und am 2026-08-26 hat der User
den Bedien-Test im Browser gefahren (Messprotokoll im Banner oben) und mit
„soweit alles gut" abgenommen. **Offen bleibt allein Test 5
(Preset-Regression)**, siehe Banner.

⚠️ **Hinweis zum Gerätezustand:** Beim Joystick-Test wurden Pan/Tilt-Limits
auf Testwerte gesetzt (`pmax 68`, `tmax 153`) und sind persistent in NVS
gespeichert — das schränkt den Bewegungsbereich spürbar ein. Falls das
Fixture „nicht weit genug fährt": zuerst dort nachsehen
(`curl -s http://192.168.8.113/api/joycfg`), bevor Mechanik oder
Movement-FX verdächtigt werden.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
