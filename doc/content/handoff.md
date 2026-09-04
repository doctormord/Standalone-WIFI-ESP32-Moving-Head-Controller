# NEXT CHAT STARTS HERE

Stand 2026-09-03 abends. Branch `future`, **Flash 78,4 %** (war 94,0 %). Nicht committet.
Auf dem Gerät läuft dieser Stand: `bld Sep 3 2026 13:05:44`, per OTA geflasht und über den
Build-Stempel verifiziert.

## Was gilt

Die Grundregeln aus der Messsession vom 2026-09-02, alle am Fixture belegt und unverändert:

- **Der Tap ist die Wahrheit.** Er setzt einen Anker, auf den der Schätzer die gemessenen
  Abstände faltet (Rungs 1/2, 2/3, 3/4, 1, 4/3, 3/2, 2 — Toleranz 8 %).
- **Stille hält das Tempo.** Ohne eintreffende Beats wird gar nichts veröffentlicht; der letzte
  Wert steht unbegrenzt. Mit komplett gestoppter Musik verifiziert.
- **Auto-Gain regelt nur hoch, während Kicks kommen** — mit zwei Ausnahmen, siehe unten.
- **Vier-Viertel läuft ohne Tap.** Am Gerät auf House 122 gemessen: 2,00 Onsets/s bei einer
  Beatrate von 2,03, **86 % der Abstände exakt ein Beat**, Tempo ohne jede Eingabe auf 0 % genau.

## DER NÄCHSTE PUNKT: greift der Tap-Anker auf D&B überhaupt?

Am 2026-09-03 gemessen, ausgelieferter Detektor, Anker per Tap auf 172 gesetzt, Track echte 172:
**GUI-Median 122, nur 3 % der Messwerte innerhalb von 5 %.** Die Regel „mit Tap steht D&B im
±8-%-Band" stammt vom 2026-09-02 und wird durch diese Messung widerlegt.

Zu klären: fällt der Anker (laufen `tapAnchorMiss` / `tapAnchorRawMiss` über?), greift er gar
nicht, oder faltet er auf eine falsche Rasterstufe? **Der Weg führt über `bOn`** — die Abstände
histogrammieren und gegen die Rungs prüfen. Nicht über die Anzeige; die verrät das Ergebnis,
nicht die Ursache.

    ./scripts/ab_detector.sh 172 <ip> 40      # ohne und mit Anker, mit Abstands-Histogramm

## Neu: Burst — vier Zahlen je beat-getriggertem Effekt

Dimmer, Gobo-Rotation, Prisma und Movement haben jetzt statt eines Teilers vier Werte:

    Sync      wann die Figur von vorne beginnt   (aeusseres Raster)
    Count     wie viele Impulse
    Length    wie lange ein Impuls dauert        (der primaere Wert)
    Spacing   wie weit sie auseinander starten   (>= Length, Standard "back to back")

`Length` ist der Unterschied zwischen **Blitz und weich**, und `Spacing` setzt die Lücke
dazwischen. Length ist bewusst der primäre Wert: bei Count 1 gibt es nichts zu beabstanden, und
in der ersten Fassung verstellte deshalb ausgerechnet „Spacing" die Impulslänge.

    4x · Length 1/2 · Spacing 1 Beat · Sync 32   ####....####....####....####....  dann 28 Beat Pause
    4x · Length 1   · back to back   · Sync 32   ################................  ein Block

Unter den Feldern steht in Worten, was dabei herauskommt — grau wenn es aufgeht, gelb mit
Warnung wenn Count × Spacing das Sync-Raster überschreitet (dann läuft es durch, weil die
Firmware keine halbe Kurve abschneiden kann).

Standard ist Count 1, `Length · fills slot`, `Sync · free run` — bitgleich zum Verhalten vor
dieser Änderung, gespeicherte Szenen ändern sich also nicht.

**Beim Erweitern von `SceneData` aufpassen:** der Block wird roh gespeichert und beim Laden auf
exakte Größe geprüft. Es gibt inzwischen drei Layouts (`SceneDataV1`, `SceneDataV2`, aktuell),
alle über ihre Größe erkannt. Neue Felder gehören ans **Ende**, die vorige Fassung muss als
Struct erhalten bleiben, und der fehlende Schwanz braucht die Werte, die das alte Verhalten
reproduzieren — sonst fallen gespeicherte Szenen stillschweigend durch.

**Und vor jedem neuen Parameter: die Checkliste in `CLAUDE.md` lesen.** Elf Stellen, jede
Auslassung scheitert lautlos. Die Liste hat sich beim zweiten Parameter desselben Tages bewährt.

## Tappen: was man wissen sollte

Der Tap benutzt eine **Kette**: solange du weitertappst, wächst sie und der Wert nähert sich an;
eine Pause von über 2 s startet eine neue. Der Zähler neben der BPM im TAP-Knopf zeigt, wie viele
Taps hinter dem aktuellen Wert stehen.

