# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-25, Session 2)
> **Der Programmer-Tab-Sync-Bug („Werte springen nach <1 s zurück, greifen
> erst beim zweiten Auswählen") ist gefixt und compile-/syntaxgeprüft, aber
> NOCH NICHT AM FIXTURE GETESTET — dafür fehlt ein einmaliger USB-Flash.**
>
> **Stand der Arbeit:** Branch `worktree-prog-sync-fix` (Worktree unter
> `.claude/worktrees/prog-sync-fix`), ein Commit, sauber. Geändert:
> `data/index.html`, `WebAPI.h`, `Moving_Head_Horizon.ino`, plus Doku.
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
> ### ⚠️ Wichtig: OTA hat noch nie funktioniert
>
> `ArduinoOTA` war eingebunden und `handle()` lief im Loop, aber
> **`ArduinoOTA.begin()` wurde nie aufgerufen** — es gab nie einen Listener.
> `README.md` hat OTA trotzdem beworben (Zeile korrigiert). `begin()` ist
> jetzt in `setup()` ergänzt. Deshalb war ein Remote-Deploy in dieser Session
> unmöglich.
>
> ### 👉 Nächster Schritt (das ist der Blocker)
>
> **Einmalig per USB flashen — Firmware *und* Filesystem:**
> ```
> pio run -t upload      # Firmware
> pio run -t uploadfs    # data/ nach LittleFS
> ```
> Klappt der Auto-Reset nicht: `scripts/flash_esptool.sh` benutzen und
> **niemals** `firmware.factory.bin` an Offset `0x0` schreiben (wischt NVS —
> siehe `CLAUDE.md`). Ab diesem Flash ist OTA nutzbar
> (`upload_protocol = espota`, `upload_port = 192.168.8.113`), künftige
> Iterationen brauchen also kein USB mehr.
>
> **Danach testen (das ist der eigentliche Nachweis):**
> 1. Im PROGRAMMER-Tab einen FX-Trigger-Modus ~15× ändern, absichtlich auch
>    direkt nachdem ein Poll gelandet ist. **Jede** Änderung muss beim
>    *ersten* Versuch haften, ohne Zurückspringen.
> 2. Dasselbe für den Static-Gobo-Slot (`/set_all`-Pfad) und einen Regler
>    (Fokus/Zoom).
> 3. Joystick-Speed/Limits setzen, Browser neu laden → Werte müssen überleben.
> 4. Gegen das Gerät prüfen, nicht gegen das UI:
>    `curl -s http://192.168.8.113/api/get_dmx`.
> 5. **Regressionstest der neun Fixes vom Vortag** (gemeinsamer Pfad!):
>    schnelles Preset-Wechseln mit allen sieben FX aktiv — kein
>    „no slot active"-Flackern, keine Parameter, die zwischen Presets
>    überlaufen, kein FX, das beim Recall ausbleibt.
>
> Falls der Test durchgeht: Branch nach `future` mergen. Falls nicht: die
> `gsrc`-Telemetrie in `/api/get_dmx` zeigt, welche Route zuletzt geschrieben
> hat — sie liest jetzt häufiger `set_all`, das ist erwartet.
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
„Pro Beam 280", Pan 540°/Tilt 270°). Das Gerät läuft und ist unter
`movinghead.local` / `192.168.8.113` erreichbar — **aber noch mit dem Stand
der Vorsession**; die Fixes dieser Session sind noch nicht darauf. Der
Movement-FX-„Acht statt Kreis"-Effekt am Zenit-Winkel bleibt unverändert
(kein Software-Fix aktiv) — in dieser Session nicht angefasst.

Verifikation bisher: `pio run` und `pio run -t buildfs` sauber; alle acht
`<script type="text/babel">`-Blöcke mit dem mitgelieferten Babel aus
`data/vendor/` transformiert (dieselbe Transformation wie am Gerät — es gibt
keinen Build-Schritt, ein Syntaxfehler zeigte sich sonst erst als leere UI).
Root Cause 3 wurde vor dem Fix live am Gerät bestätigt. **Der Praxistest am
Fixture fehlt.**

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
