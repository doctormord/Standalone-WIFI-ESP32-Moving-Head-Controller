# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Offizielles Fixture-Datenblatt (SHEHDS 160W 3in1 GOBO) beschafft und
> ausgewertet.** User lieferte Handbuch-PDF + Avolites-`.d4` +
> MagicQ-`.R20` + `.ssl2` (letzteres binär, nicht auslesbar). Alles
> extrahiert nach **`doc/content/mapping_sheds_160w_3in1_gobo.md`** — die
> jetzt maßgebliche Referenz für alle Kanal-/Gobo-/Shake-Zahlen dieses
> Fixtures. Vorher musste in dieser Session mehrfach ohne belastbare Basis
> geraten werden (Shake-Offset, Ungewissheit bei Gobo-Nummerierung) —
> das ist jetzt behoben.
>
> **Wichtigste Erkenntnisse:**
> 1. **Farbrad/Gobo-Räder/Prisma/Frost waren schon immer korrekt im Code**
>    (`wheelMap`/`sGoboMap`/`rGoboMap` stimmen 1:1 mit dem Datenblatt). Der
>    „Gobo 6 kommt nicht"-Bug aus der vorigen Runde ist damit **kein
>    Code-Bug** — vermutlich physische Abweichung/Defekt am konkreten
>    Gerät, nur durch Sichtprüfung zu klären.
> 2. **Shake-Formel war nachweislich falsch, jetzt mit echten Werten
>    gefixt.** Der alte `STEPFX_SCRATCH_OFFSET = 183` (geraten, nie
>    verifiziert) landete bei den meisten Gobo-Nummern in der falschen
>    Shake-Zone (Beispiel Gobo 6/CH7: alter Code → Zone von Gobo 7). Neue,
>    aus dem Datenblatt abgeleitete Formel: `211 + (n-1)×5` für CH7 (statisch),
>    `226 + (n-1)×5` für CH8 (rotierend), kein Shake für CH6 (Farbe, hat
>    laut Datenblatt keine Shake-Funktion) oder Index 0 (White/Open).
> 3. **CH9-Drehrichtungsgrenze (64–192 CW / 193–255 CCW) dokumentiert** —
>    aktuelle Defaults liegen sicher innerhalb einer Zone, aber jetzt als
>    Invariante für künftige Preset-Änderungen festgehalten.
> 4. **CH17-Macro-Dropdown vermutlich fixture-fremde Platzhalterwerte** —
>    erkannt, aber bewusst nicht blind gefixt (Datenblatt selbst zu grob,
>    um die 13 granularen Dropdown-Werte zu verifizieren/ersetzen).
>
> Geflasht (`pio run -t upload`, kein `buildfs` nötig — nur
> `Moving_Head_Horizon.ino` geändert), per `curl` als online bestätigt.
>
> **Was noch NICHT visuell/physisch verifiziert ist:**
> - Ob Gobo-Chaser-Shake jetzt tatsächlich sichtbar/spürbar funktioniert.
> - Ob „Gobo 6 static" wirklich ein Hardware-/Mechanik-Problem ist (durch
>   Sichtprüfung am physischen Rad zu bestätigen).
> - Alles aus den beiden vorigen Runden am selben Tag (Motor-Stop,
>   Modulator-Speed, Poll-Race, Mode/Curve, Jog-Snapback, Joystick-Curve
>   v2, drei einheitliche Joystick-Tabs, geglätteter Followspot-Marker,
>   F5-Tab-Persistenz) — siehe die beiden vorigen Handoff-Runden in
>   `history.md` für Details, alles bisher nur code-seitig verifiziert.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit der alte
> `firmware/`-Ordner beim GitHub-Push entfernt wurde.
>
> **Drei Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Am Fixture/im Browser nachtesten — insbesondere den Gobo-Chaser-Shake
>    (jetzt mit korrekten Zonen) und ob Gobo 6 tatsächlich ein
>    Hardware-Thema ist. Das ist der wichtigste nächste Schritt, den nur
>    der User erledigen kann.
> 2. Falls gewünscht: CH17-Macro-Dropdown am echten Bordmenü verifizieren
>    (`Set → Run Mode`) und ggf. durch echte Werte ersetzen.
> 3. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 4. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Neun Review-/Test-Runden durch (Details siehe `history.md`), plus jetzt
erstmals ein **offizielles Herstellerdatenblatt** als Referenz
(`mapping_sheds_160w_3in1_gobo.md`). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Verbleibender Blocker: die zuletzt gefixten
Verhaltensänderungen sind noch nicht am laufenden Gerät/im Browser
bestätigt.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet — dabei
   den Fund gemacht, dass React/Babel per CDN geladen wurden. Gefixt.
2. Stage-Map-Bild-Speichern/Laden geprüft, drei kleine Punkte gefixt.
3. `/code-review` (Vollcodebase) ergab 9 Findings — alle gefixt.
4. Projekt auf GitHub gepusht (`future`-Branch).
5. `/ultrareview` (1. Anlauf) — 15 von 18 gefixt.
6. `/ultrareview` (2. Anlauf, „nachholen") — 8 von 8 gefixt.
7. Erster echter Hardware-Test: doppelten `Content-Encoding`-Header
   gefunden und gefixt.
8. Zweiter echter Hands-on-Test: 7 gemeldete Bugs plus 2 Nachträge —
   5+2 gefixt (FX-Stop-Reset, Modulator-Speed, Poll/Toggle-Race,
   Mode/Curve-Key-Mismatch, Jog-Snapback, Joystick-Curve v1).
9. Dritte Runde, direktes Feedback zum Joystick: Curve v1 reichte nicht
   (neu gebaut als zeitbasierte 2s-Rampe), gemeinsame
   `JoystickAdvancedControls`-Komponente in alle drei Bewegungs-Tabs,
   toter Curve-Button entfernt, Advanced-Motors-Block entfernt,
   Followspot-Marker geglättet, F5-Tab-Persistenz.
10. Vierte Runde: offizielles Fixture-Datenblatt (PDF + `.d4` + `.R20` +
    `.ssl2`) ausgewertet, komplette DMX-Tabelle nach
    `mapping_sheds_160w_3in1_gobo.md` extrahiert, Shake-Offset-Formel mit
    den jetzt bekannten echten Zonen korrekt neu gebaut, Gobo-Nummerierung
    als code-seitig korrekt verifiziert (Bug vermutlich Hardware-seitig).

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die neue Fixture-DMX-Referenz —
keine Session-Historie, sondern ein Nachschlagewerk; bei Bedarf direkt
erweitern/korrigieren (kein Append-only-Zwang wie bei `history.md`).
