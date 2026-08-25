# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-25)
> **Gerät ist konfiguriert, erreichbar und läuft mit dem in dieser Session
> geflashten, live verifizierten Code. Kein offener Blocker.**
>
> **Was in dieser Session gemacht wurde (volle Details in `history.md`
> 2026-08-25, Kurzfassung in `backlog.md` → „Kürzlich gefixt"):**
>
> Zwei Teile: (A) LIVE-UI-Politur auf User-Feedback zu einem Screenshot —
> größere Default-Schrift/-Buttons (CSS `zoom` auf `.tab-scroll`), eine
> gemeinsame `PresetGrid`-Komponente ersetzt den alten `<select>`-Slot-
> Picker im Programmer-Tab, neue `/save_center`-Route + Button zum
> Neuspeichern *nur* der Pan/Tilt-Center-Position eines bereits
> programmierten Slots, ein „unsaved changes"-Tracker im Programmer-Tab.
>
> (B) Eine lange, mehrstufige Bugjagd zu einem beim Testen der UI-Politur
> aufgefallenen Problem: das „welcher Preset-Slot ist aktiv"-Indicator
> flackerte auf „no slot active", später meldete der User „Parameter werden
> zwischen Presets übertragen" und schließlich, dass einzelne FX (vor allem
> Dimmer FX) beim Preset-Recall nicht zuverlässig wiederkamen. **Neun echte,
> unabhängige Root Causes gefunden und einzeln gefixt** — von zwei
> Backend-Routen, die `activePresetSlot` bei jedem Aufruf statt nur bei
> echten Übergängen zurücksetzten, über einen kompletten Umbau der
> Optimistic-Write-vs-Poll-Synchronisation (Wall-Clock-Timer →
> Generation-Counter), mehrere nie vom Poll zurückgelesene Frontend-Felder
> (`colorOff`, `panFine`/`tiltFine`), bis zu echter Daten-Korruption durch
> nie nachgezogene „zuletzt gesendet"-Baselines und eine Race zwischen zwei
> bereits abgeschickten HTTP-Requests, die am Ende einen serverseitigen
> Generation-basierten Staleness-Guard brauchte (Frontend-seitige Fixes
> allein reichten dafür nicht). Jeder Fix wurde live per `curl`-Telemetrie
> (`"gsrc"`-Debug-Feld in `/api/get_dmx`, neu und dauerhaft behalten)
> verifiziert, nicht nur kompiliert. **Vom User nach einem längeren
> Testlauf (alle sieben FX-Typen aktiv, wiederholtes schnelles
> Preset-Wechseln) als „ist gefixt jetzt" bestätigt.**
>
> ---
>
> **Kein offener Bug aus dieser Session.** Der User hat aber explizit einen
> strukturellen Punkt für eine künftige Session vorgeschlagen (ggf. mit mehr
> Reasoning-Budget/Opus): die Architektur, die diese neun Bugs erst möglich
> gemacht hat — mehrere unabhängige, asynchrone Schreibpfade ohne zentrale
> Ownership über denselben Live-State — bleibt strukturell fragil, auch
> nach allen neun chirurgischen Fixes. Siehe `backlog.md` → „Technische
> Schulden" (neuer Eintrag, ganz oben) für die volle Diskussion und
> Kandidaten (einziger besessener Server-State mit echtem Request/Response,
> oder WebSockets statt Poll+Echo — letzteres steht ohnehin schon als
> Feature-Wunsch im Backlog und würde das Problemfeld strukturell mit
> erledigen). Das ist ein guter Startpunkt, falls die nächste Session
> genau darauf fokussiert werden soll — kein akuter Blocker, aber der vom
> User selbst benannte nächste sinnvolle Schritt.
>
> Ansonsten frei (keine feste Reihenfolge, aus vorigen Sessions
> übernommen):
> 1. Mic-Sensitivity subjektiv mit echter Musik über den AUDIO-Tab
>    gegenprüfen.
> 2. Mic-BPM-Oktave-Korrektur noch nicht mit echtem Audio verifiziert.
> 3. Zeit-Rampen für den Shake ("wa-wa-wosh"), falls weiterhin gewünscht.
> 4. Separates Followspot-Joystick-Profil?
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Movement-Zenit-„Acht" (siehe `backlog.md` → „Bekannte kleine Issues")
>    — weiterhin kein aktiver Software-Fix, Formel-Bug aus dem letzten
>    Versuch dokumentiert als Ausgangspunkt.
> 7. Übrige Tech-Debt-Punkte in `backlog.md` (Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen, etc.)

## Aktueller Status

Repository ist sowohl lokal als auch auf GitHub (`future`-Branch)
git-versioniert (noch **nicht committed** — diese Session endete mit
Doku-Updates, kein `git commit` bisher). Zielhardware: **ESP32-C3
Supermini** (Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280", Pan 540°/Tilt
270°). Gerät läuft mit dem in dieser Session mehrfach geflashten und live
verifizierten Code (LIVE-UI-Politur + neun State-Sync-Fixes, siehe oben),
ist über `movinghead.local`/direkte IP erreichbar und im normalen Betrieb.
Der Movement-FX-„Acht statt Kreis"-Effekt am Zenit-Winkel bleibt
unverändert vom letzten Stand (kein Software-Fix aktiv, siehe „Bekannte
kleine Issues" in `backlog.md`) — in dieser Session nicht angefasst.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
