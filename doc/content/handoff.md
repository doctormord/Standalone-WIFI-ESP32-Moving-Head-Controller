# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Gobo-Shake-Kalibrierung ausgewertet: Shake ist 5 diskrete
> Firmware-Stufen, keine kontinuierliche Regelung.** User beobachtete
> beim manuellen DMX-Sweep über Gobo 1s Shake-Zone (CH7=211–215):
> „wackelt links rechts aufsteigend speed gesteppt, 5 stufen, scheint
> ok." Damit ist der letzte offene Rückfrage-Punkt aus den vorigen Runden
> gelöst.
>
> **Direkter Folge-Vorschlag geprüft und verworfen:** User schlug vor,
> einen eigenen, stufenlosen Shake über CH7/CH8s Rotation-Zonen zu bauen
> (CW/CCW-Alternation statt der 5 festen Fixture-Stufen). Vor dem Bauen
> live getestet (Gobo 1 gewählt, dann langsame Rotation) — Ergebnis: die
> Rotation-Zone dreht das **ganze Rad kontinuierlich durch verschiedene
> Gobo-Motive**, sie wackelt nicht das gewählte einzelne Gobo an Ort und
> Stelle. Ein CW/CCW-Shake darüber sähe wie wildes Vor-und-Zurück-Spinnen
> aus, nicht wie sauberes Wackeln. **Nicht weiterverfolgt**, keine
> Code-Änderung — in `mapping_sheds_160w_3in1_gobo.md` festgehalten. Der
> bereits gebaute 5-Stufen-Shake bleibt der richtige Weg für dieses Fixture.
>
> **Was das erklärt:** Das alte Software-Modell ließ den DMX-Wert
> kontinuierlich innerhalb der Zone oszillieren, um „Speed"/„Range" zu
> simulieren — dadurch sprang die Fixture ständig zwischen ihren 5
> eingebauten Geschwindigkeiten hin und her, statt eine zu halten. Das
> erklärt rückwirkend exakt: „Speed scheint invers" (chaotisches Springen
> durch alle Stufen unabhängig von der Slider-Richtung) und „Range
> beeinflusst Speed" (Range bestimmte, wie weit die Oszillation in
> schnellere Stufen reichte).
>
> **Neu gebaut:** `scratchSpeed` wählt jetzt direkt eine von 5
> Firmware-Stufen (1=langsamst, 5=schnellst), `runStep()` hält einen
> einzigen festen DMX-Wert (`shakeBase + (gobo-1)×5 + (stufe-1)`), keine
> Oszillation mehr. `scratchRange` komplett entfernt (Backend-Feld,
> API-Parameter, JSON-Export, Frontend-Regler) — kein reales Gegenstück
> mehr dafür. „Shake speed"-Regler zeigt jetzt direkt 1–5.
>
> **Live per `curl` verifiziert:** Stufe 1 → CH7 exakt 211, Stufe 5 → CH7
> exakt 215. `mapping_sheds_160w_3in1_gobo.md` mit dem bestätigten Befund
> aktualisiert, offener Punkt dort als gelöst markiert.
>
> **Was noch offen ist (aus vorigen Runden, unverändert):**
> - 15ms-Commit-Delay-Fix für Kurz-Taps — visuell/physisch unverifiziert.
> - Gobo-Chaser-Stop-Fix (global gemachte `wasActive`-Flags) —
>   code-seitig live per curl bestätigt, aber noch nicht vom User selbst
>   im Browser/an der Lampe nachgetestet.
> - Followspot-eigenes Joystick-Profil — als mögliches Feature im Raum,
>   nicht bestätigt.
> - Movement-Stop-mit-Momentum — auf User-Wunsch bewusst offen.
> - Layout-Bug bei „MAX"-Reglern in Movement FX — ungeklärt.
> - Neue Shake-Stufen-Logik selbst — code-seitig live verifiziert (curl),
>   aber noch nicht vom User als „fühlt sich nach 5 sauberen,
>   kontrollierbaren Stufen an" bestätigt.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> ist kaputt (fehlender `firmware/`-Ordner).
>
> **Vier Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen, der Layout-Bug bei den „MAX"-Reglern.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Am Fixture/im Browser nachtesten: neue Shake-Stufen (1–5, fühlt sich
>    das gut steuerbar an?), Gobo-Chaser-Stop-Fix, 15ms-Commit-Delay.
> 2. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 3. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 4. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 5. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Fünfzehn Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer abgeschlossenen
interaktiven Hardware-Kalibrierung für den Gobo-Shake. Zielhardware:
**ESP32-C3 Supermini** (Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam
280"). Repository ist sowohl lokal als auch auf GitHub (`future`-Branch)
git-versioniert und läuft auf echter Hardware. Der Gobo-Shake ist damit
der erste Fall in dieser Session, bei dem eine mehrfach gescheiterte
Software-Guess-Kette durch eine gezielte, gemeinsame Hardware-
Kalibrierung (statt eines weiteren Blindschusses) sauber aufgelöst wurde
— ein wiederverwendbares Muster für künftige undokumentierte
Fixture-Verhaltensweisen.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–14: siehe vorige Handoff-Snapshots / `history.md`.
15. Neunte Live-Test-Runde: Gobo-Shake-Kalibrierungsergebnis ausgewertet
    (5 diskrete Firmware-Stufen statt kontinuierlicher Bereich), Shake-
    Mechanismus komplett neu gebaut (Stufen-Wahl statt Oszillation),
    `scratchRange` als überholtes Konzept vollständig entfernt, live per
    `curl` verifiziert, Fixture-Referenzdatei mit dem bestätigten Befund
    aktualisiert.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren (die
Shake-Kalibrierung ist dort jetzt als abgeschlossen dokumentiert).
