# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Eigene Regression aus der letzten Joystick-Curve-Runde gefunden und
> gefixt, plus einen echten Bug, den der User selbst diagnostiziert hat.**
> Direkt im Anschluss an die Fixture-Datenblatt-Runde kam noch am selben
> Tag konkretes Live-Test-Feedback: kurzes Antippen einer Pfeiltaste löste
> einen deutlichen Bewegungs-Ausschlag aus und lief nach dem Loslassen
> sichtbar weiter; Curve=0/Momentum=0 sollte sofort volle Geschwindigkeit
> geben, hatte aber trotzdem eine Rampe; „Stop Gobo Rot" setzte CH9 nicht
> zuverlässig auf 0.
>
> **Gefixt (Details in `history.md`, 2026-08-17 dritte Fortsetzung):**
> 1. **Eigene Regression aus der vorigen Runde:** `accelMul` sprang beim
>    Loslassen abrupt auf `1.0`, während `joySmoothX` (unabhängig davon)
>    schon nahe am Zielwert war — das Produkt ergab exakt im
>    Loslass-Moment einen kurzen Vollgas-Ausschlag statt eines weichen
>    Ausklingens. Jetzt friert `accelMul` beim Loslassen auf seinem
>    letzten Wert ein statt zu springen.
> 2. **Curve steuerte bisher nur die Form einer fest verdrahteten
>    2-Sekunden-Rampe, nicht deren Dauer** — Curve=Minimum hatte
>    trotzdem immer eine 2s-Rampe. Jetzt ist Curve direkt die Rampendauer
>    in Sekunden (linear), `Curve≈0` bedeutet sofortige Vollgeschwindigkeit.
>    Regler-Bereich im Frontend auf `0–5s` erweitert, Backend-Clamp
>    entsprechend gelockert.
> 3. **Stop-Kommando konnte in der Debounce-Queue hängen bleiben**
>    (bis zu ~80ms Verzögerung) — neue `sendJoy()`-Hilfsfunktion umgeht
>    die Queue für Stop-Befehle komplett, für Tastatur *und* Maus/Touch.
> 4. **„Stop Gobo Rot" setzte CH9 nicht zuverlässig auf 0 — User hat den
>    echten Mechanismus selbst gefunden.** Die Programmer-Tab-Slider für
>    Kanäle mit laufendem FX/Chaser übernahmen per Poll ständig den
>    Live-Wert und schrieben ihn per `/set_all` zurück — direkt nach dem
>    Stoppen konnte so ein veralteter Live-Snapshot den frisch von der
>    FX-Engine gesetzten Stop-Wert (CH9→0) wieder überschreiben. Jetzt
>    werden alle 6 FX-/Chaser-gekoppelten Kanäle (Dimmer, Color, beide
>    Gobo-Räder, Gobo-Index, Prisma-Rotation) komplett von der
>    Outbound-Sync ausgenommen, solange ihr FX läuft — behebt auch das
>    vom User zu Recht vermutete Bandbreiten-Problem.
>
> **Was noch NICHT visuell/physisch verifiziert ist:**
> - Kurzer Tap bleibt jetzt klein, kein Ausschlag/Nachlaufen mehr.
> - Curve=0 gibt wirklich sofortige Vollgeschwindigkeit.
> - „Stop Gobo Rot" hält CH9 jetzt zuverlässig auf 0.
> - Alles aus den vorigen Runden am selben Tag (Motor-Stop, Modulator-
>   Speed, Poll-Race, Mode/Curve, Jog-Snapback, drei einheitliche
>   Joystick-Tabs, Fixture-Datenblatt/Shake-Fix) — siehe die vorigen
>   Handoff-Runden in `history.md`, bisher nur code-seitig verifiziert.
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
> 1. Am Fixture/im Browser nachtesten — insbesondere Joystick-Feel (Tap,
>    Curve=0, Loslassen) und ob „Stop Gobo Rot" CH9 jetzt wirklich auf 0
>    hält. Wichtigster nächster Schritt, den nur der User erledigen kann.
> 2. Gobo-Chaser-Shake (jetzt mit korrekten Zonen aus der vorigen Runde)
>    und die Gobo-6-Frage (vermutlich Hardware) am Fixture prüfen.
> 3. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 4. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Zehn Review-/Test-Runden durch (Details siehe `history.md`), inklusive
eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Bemerkenswert an dieser Runde: eine der beiden
Kern-Ursachen war eine **eigene Regression** aus der unmittelbar vorigen
Session-Runde (Curve-Snap-Bug) — ein Hinweis, dass schnelle iterative
Fixes auf Basis von Nutzer-Feedback (ohne Zwischenschritt eigener
Live-Verifikation) selbst neue, subtile Bugs einführen können; entdeckt
nur, weil der User konsequent weiter live am Gerät testet.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–9: siehe vorige Handoff-Snapshots / `history.md` (React/Babel lokal,
Stage-Map-Fixes, mehrere Review-Runden, erster+zweiter Hardware-Test,
Joystick-Controls vereinheitlicht, Fixture-Datenblatt ausgewertet).
10. Vierte Live-Test-Runde: eigene Regression aus der Curve-v2-Änderung
    gefunden (Release-Snap-Bug) und gefixt; Curve-Semantik auf
    „Curve = Rampendauer in Sekunden, 0 = sofort" umgebaut; Stop-Befehl
    bekommt einen Fastlane-Pfad an der Debounce-Queue vorbei; User hat
    selbst den Mechanismus hinter „CH9 stoppt nicht" gefunden (Poll/
    `track()`-Race bei FX-gekoppelten Slidern) — alle sechs betroffenen
    Kanäle jetzt während laufender FX von der Outbound-Sync ausgenommen.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
