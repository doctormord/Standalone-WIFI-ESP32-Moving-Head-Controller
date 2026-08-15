# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-16)
> Der Code lebt jetzt auch auf GitHub:
> `github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller`,
> Branch **`future`**. Dieses lokale Verzeichnis (`/Users/christian/
> Documents/Arduino/Moving_Head_Horizon`) ist selbst ein Git-Repo, das
> diesen Branch trackt (`origin`, SSH). `V1`/`V2`/`V3`/`firmware` wurden
> dort entfernt, `images`/`install.html`/`LICENSE` bleiben. `README.md`
> (Repo-Root, Großschreibung — das bestehende, reichhaltige Readme, nicht
> ersetzt) ist die kanonische Projektbeschreibung; das alte `Readme.md`
> gibt es nicht mehr.
>
> **Kompilier-Stand:** `pio run` und `pio run -t buildfs` liefen zuletzt
> beide `[SUCCESS]`. **Noch nicht auf echter Hardware getestet** — das ist
> nach wie vor der größte offene Punkt.
>
> **Bekannter offener Punkt aus dem Push:** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit `firmware/`
> gelöscht wurde. Braucht einen frisch gebauten `firmware/`-Ordner (echter
> Hardware-Build + Upload), bevor er wieder funktioniert — bewusst nicht
> in dieser Session erledigt (Quellcode-Push war der Auftrag, keine
> Binary-Artefakte).
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Auf echter Hardware flashen und die Fixes dieser Session verifizieren
>    — insbesondere der Blackout-Panic-Button-Fix (`/bump?t=blackout`) und
>    die neu eingeführte Chaser-Konfig-Persistierung (`/chaser` speichert
>    jetzt selbst in NVS, vorher tat das nur der tote `/chaser_cfg`).
> 2. `firmware/`-Ordner neu aufbauen (Build + Upload der Binaries), damit
>    der One-Click-Installer wieder funktioniert.
> 3. `/ultrareview` (Cloud-Multi-Agent-Review) — war für diese Session
>    angefragt, lief aber noch nicht, siehe unten.
> 4. Danach frei: Preset-Engine-Split, ADS1115-Hardware-Joystick, oder die
>    restlichen Tech-Debt-Punkte in `backlog.md` (Index-Konvention,
>    `fadeDuration`-Kopplung, Output-Build-Kadenz, `jogBend`).

## Aktueller Status

Alle bislang bekannten Bugs aus zwei vollständigen `/code-review`-Runden
(insgesamt 15 + 9 = 24 Findings) sind gefixt und kompilier-/größen-
verifiziert. Zielhardware: **ESP32-C3 Supermini**. Repository ist jetzt
sowohl lokal als auch auf GitHub (`future`-Branch) git-versioniert.
**Einziger großer Blocker vor Live-Einsatz: kein Test auf echter
Hardware bisher.**

## Was in dieser Session (Fortsetzung, 2026-08-15/16) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet —
   dabei den konkreten Fund gemacht, dass React/Babel bisher per CDN
   geladen wurden (bricht im AP-Only-Offline-Betrieb). Auf Wunsch gefixt:
   lokal gehostet, gzip-komprimiert (`data/vendor/`), neue `/vendor/*`-
   Routen in `WebAPI.h`.
2. Stage-Map-Bild-Speichern/Laden (Follower-Modus) geprüft, drei kleine
   Robustheits-/UX-Punkte gefunden und auf Wunsch gefixt (Fehler-Feedback,
   Schreib-Verifikation, Kalibrierpunkte-Reset bei neuem Foto).
3. `/code-review` (Vollcodebase, kein Git zu dem Zeitpunkt) ergab 9
   weitere Findings — alle gefixt, inkl. eines wichtigen Zusatzfunds
   unterwegs: `/chaser_cfg` war nicht nur toter Code, sondern der einzige
   Pfad, der Chaser-Konfiguration in NVS persistierte.
4. Projekt auf GitHub gepusht (`future`-Branch): `V1`/`V2`/`V3`/`firmware`
   entfernt, `images` behalten, aktueller Code + `doc/content/` + `CLAUDE.md`
   + `platformio.ini` hinzugefügt, bestehendes `README.md` gezielt
   aktualisiert statt ersetzt.
5. `/ultrareview` angefragt — **steht noch aus**, war der letzte Schritt
   vor Ende dieser Session.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
