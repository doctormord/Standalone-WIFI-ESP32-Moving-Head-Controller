# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-25, Session 2)
> **Der Programmer-Tab-Sync-Bug („Werte springen nach <1 s zurück, greifen
> erst beim zweiten Auswählen") ist gefixt, geflasht (USB, Firmware + FS) und
> backend-seitig live verifiziert. Offen ist nur noch der Bedien-Test im
> Browser** — 15× einen FX-Trigger-Modus umstellen und prüfen, dass jede
> Änderung beim *ersten* Versuch haftet (siehe unten).
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
> ### 👉 Offen: der Bedien-Test im Browser
>
> Das ist der einzige verbleibende Nachweis — er braucht echte Klicks und
> konnte nicht automatisiert werden:
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
Firmware-Änderungen danach live nachgemessen (siehe Banner). **Was fehlt, ist
allein der Bedien-Test im Browser** — dafür braucht es echte Klicks.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
