# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Rotation-Pulse-Shake für CH7 (statisches Gobo) live gebaut und
> verifiziert — ein echter, kontinuierlich einstellbarer Shake statt der
> 5 fixen Fixture-Stufen.** User hatte die vorige „nicht weiterverfolgt"-
> Einschätzung berechtigt korrigiert: der erste Test prüfte eine
> gehaltene Dauerrotation (falsche Technik), nicht kurze, abwechselnde
> Pulse zwischen CW-/CCW-Zone mit Index-Re-Anchor dazwischen (die
> tatsächlich vorgeschlagene Technik). Zweiter, korrigierter Live-Test
> bestätigte: „gobo scheibe pendelt links rechts langsam" — funktioniert.
>
> **Gebaut:** `runStep()` bekommt einen `rotationPulse`-Modus (nur für
> `sgobFX`/CH7): Vier-Phasen-Zyklus CW-Puls → Index-Re-Anchor → CCW-Puls
> → Index-Re-Anchor, Timing über `scratchSpeed` (jetzt wieder Hz-Float,
> 0,2–10), Intensität über `scratchRange` (0–100%, zurückgeholt). CH8
> bleibt beim fixture-nativen 5-Stufen-Shake (keine Gegenrichtung auf CH8
> selbst, CH9-Alternative würde mit Rotation FX kollidieren). Frontend
> zeigt automatisch die passenden Regler je nach Wheel.
>
> **Live verifiziert** (echte Firmware, kein externer curl-Loop mehr
> nötig): Start → „es wackelt und pendelt... nicht 100% smooth aber
> geht" (User-Zitat). Stop mit `mv=60` → CH7 bleibt sauber bei Gobo 6.
>
> **Nebenbefund:** Während des Tests ein unerwarteter Sync-Konflikt
> (`sgA` sprang ohne Zutun auf 0) — höchstwahrscheinlich ein parallel
> offener Browser-Tab, der mit den curl-Tests kollidierte (bereits
> bekanntes Mehrfach-Client-Sync-Problem, kein neuer Bug). Nach Schließen
> aller Tabs lief der Test sauber durch.
>
> **Nächster möglicher Schritt (angefragt, bewusst zurückgestellt):**
> User fragte nach Speed-/Intensitäts-Rampen über die Zeit („wa-wa-wosh",
> sanft anlaufen, kurz aufdrehen) — technisch gut machbar mit derselben
> Modulator-Technik wie bei Dimmer-/Rotation-FX, aber bewusst nicht in
> derselben Runde gebaut, um die gerade verifizierte Basis erst zu
> sichern.
>
> **Was noch NICHT vom User selbst im Browser bestätigt ist:**
> - Der neue Rotation-Pulse-Shake über die echte UI (bisher nur curl).
> - Ob die leichte „nicht 100% smooth"-Unrundheit störend ist oder so
>   bleiben kann.
> - 15ms-Commit-Delay-Fix für Kurz-Taps.
> - Gobo-Chaser-Stop-Fix (global gemachte `wasActive`-Flags).
> - Followspot-eigenes Joystick-Profil — als mögliches Feature im Raum.
> - Movement-Stop-mit-Momentum — auf User-Wunsch bewusst offen.
> - Layout-Bug bei „MAX"-Reglern in Movement FX — ungeklärt.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> ist kaputt (fehlender `firmware/`-Ordner). Mehrfach-Client-Sync
> (`backlog.md` → „Bekannte kleine Issues") bleibt ein bekanntes,
> unadressiertes Risiko — heute erneut live beobachtet.
>
> **Vier Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen (relevant für den Mehrfach-Client-Konflikt
> — eine echte Lösung bräuchte WebSockets oder zumindest einen einzigen
> Polling-Mechanismus mit Konflikterkennung), der Layout-Bug bei den
> „MAX"-Reglern.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Rotation-Pulse-Shake über die echte Browser-UI nachtesten (bisher
>    nur curl) — Speed/Range-Regler im Static-Gobo-Chaser-Panel.
> 2. Falls gewünscht: Speed-/Intensitäts-Rampen über die Zeit für den
>    Shake bauen (Modulator-Technik, wie besprochen).
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Sechzehn Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake — von „Idee
verworfen" über „User korrigiert die Testtechnik" zu „funktioniert live
bestätigt" in derselben Session. Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Bemerkenswert: die erste Ablehnung einer User-Idee
(„Rotation-Zone als Shake") war korrekt bezüglich des GETESTETEN
Verhaltens, aber voreilig bezüglich der IDEE selbst — der User hatte
recht, nur der Test prüfte die falsche Technik. Lehre: bei einer
gegensätzlichen User-Reaktion auf eine Ablehnung lohnt sich eine zweite,
genauere Nachfrage nach der exakt gemeinten Technik, bevor man bei der
ersten Widerlegung bleibt.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–15: siehe vorige Handoff-Snapshots / `history.md`.
16. Zehnte Live-Test-Runde: Rotation-Pulse-Shake für CH7 gebaut (echte,
    kontinuierliche Speed/Range-Regelung über CW/CCW-Rotationspulse +
    Index-Re-Anchor), live per curl gegen die echte Firmware verifiziert
    (Start, Pendel-Bewegung, sauberer Stop), Mehrfach-Client-Sync-
    Konflikt live beobachtet (bekanntes, nicht neues Problem),
    Zeit-Rampen für Shake als nächster möglicher Schritt identifiziert
    und bewusst zurückgestellt.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