**Drei Taps sind der schlechteste Fall** — zwei Intervalle, keine Redundanz. Bei 172 BPM sind
0,49 BPM pro Millisekunde Tippfehler fällig, also ±9 BPM bei üblicher Handgenauigkeit. Ab etwa
zehn Taps steht der Wert still, bei 24 liegt die Streuung unter einem halben BPM.

Merke: **je schneller das Tempo, desto empfindlicher der Tap.** Dieselbe Hand ergibt bei 120 BPM
die halbe Streuung wie bei 172.

## Auto-Gain: wann es greift

- Hochregeln nur, während Beats eintreffen (2-s-Fenster) — gegen das Aufreißen in Breaks.
- **Ausnahme 1:** ein Tap entsperrt es für 15 s. Ohne das verklemmt sich die Regel.
- **Ausnahme 2 (`AG_STARVE_SHIFT = 1`):** unterhalb Stufe 2 entscheidet die Pegelprüfung allein.
  Das Beat-Gate setzt voraus, dass Beats erkennbar sind — am unteren Ende gilt das nicht, und
  eine laute Passage konnte die Verstärkung dort dauerhaft stranden lassen.
- Runter regelt es immer.
- **Der gespeicherte Wert wird nur bei MANUELLER Verstärkung geladen.** Bei Auto startet das
  Gerät vom Default (Stufe 3). Vorher stand in NVS eine 0, die nie überschrieben wurde — jeder
  Neustart landete dort, ohne Erkennung und ohne Weg heraus.

Signatur, falls es je wieder klemmt: `ig` klein, `pk` winzig, `xb` dauerhaft 0 auf
`/api/audio_debug`. Sofortmaßnahme: tappen, oder `?ig=3` von Hand.

## CPU: gemessen, Frage geschlossen

    updateEngines   252 us -> 4,3 %      Audio-Block 3036 us -> 9,5 % (davon FFT 1211 us = 3,8 %)
    zusammen ~14 % eines Kerns, Schleifenrate 172/s mit laufender FFT, 331/s ohne

**Der C3 ist nicht CPU-begrenzt.** Damit ist auch der Chipwechsel erledigt: eine FPU haben nur
ESP32, S3, H4 und P4 — **C5, C6 und S2 nicht**, und der P4 hat kein WLAN. Details und die beiden
falsch messenden Telemetriefelder, die dabei gefunden wurden, stehen in `handover.md`.

    ./scripts/measure_cpu.sh <ip> 30

## Flashen

Normalbetrieb ist OTA (`espota.py -i <ip> -f .pio/build/supermini/firmware.bin`, mit `-s` für
das Dateisystem). **Ausnahme: die Partitionstabelle.** Sie liegt bei `0x8000`, OTA schreibt nur
App-Slots. Ein Gerät, das noch die alte Tabelle hat, braucht einmal USB **mit Dateisystem** —
der FS-Offset wandert von `0x290000` nach `0x310000`. `nvs` bleibt in jedem Fall unangetastet.

## Werkzeuge

    ./scripts/measure_cpu.sh <ip> 30           CPU-Aufteilung, zwei Phasen
    ./scripts/ab_detector.sh <bpm> <ip> 40     Abstands-Histogramm, ohne und mit Anker
    python3 sim/mktracks.py tracks             7 annotierte Testtracks, 70..174 BPM
    cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp
    ./simbeat --mode compare                   Schätzer gegeneinander
    ./simbeat --mode single --file X.wav --beats X.beats

## Was ausprobiert und verworfen wurde

Ein psychoakustisches Front-End (Bark-Bänder, Log-Kompression, SuperFlux) wurde gebaut, über
drei Genres am Gerät vermessen und wieder ausgebaut: klar schlechter auf House und Trap,
uneinheitlich auf D&B (92 % in einem Lauf, 65 % im nächsten, und nur mit Anker). Der
Forschungsstand bleibt im Simulator (`--mode psy`, `psyscore`, `psyband`) und in
`proposal.md` §5–6. Ebenso dokumentiert dort: Autokorrelation, der Breitbandpfad, die
rahmenbasierte Erkennung.

## Wenn wieder etwas nicht stimmt: erst die Eingangsgröße messen

Die teuerste Lehre dieser Woche, viermal bestätigt:

- **Onset-Menge ist nicht Tempo-Richtigkeit.** Auf Trap fand ein Detektor das Siebenfache an
  Onsets und meldete trotzdem falsch, weil nur 7 % der Abstände einen Beat lang waren. Die Zahl,
  auf die es ankommt, heißt „davon 1 Beat".
- **Zu ähnliche Zahlen sind ein Alarmzeichen.** Schneiden zwei Varianten exakt gleich ab, misst
  man zweimal dieselbe.
- **Eine Unmöglichkeit ist der beste Hinweis.** Dass ein Teil (`fftUs`) teurer schien als das
  Ganze (`audUs`), hat zwei falsch definierte Messfelder aufgedeckt.
- **Die Testbedingung gehört protokolliert, nicht erinnert.** Eine Gegenprobe lief auf House,
  während sie für D&B gehalten wurde — und hätte fast zum Löschen eines Features geführt.
