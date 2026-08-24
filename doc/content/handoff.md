# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-24)
> **Gerät ist konfiguriert, erreichbar (USB + WLAN) und läuft mit dem
> dokumentierten, unveränderten Baseline-Code. Kein offener Blocker.**
>
> **Was in dieser Session gemacht wurde (Details in `history.md` 2026-08-24
> und `backlog.md`/`mapping_sheds_160w_3in1_gobo.md` → CH3/CH4):**
>
> Der in der vorigen Session gebaute Blend-Fix für die Movement-„Acht statt
> Kreis"-Verzerrung am Zenit wurde geflasht und live getestet — an einem
> exakt auf dem Zenit zentrierten Kreis (`cp=32767, ct=32767`, der
> schwierigste Fall, identisch zum User-eigenen „360°-Pan-bei-Tilt=127"-
> Vorschlag). **Ergebnis: funktioniert nicht.** Der Tilt-Ausgang sprang
> jede halbe Umdrehung zwischen den beiden Polseiten hin und her (per
> `curl`-Telemetrie UND unabhängig vom User live am Fixture bestätigt —
> „durchgestrichener Kreis"/erneute Acht). Root Cause identifiziert: die
> Formel entschied „welche Seite des Pols" pro Sample anhand des
> Vorzeichens des *naiven linearen* Tilt-Ergebnisses — genau die Größe,
> die bei einem Kreis um den Zenit zweimal pro Umdrehung das Vorzeichen
> wechselt, was den Korrektur-Zweig zum Hin-und-Herspringen brachte statt
> ihn auf einer Seite zu halten.
>
> Auf explizitem User-Wunsch wurde ein möglicher Reparatur-Ansatz (feste
> Polseite statt per-Sample-Umschaltung) **nicht mehr live ausprobiert**
> — Code wurde komplett per `git checkout` auf den letzten committeten,
> dokumentierten Stand (`07c5d38`, „kein Software-Fix") zurückgesetzt,
> neu kompiliert und erneut geflasht. **Kein Software-Fix für die
> Zenit-Acht mehr aktiv.**
>
> Die Root-Cause-Recherche selbst (branchenweite Gimbal-Pol-Singularität,
> kein Defekt dieser Einheit — siehe mehrere unabhängige Lighting-Foren-
> Quellen in `mapping_sheds_160w_3in1_gobo.md`) bleibt weiterhin gültig
> und dokumentiert; nur der darauf aufbauende zweite Fix-Versuch wurde
> zurückgenommen.
>
> User bestätigte außerdem, dass sich das Fixture insgesamt spürbar
> geschmeidiger bewegt als in Erinnerung — das ist real und geht auf die
> deutlich früher gefixten Multi-Beat-BPM-Sync-Jitter- und
> `modSp`-Einheiten-Bugs zurück (Commit `92ab41f`), nicht auf irgendetwas
> aus dieser Session.
>
> ---
>
> **Kein offener Punkt aus dieser Session.** Falls die Zenit-Acht erneut
> angegangen werden soll, Ausgangspunkt ist der in
> `mapping_sheds_160w_3in1_gobo.md` → CH3/CH4 und `backlog.md` dokumentierte
> Formel-Bug (Vorzeichen-Instabilität der „welche Polseite"-Entscheidung)
> — nicht bei null anfangen. Ansonsten frei (keine feste Reihenfolge, aus
> vorigen Sessions übernommen):
> 1. Mic-Sensitivity subjektiv mit echter Musik über den AUDIO-Tab
>    gegenprüfen.
> 2. Mic-BPM-Oktave-Korrektur noch nicht mit echtem Audio verifiziert.
> 3. Zeit-Rampen für den Shake ("wa-wa-wosh"), falls weiterhin gewünscht.
> 4. Separates Followspot-Joystick-Profil?
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Übrige Tech-Debt-Punkte in `backlog.md` (Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen, etc.)

## Aktueller Status

Repository ist sowohl lokal als auch auf GitHub (`future`-Branch)
git-versioniert. Zielhardware: **ESP32-C3 Supermini** (Fixture: SHEHDS 160W
3in1 GOBO / „Pro Beam 280", Pan 540°/Tilt 270°). Der Movement-FX-„Acht statt
Kreis"-Effekt am Zenit-Winkel ist **kein Defekt dieser Einheit** (branchenweit
bekanntes Gimbal-Pol-Verhalten), aber **aktuell kein Software-Fix aktiv** —
ein zweiter Fix-Versuch wurde live getestet, als fehlerhaft erkannt und
komplett zurückgenommen (siehe oben). Gerät läuft mit dem dokumentierten
Baseline-Code (Commit `07c5d38`), ist über `movinghead.local` erreichbar und
im normalen Betrieb.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
