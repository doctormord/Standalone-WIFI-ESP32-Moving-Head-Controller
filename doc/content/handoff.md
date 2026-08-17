# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Antwort auf die drei Rückfragen aus der vorigen Runde erhalten, zwei
> davon direkt umgesetzt, Shake-Kalibrierung mit dem User begonnen.**
>
> **Gefixt:**
> - **Kurzer Tastatur-Tap bei Curve/Momentum=0 bewegte zu weit.** Ohne
>   Rampe ist die Bewegungsdauer direkt an das Zeitfenster gekoppelt, in
>   dem das Backend `joyInputX≠0` sieht — und das wird durch reale
>   Netzwerk-Latenz (Start- bis Stop-Ankunft) aufgebläht, nicht nur durch
>   die echte Tastendruckdauer. `useKeyboardJoystick` verzögert das
>   Committen des ersten Bewegungsbefehls jetzt um 15 ms; wird die Taste
>   vorher losgelassen, wird gar nichts gesendet. Echtes Halten (>15ms)
>   bleibt unverändert „sofort mit voller Geschwindigkeit".
> - Der zweite Teil („Loslassen bei Momentum=0 = harter Stopp") war
>   bereits durch vorige Runden abgedeckt — bestätigt, keine Änderung
>   nötig.
>
> **Begonnen, Ergebnis steht noch aus:**
> - **Gobo-Shake-Kalibrierung:** CH7 (statisches Gobo) manuell durch alle
>   5 Werte der Gobo-1-Shake-Zone (211–215) gefahren, User beobachtet live
>   am Fixture. **Auswertung durch den User steht noch aus** — das ist der
>   wichtigste nächste Schritt in der nächsten Runde, um die
>   Shake-Speed/-Range-Parameter auf echte Daten statt Annahmen zu
>   stellen.
>
> **Geklärt (keine Code-Änderung, nur Antwort):**
> - **NVS-Frage:** `joySpeed`/`joyCurve`/`joyMomentum` werden bereits
>   persistiert, aber als **ein globaler Satz**, nicht separat für
>   Followspot — alle drei Tabs teilen sich seit der Controls-
>   Vereinheitlichung denselben State. Ein echtes Followspot-eigenes
>   Profil wäre ein neues Feature (eigene Backend-Variablen/NVS-Keys),
>   beim User rückgefragt statt blind gebaut.
> - **Movement-Stop mit Momentum:** auf expliziten Wunsch des Users
>   offen gelassen („scheint iwie anders geworden zu sein"), keine
>   Änderung in dieser Runde.
>
> **Was noch NICHT visuell/physisch verifiziert ist:** der neue
> 15ms-Commit-Delay-Fix, alle Ergebnisse der Shake-Kalibrierungs-Session
> (steht noch aus), plus alles aus den vorigen Runden am selben Tag.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> ist kaputt (fehlender `firmware/`-Ordner). Layout-Bug bei „MAX"-Reglern
> in Movement FX weiterhin ungeklärt. Movement-Stop-mit-Momentum bewusst
> offen. Followspot-eigenes Joystick-Profil als mögliches neues Feature
> im Raum, noch nicht bestätigt/beauftragt.
>
> **Vier Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen, der Layout-Bug bei den „MAX"-Reglern.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. **User berichtet, was er bei den 5 Shake-Kalibrierungswerten
>    (CH7=211–215) beobachtet hat** — daraus die Shake-Speed/-Range-Logik
>    neu ableiten (echte Daten statt Annahmen). Wichtigster nächster
>    Schritt.
> 2. Am Fixture/im Browser den 15ms-Commit-Delay-Fix nachtesten.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Dreizehn Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Diese Runde zeigt einen Musterwechsel: statt eines
dritten Blind-Guess-Versuchs beim Gobo-Shake wurde erstmals eine
gemeinsame, interaktive Hardware-Kalibrierung mit dem User begonnen
(manueller DMX-Sweep + Live-Beobachtung) — ein Modell, das sich bei
ähnlich unklaren, undokumentierten Fixture-Verhalten künftig wiederholen
lässt.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–12: siehe vorige Handoff-Snapshots / `history.md`.
13. Siebte Live-Test-Runde: 15ms-Commit-Delay für Tastatur-Joystick
    (behebt Kurz-Tap-Latenzfenster-Bug bei Curve/Momentum=0), Gobo-
    Shake-Kalibrierung mit dem User begonnen (manueller DMX-Sweep über
    `curl`, Ergebnis steht aus), NVS-Persistenz-Frage beantwortet
    (bereits gespeichert, aber global statt pro-Tab), Movement-Stop-
    Thema auf User-Wunsch offen gelassen.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren (z. B. sobald die
Shake-Kalibrierungsergebnisse feststehen).
