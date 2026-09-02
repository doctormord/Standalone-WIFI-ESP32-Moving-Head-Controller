# NEXT CHAT STARTS HERE

Stand 2026-09-02 abends, nach einer langen Session am Fixture. Branch `future`, Flash 94,1 %.
Auf dem Gerät läuft der committete Stand, per OTA geflasht und über den Build-Stempel verifiziert.

## Wo wir stehen

Die Beat-Uhr ist repariert und **auf echter Hardware gemessen**, nicht geschätzt. Vier
voneinander unabhängige Fehler, alle am DMX-Ausgang belegt:

| Symptom | Ursache | Danach |
|---|---|---|
| Tap wirkt nicht | `tapAnchorMiss` zählte Audioblöcke (31/s) statt Auswertungen → 0,64 s statt 20 s | Anker hält 162 s statt 0 s |
| „läuft auseinander" | Phasenfehler einseitig gemessen — konnte ziehen, nie bremsen | Bündelung 0,85–0,94 über 3 min |
| „jittert extrem" | Phasen-Lock zählte bei frühen Onsets einen Beat, Sprung bis 30 % | Zählung allein im Metronom |
| „springt auf 72" | „±15-%-Band" prüfte gegen den letzten Wert = Schrittbegrenzer | ohne Anker 68..163 → 112..149 |
| Tap wird überschrieben | Band 15 %, Faltungstoleranz 8 % | beide 8 %; Tap 125 hält bei 121–129 |

Die Rate war nie falsch: 0,99× / 1,00× / 0,99× über drei Sync-Teiler.

## Zwei Dinge für den Betrieb

- **Jeder Flash startet neu, dabei gehen FX-Zustand und Tap-Anker verloren.** Danach einmal
  tappen und die Effekte wieder einschalten — sonst misst man den ankerlosen Fall.
- **Der Anker ist das Instrument für Wahrheit.** `tAnchor` gegen `tBPM` auf `/api/state` liefert
  Wahrheit und Messung im selben Moment. Ein Fehltap wird durch einen neuen Tap ersetzt.

## Der Simulator, und was er wert ist

`sim/` kann jetzt synkopierte Kick-Muster (`--pattern hiphop|broken|dnb|half`) und vergleicht
Tempo-Schätzer auf identischem Onset-Strom (`--mode compare`). Ergebnis über 5 Muster × 6 Tempi:
Intervall-Median 31 %, Autokorrelation über alle drei Bänder + Prior 81 %, mit Anker 100 %.

**Aber:** am echten Track erreichte „alle Bänder + Prior" nur 7 % — der Prior bei 120 BPM zieht
dort zur falschen Antwort. Erst mit Anker stimmt es (51 % nur Bass → 91 % alle Bänder). Deshalb
wurde die Autokorrelation **nicht portiert**: ihr einziger Mehrwert wäre Betrieb ohne Tap, und
ohne Tap versagt sie auf diesem Material ebenso. Das vorhandene Ratio-Folding löst den Fall.

Die Lehre daraus ist wichtiger als das Ergebnis: **der Simulator ist notwendig, nicht
hinreichend.** Er findet Bugs und schließt Verfahren aus; er entscheidet nicht, was am echten
Signal gewinnt.

## Was als Nächstes ansteht

Kein akuter Fehler offen. Sinnvolle Kandidaten, in dieser Reihenfolge:

1. **Beobachtungsbetrieb.** Der Stand ist heute in vielen kleinen Schritten entstanden. Ein Set
   fahren und schauen, ob Tempo und Effektphase halten, ist mehr wert als der nächste Umbau.
2. **CPU am Gerät messen.** Drei Detektorbänder gegen bedarfsgesteuerte FFT — netto vermutlich
   günstiger, aber unbelegt. `audUs`/`fftUs`/`loopMax` je einmal mit offenem und geschlossenem
   AUDIO-Tab. Notausgang `/audio_tune?sab=0`.
3. **Preset-Engine-Split**, siehe `backlog.md` → Technische Schulden.

## Regeln, die aus dieser Session kommen

- **Was gemessen wird, muss vorher gesetzt werden.** Zweimal gegen eine Einstellung gemessen, die
  sich zwischendurch geändert hatte. Messskripte setzen ihren Zustand jetzt selbst.
- **Flash verifizieren, immer.** `/api/state` liefert `bld` mit Build-Datum und -Uhrzeit. Ein
  nicht verifizierter Flash hätte hier fast eine Messung fehlinterpretiert.
- **Zwei Endpunkte in einer Poll-Schleife halbieren die Abtastrate.** Ein „34 % Jitter" war reines
  Artefakt — die Ausreißer lagen bei fast exakt Vielfachen eines Zyklus.
- **Wahrheit wird zum Zeitpunkt der Messung erhoben, nicht erinnert.**

    cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp
    ./simbeat --mode compare                       # Schätzer gegeneinander, alle Muster
    ./simbeat --mode compare --pattern hiphop      # nur der synkopierte Fall
    ./simbeat --mode bands --file X.wav            # echte Datei, drei Bänder
    LC_ALL=C setzen, wenn die Ausgabe mit awk geparst wird
