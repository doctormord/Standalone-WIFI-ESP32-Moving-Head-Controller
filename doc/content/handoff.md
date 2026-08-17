# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Eigener Bug im Gobo-Chaser-Stop-Fix gefunden und gefixt, diesmal live
> per `curl` verifiziert (nicht nur „Server läuft").** User schickte einen
> Screenshot: Gobo-Chaser (statisch + rotierend) stoppen ging nicht auf
> die links im Programmer-Tab eingestellten manuellen Werte zurück (beide
> „White (Open)"), obwohl genau das in einer vorigen Runde gefixt werden
> sollte.
>
> **Root Cause:** Zwei eigene, für sich genommen korrekte Fixes aus zwei
> verschiedenen Runden bekämpften sich gegenseitig. Der `mv`-basierte
> atomare Stop-Restore (`/sgobfx`/`/rgobfx`) schrieb den manuellen Wert
> korrekt — aber `runStep()`s älterer, unabhängiger Stop-Reset
> (`wasActive`-Flankenerkennung) lief im allernächsten
> `updateEngines()`-Durchlauf ebenfalls an und überschrieb ihn sofort
> wieder mit der letzten Chaser-Position. Die beiden Mechanismen kannten
> einander nicht, weil `sgWasActive`/`rgWasActive`/`colWasActive` bisher
> `static`-Lokalvariablen in `updateEngines()` waren — für `WebAPI.h`
> unerreichbar.
>
> **Fix:** Die drei Flags sind jetzt echte globale Variablen (deklariert
> vor `#include "WebAPI.h"`, wie im Projekt für genau diesen Zweck
> üblich). `/sgobfx`/`/rgobfx` setzen sie explizit auf `false`, sobald sie
> den `mv`-Restore selbst übernommen haben — `runStep()`s Fallback greift
> dadurch nur noch bei Stop-Pfaden ohne bekannten manuellen Wert (z. B.
> `/kill_fx`).
>
> **Diesmal per `curl` live nachgestellt statt nur „online" geprüft:**
> Chaser gestartet, CH7 bei 70 (gültiger Zwischenwert), dann mit `mv=0`
> gestoppt — CH7 sofort **und** eine Sekunde später weiterhin `0`. Root
> Cause bestätigt behoben.
>
> **Was noch offen ist (aus der vorigen Runde, unverändert):**
> - Gobo-Shake-Kalibrierung (CH7=211–215 durchgefahren) — **Auswertung
>   durch den User steht noch aus.**
> - 15ms-Commit-Delay-Fix für Kurz-Taps — visuell/physisch unverifiziert.
> - Followspot-eigenes Joystick-Profil — als mögliches Feature im Raum,
>   nicht bestätigt.
> - Movement-Stop-mit-Momentum — auf User-Wunsch bewusst offen.
> - Layout-Bug bei „MAX"-Reglern in Movement FX — ungeklärt.
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
> 1. **User berichtet die Ergebnisse der Shake-Kalibrierung** (CH7=
>    211–215) — wichtigster offener Punkt aus der vorigen Runde.
> 2. Am Fixture/im Browser den Gobo-Chaser-Stop-Fix aus dieser Runde und
>    den 15ms-Commit-Delay-Fix nachtesten.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Vierzehn Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer laufenden interaktiven
Hardware-Kalibrierung für den Gobo-Shake. Zielhardware: **ESP32-C3
Supermini** (Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository
ist sowohl lokal als auch auf GitHub (`future`-Branch) git-versioniert
und läuft auf echter Hardware. Bemerkenswert an dieser Runde: der Bug war
das Ergebnis von zwei für sich genommen korrekten Fixes aus
unterschiedlichen Runden, die denselben Kanal beschreiben wollten, ohne
voneinander zu wissen — ein Hinweis, dass bei mehreren aufeinander
aufbauenden Fixes am selben Subsystem (hier: Wheel-Chaser-Stop-Verhalten)
der GESAMTE Interaktionspfad nochmal end-to-end zu prüfen ist, nicht nur
der zuletzt geänderte Ausschnitt. Diesmal auch erstmals mit einem
Live-`curl`-Test verifiziert, der das *gemeldete* Fehlverhalten aktiv
nachstellt, statt sich nur auf „Gerät antwortet wieder" zu verlassen.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–13: siehe vorige Handoff-Snapshots / `history.md`.
14. Achte Live-Test-Runde: per Screenshot belegten Gobo-Chaser-Stop-Bug
    nachvollzogen (zwei eigene Fixes bekämpften sich gegenseitig, da die
    `wasActive`-Flags nicht cross-file sichtbar waren), globale Flags
    eingeführt, Fix live per `curl`-Nachstellung (nicht nur Server-Ping)
    verifiziert.

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
