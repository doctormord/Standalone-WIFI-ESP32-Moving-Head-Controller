# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-18, Fortsetzung)
> **Start/Stop-Race und ein echter Backend-Kanal-Clobber für ALLE FX-Typen
> geprüft und gefixt**, direkt im Anschluss an den sg/rg-Stop-Race-Fix
> (siehe vorheriger Banner-Stand unten). User meldete dasselbe Symptom
> ("springt zurück auf run" / "Änderungen werden nicht angenommen") auch
> für Dimmer-FX und Color-FX, plus eine Bandbreiten-Beobachtung (CH1/CH6-
> Regler "wackeln" während die FX läuft), und bat explizit um Prüfung für
> **alle** FX.
>
> **Gefunden (zwei getrennte Bugs, keine Vermutung — jede Komponente
> einzeln gelesen):**
> 1. **Frontend-Race (alle FX):** `fx`/`dimFx`/`grFx`/`prFx`/`colFx`/
>    `chaser` nutzten beim Stop noch die alte debounced `tFetch`-Queue statt
>    des `tFetchImmediate`-Bypasses, der bis dahin nur für sg/rg existierte.
> 2. **Echter Backend-Bug (nicht nur Race), nur `grFx`/`prFx`/`dimFx`:**
>    `updateEngines()` setzte CH9/CH11 beim Stop hart auf **0** statt auf
>    den manuellen Programmer-Wert; `dimFX` liess `dimSmoothTarget` auf dem
>    letzten LFO-Wert stehen statt auf dem manuellen Wert. `moveFX`/`colFX`/
>    `chaser` hatten dieses Clobber-Problem nicht (Movement restauriert aus
>    `centerPan/Tilt16`, `colFX` über den bestehenden `wasActive`-Fallback).
> 3. **Bandbreiten-/"Regler springt"-Beobachtung bestätigt als eigener Bug:**
>    Der Poll-Merge in `data/index.html` überschrieb `dimmer`/`goboRot`/
>    `prismRot`/`colorBase` bei jedem Poll (alle 2 s) unconditional aus dem
>    Live-DMX-Wert, auch während die zugehörige FX lief.
>
> **Gebaut:**
> - `Moving_Head_Horizon.ino`: `gRotFX`/`pRotFX`-Stop-Zweige entfernen das
>   harte `dmxData[9|11] = 0`.
> - `WebAPI.h`: `/modfx` akzeptiert jetzt `mv=` (analog `/sgobfx`/
>   `/rgobfx`) — schreibt bei Stop für `gr`/`pr` direkt `dmxData[9|11]`,
>   für `dim` stattdessen `dimSmoothTarget`.
> - `data/index.html`: alle sechs verbliebenen Stop-Übergänge nutzen jetzt
>   `tFetchImmediate`; `grFx`/`dimFx`/`prFx` senden zusätzlich `mv=` mit;
>   der Poll-Merge überschreibt die vier betroffenen Felder nur noch, wenn
>   die zugehörige FX nicht läuft.
>
> **Live per curl verifiziert:** CH1 (Dimmer), CH9 (Gobo-Rotation), CH11
> (Prism-Rotation) — Start → Stop mit `mv=<Wert>` → Kanal landet sofort auf
> `mv` und bleibt 2 s später stabil dort, kein Rückfall auf 0 oder den
> letzten FX-Wert. `pio run` + `pio run -t buildfs` beide `[SUCCESS]`,
> geflasht (`upload` + `uploadfs`), Gerät danach unter `192.168.8.113`
> erreichbar, `/kill_fx` setzt alle FX-Flags sauber auf 0.
>
> **Noch NICHT vom User im Browser/an der echten Hardware bestätigt:**
> - Greift Stop jetzt bei Movement-FX, Dimmer-FX, Gobo-Rotation-FX,
>   Prism-Rotation-FX, Color-Chaser und Scene-Chaser zuverlässig beim
>   ersten Versuch (nicht nur sg/rg, die bereits vorher bestätigt waren)?
> - Wackeln die CH1/CH6/CH9/CH11-Regler jetzt nicht mehr sichtbar, während
>   die jeweilige FX läuft?
> - **Aus der vorigen Runde weiterhin offen:** Fix 1 (Shake-Amplitude bei
>   niedriger Speed) und Fix 2 (Settle-Fenster, kein Choppy-Effekt beim
>   Gobo-Wechsel) — code-/curl-verifiziert, aber noch nicht am echten
>   Gerät vom User optisch bestätigt.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Die oben genannten Fixes am echten Gerät/im Browser nachtesten.
> 2. Falls alles sich bestätigt: stabiler Stand für die zuvor besprochenen
>    Zeit-Rampen für den Shake ("wa-wa-wosh"), falls gewünscht.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Achtzehn Review-/Test-Runden durch (Details siehe `history.md`), inklusive
eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake — von „Idee
verworfen" über „User korrigiert die Testtechnik" zu „funktioniert live
bestätigt" zu „nach echtem UI-Test nachgeschärft" zu „Start/Stop-Race und
Kanal-Clobber für alle FX-Typen geprüft und gefixt" in derselben
fortlaufenden Session. Zielhardware: **ESP32-C3 Supermini** (Fixture:
SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl lokal als
auch auf GitHub (`future`-Branch) git-versioniert und läuft auf echter
Hardware, aktuell erreichbar unter der festen LAN-IP `192.168.8.113`
(mDNS `movinghead.local` bleibt bekannt-flakey).

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 18) gemacht wurde

1–17: siehe vorige Handoff-Snapshots / `history.md`.
18. Nach dem sg/rg-Stop-Race-Fix bat der User um dieselbe Prüfung für alle
    FX-Typen. Gefunden: derselbe Frontend-Race bei `fx`/`dimFx`/`grFx`/
    `prFx`/`colFx`/`chaser`, plus ein echter (kein Race) Backend-Bug bei
    `grFx`/`prFx` (Kanal-Stomp auf 0 beim Stop) und `dimFx`
    (`dimSmoothTarget` bleibt auf letztem LFO-Wert stehen), plus ein
    Poll-Merge-Bug (Dimmer/Gobo-Rot/Prism-Rot/Color-Regler wackeln
    sichtbar mit der laufenden FX mit). Alle gefixt, kompiliert, geflasht,
    Backend-Seite live per curl bestätigt. Browser-/Hardware-seitige
    Bestätigung steht noch aus.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 18).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
