# Horizon Light Controller — Technical Handover

> Lebendes Dokument — wird überschrieben/ergänzt, nicht angehäuft. Für den
> chronologischen Verlauf siehe `history.md`, für offene Punkte `backlog.md`.

## Projekt-Übersicht

Ein DMX-Lichtcontroller für Moving Heads, basierend auf einem **ESP32-C3
Supermini**. Steuerung über ein reaktives Web-Interface (React 18), das
direkt vom ESP32-Flash (LittleFS) ausgeliefert wird — kein externer Server,
keine App nötig. Zusätzlich Art-Net-Empfang (Universe 0) für DMX von externer
Lichtsoftware, mit Vorrang vor internen Effekten.

Dieses Repository (`Moving_Head_Horizon`) ist das Ergebnis eines Merges
(2026-08-14) aus zwei separaten Vorgänger-Ständen — Details in `history.md`:
- Backend aus `horizon_light_controller` (vollständigste Endpunkt-Sammlung).
- Frontend aus einem Zweig namens "V3" (neuester UI-Stand mit
  `AudioVisualizer`, `useBeatPulse`, Toast-Notifications, Landscape-Hinweis).

## Architektur

### Backend (ESP32 C++)

- **`Moving_Head_Horizon.ino`** — Hauptschleife (`setup()`/`loop()`),
  Hardware-/Kanal-Konfiguration (`CH_*`-Defines, feste 18-Kanal-Personality),
  globaler State, DMX-Ausgabe via Hardware-UART1 (Software-Break über
  `uart_set_line_inverse`), Art-Net-Empfang (`onArtDmx`), Scene-/
  Chaser-Execution (`executePreset`, `executeChaserSlot`, `triggerLoad`),
  Frame-Engine (`updateEngines`). Bindet `WebAPI.h` bewusst erst **nach**
  allen Globals/Hilfsfunktionen ein (Arduino verkettet Übersetzungseinheiten
  textuell — Include-Reihenfolge = Deklarationsreihenfolge).
- **`WebAPI.h`** — REST-artige Endpunkte (`/save`, `/joy_in`, `/set_all`,
  …). Presets/Chaser-Szenen als gepackter Binary-Blob (`SceneData` via
  `prefs.putBytes()`), um NVS-Fragmentierung durch viele Einzelkeys zu
  vermeiden. Vollständige Endpunktliste mit Parametern:
  siehe `functions.md`.
- **`FX_Engine.h`** — `MovementEngine` (Kinematik/Formen fürs Pan-Tilt,
  Phasenversatz pro Fixture für Chase-Muster), `Modulator` (LFO für
  Dimmer/Prisma/Gobo-Rotation), `StepFX` (diskrete Wheel-Schritte für
  Farbrad/statisches Gobo/rotierendes Gobo). Float-Accumulator
  (`exactPan`/`exactTilt` in `updateEngines`) gegen Rundungsdrift bei
  langsamen Joystick-Fahrten.
  **LFO-Formen liegen seit 2026-08-26 in *einer* gemeinsamen Funktion
  `lfoShape(p, mode, curve, allowRandom)`**, die sowohl `Modulator` als auch
  `MovementEngine` benutzen. Vorher hatte `MovementEngine` eine eigene,
  unvollständige Kopie und kannte nur Quadratic und Sine — `Cubic`, `Gauss` und
  `Random` fielen dort still auf Linear zurück, obwohl das UI für beide dasselbe
  Dropdown anbietet. Wer eine Kurve ergänzt, ergänzt sie damit zwangsläufig für
  beide. `allowRandom=false` ist der eine bewusste Unterschied: `Random` würfelt
  pro Aufruf neu, was als Dimmer-Flackern taugt, aber die Size eines
  Bewegungsmusters pro Frame neu setzen würde.
  **Merke zu den Modes:** `0 Forward` und `2 Reverse` sind Sägezähne und springen
  am Zyklusende. Bei Movement-Size heißt das ein physischer Sprung des Kopfes,
  sobald `szSt != szEn` — nur `1 Ping-Pong` ist stetig (live gemessen, siehe
  `history.md` 2026-08-26 (2)).
  ⚠️ **Es gibt zwei Sync-Tabellen, und das ist Absicht — nicht „vereinheitlichen":**
  `syncBeats[7] = {8, 4, 2, 1, 0.5, 0.25, 0.125}` für Modulatoren, StepFX und
  Chaser (Index 0 = längster Zyklus, herunter bis 1/8 Beat), aber
  `moveSyncBeats[8] = {1, 2, 4, 8, 16, 32, 64, 128}` für `MovementEngine`
  (Beats *pro Umdrehung*, aufsteigend). Grund: eine Kopfbewegung muss länger als
  einen Beat dauern dürfen — Bruchteile eines Beats schafft die Mechanik gar nicht,
  während ein Dimmer-Flackern im 1/8-Beat-Raster sinnvoll ist. Entsprechend klemmt
  `MovementEngine` auf `0..7`, alles andere auf `0..6`; beide Klemmungen sind für
  ihre jeweilige Tabelle korrekt. Der Parameter in `FX_Engine.h` heißt in beiden
  Fällen `syncBeats`, was beim Lesen leicht in die Irre führt: entscheidend ist,
  welches Array `updateEngines()` übergibt (`.ino` Zeile ~392 vs ~399).
- **`Audio_Engine.h`** — I2S-Audio-Sampling (Pins 4/5/6) für Beat-Detection
  (Bass/Mid/High über Envelope-Follower + dynamischer Threshold) und
  automatisches BPM-Tracking (Median-Filter über Rolling-History,
  rate-limited Poll alle 40 ms).

### Frontend (React / Babel, `data/index.html`)

- Monolithisches Single-File-Frontend, React 18 + Babel Standalone über CDN
  (`<script type="text/babel">`), kein Build-Schritt — JSX wird im Browser
  transpiliert. In mehrere IIFE-`<script>`-Blöcke gegliedert (Hooks/
  Primitives, geteilte Widgets, Tab-Komponenten).
- **State Management:** zyklisches Polling. `/api/state` alle 500 ms
  (kompakter Live-State: BPM, aktiver Preset, Chaser-Status, Audio-Trigger-
  Flags), `/api/get_dmx` alle 2000 ms (voller State inkl. aller FX-Parameter,
  für UI-Resync z. B. nach Reload). Slider-Änderungen triggern `tFetch`
  (debounced Fetch), um den ESP nicht mit Requests zu fluten.
- **Optimistic-Write-vs-Poll-Sync (seit 2026-08-25, siehe `history.md` für die
  volle Bugjagd-Historie):** ein lokaler UI-Zustand (z. B. gerade recalltes
  Preset) und der nächste Poll können sich überholen — gelöst über einen
  Generation-Counter statt eines Wall-Clock-Timers. `stateGen`
  (`Moving_Head_Horizon.ino`) wird von jeder mutierenden Route hochgezählt
  und statt „OK" zurückgegeben; `/api/get_dmx` liefert denselben Zähler als
  `"gen"` mit. Frontend merkt sich pro Feld „warte auf mindestens Generation
  G" (`pendingGenRef`/`isLocalDirty`, `data/index.html`) und ignoriert
  Poll-Antworten, die älter sind. Zusätzlich lehnen FX-Konfig-Routen (`/fx`,
  `/modfx`, `/colfx`, `/sgobfx`, `/rgobfx`, `/set_all`s Dimmer-Zweig) einen
  Request serverseitig ab (`isStaleWrite()`, `WebAPI.h`), wenn sein
  mitgesendeter `&g=`-Parameter älter ist als `lastRecallGen` — verhindert,
  dass ein bereits abgeschickter, aber verspätet ankommender Request einen
  zwischenzeitlichen Recall überschreibt (die eine der neun 2026-08-25-Bugs,
  die reine Frontend-Reconciliation nicht lösen konnte). `"gsrc"` in
  `/api/get_dmx` (welche Route hat `stateGen` zuletzt erhöht) ist ein
  dauerhaft behaltenes Debug-Feld, analog zu `op`/`ot` und `rawBPM`/`rawMs`/
  `loopMax` weiter unten — bei ähnlichen Sync-Bugs zuerst dort nachsehen,
  bevor man aus Kanal-Diffs zwischen zwei Polls rät (führt in die Irre, da
  Pan/Tilt kontinuierlich aus dem Smoothing-Loop driften, unabhängig von
  jeder HTTP-Mutation).
- **Zwei Ergänzungen vom 2026-08-25 (2), ohne die das Obige nicht trägt**
  (volle Herleitung in `history.md`):
  1. **Gating pro FX-Gruppe, nicht pro Feld.** `pendingGenRef` deckte anfangs
     nur die acht `*FxRunning`-Booleans plus `presetActive`/`presetNames` ab,
     während *alle* FX-Parameter derselben Gruppe ungefiltert gemerged wurden.
     Jede Gruppe wird jetzt als Einheit über ihren ohnehin vorhandenen
     `runningKey` gegated — **Felder *und* die `pr2.*`-Baseline**. Das
     Baseline-Gating ist die wichtigere Hälfte: zieht man die Baseline aus
     einem Poll nach, der den lokalen Edit noch nicht kennt, stimmen State und
     Baseline auf dem *alten* Wert überein, der Diff sieht „nichts zu senden"
     und der Edit ist **gelöscht** statt nur verzögert. Die manuellen Kanäle
     hängen analog an einem gemeinsamen `'channels'`-Key (Pan/Tilt bewusst
     ausgenommen — sie liegen nie im `/set_all`-Batch).
  2. **Kein Schreibvorgang darf verworfen werden.** Die 300-ms-
     `isReceiving`-Totzone nach jeder Poll-Antwort unterdrückte Sends
     korrekt, aber der Effect hängt an `[state]` und `isReceiving` ist eine
     *ref* — ihr Zurücksetzen löst keinen Render aus, es gab also **nie einen
     Retry**. `deferSync()` setzt jetzt einen einzelnen, idempotenten
     320-ms-Timer, der `syncTick` erhöht (steht in der Dependency-Liste).
     Invariante beim Anfassen dieses Effects: *jeder* Pfad mit
     `isReceiving`-Guard muss `deferSync()` rufen — aktuell sechs.
- **Joystick-Config-Read-back (`/api/joycfg`, seit 2026-08-25 (2)):** die neun
  `/joy_cfg`-Werte werden nach NVS persistiert, hatten aber keine
  Lese-Route — ein frisch geladener Tab hielt die UI-Defaults und überschrieb
  bei der ersten State-Änderung die gespeicherte Geräte-Config damit. Das
  Frontend seedet jetzt beim Mount **State *und* Sende-Baseline `p.joyKey`**
  daraus und sendet vorher gar nichts (`joyCfgLoadedRef`). Merkregel für neue
  persistierte Settings: ohne Read-back-Route überschreibt die UI sie beim
  nächsten Reload.
- **Bekannte strukturelle Schwäche (siehe `backlog.md` → „Technische
  Schulden" für die volle Architektur-Diskussion):** mehrere unabhängige,
  asynchrone Schreibpfade (Poll-Merge, `syncFx`/`track()`-Echo, Joystick-
  Smoothing, NVS-Recall) fassen alle denselben `dmxData[]`/Live-State ohne
  zentrale Ownership an. Die obigen Mechanismen lösen jede einzelne
  gefundene Race chirurgisch, sind aber vier verschiedene, parallel
  laufende Sicherungen — ein sauberer Neuentwurf (einziger besessener
  Server-State mit echtem Request/Response, oder WebSockets statt
  Poll+Echo) wäre die strukturelle Lösung.
  **Nachtrag 2026-08-25 (2):** Der Satz „kein akuter Bug mehr", der hier
  stand, war falsch. Genau diese Schwäche produzierte noch am selben Tag die
  nächste Bug-Welle (Werte springen im Programmer-Tab zurück), weil die
  vorherigen Fixes je *ein Feld* absicherten, der **geteilte** Schreib- und
  Merge-Pfad aber ungeschützt blieb. Lehre für die nächste Session an diesem
  Problemfeld: **den geteilten Pfad fixen, nicht das gemeldete Feld** — und
  bei jedem neuen State-Feld beide Fragen stellen: „wird es beim Poll
  gegated?" und „kann sein Send verworfen werden, ohne dass jemand ihn
  wiederholt?".
- **Joystick:** eigener Hook `useKeyboardJoystick` für Tastatur/Maus mit
  mehrstufigem Ramping (Shift/Alt-Modifier), sendet `joyInputX/Y` an
  `/joy_in`.

## Beat-Erkennung (Stand 2026-09-02)

Erkennung läuft auf **Sample-Ebene**, nicht auf FFT-Frames. Ein Frame dauert 32 ms, der
Anschlag einer Bassdrum 5–20 ms — frame-basierte Erkennung kann einen Onset also nur auf
Frame-Grenzen zeitstempeln, was bei einem 460-ms-Beat ±7 % Jitter bedeutet, bevor irgendein
Schätzer anfängt. Nachgebaut ist die analoge Topologie des Pioneer DJM-500, pro Sample bei
16 kHz:

**Bandpass → Hüllkurve → Komparator gegen rollende Referenz → Gipfelwahl → Intervall-Median**

- **Drei unabhängige Bänder** (`sdBass` 40–159 Hz, `sdMid` 159–637 Hz, `sdHigh` über 1273 Hz,
  alle in `Audio_Engine.h`). Jeder FX kann von jedem Band getriggert werden — Movement auf dem
  Kick, Dimmer auf den Hats. Jedes Band hat eine **eigene Erholzeit**: ein Kick kommt einmal
  pro Beat und seine Nachbarn sollen unterdrückt werden, Hats laufen auf Achteln oder
  Sechzehnteln und das Band muss lange vorher wieder scharf sein.
- **Die Schwelle ist eine Position im Dynamikbereich**, `Untergrenze + Anteil × (Spitze −
  Untergrenze)`, kein Vielfaches eines Mittelwerts. Damit ist der Empfindlichkeitsregler
  dimensionslos und muss bei Pegelwechsel nicht nachgestellt werden. Die Untergrenze ist der
  **Median** des Fensters, nicht der Mittelwert — ein Mittelwert wird von genau den Spitzen
  hochgezogen, gegen die er messen soll.
- **Der Onset wird am Gipfel genommen**, nicht am Schwellendurchgang. Ein Durchgang wandert mit
  dem Pegel, ein Gipfel nicht; Peak-zu-Peak gemessene Abstände sind deutlich wiederholgenauer.
- **Tempo ist der Median der Kick-Abstände.** Abstände außerhalb 60–200 BPM werden am Eingang
  verworfen — das ersetzt jede Oktav- und Faltungslogik. Ein Wert wird nur ausgegeben, wenn die
  Abstände untereinander übereinstimmen, sonst gilt der alte weiter.
- **`drift` ist die Gesundheitsprüfung** des Ganzen: `sdSampleClock` zählt nur *verarbeitete*
  Samples, und die Beat-Abstände werden darauf gemessen. Verliert die Abholung Audio, geht die
  Uhr nach und **jedes Tempo fällt zu hoch aus**. Muss bei ~0 Promille stehen.

Verifiziert gegen synthetische Musik mit exakt bekannter Beat-Position (`sim/`): F-Maß 0,99
bei 100 % Treffergenauigkeit und ~7 ms Zeitfehler bei 130 BPM, Tempo über 90–174 BPM auf 2 BPM
genau, stabil über einen 50-fachen Pegelbereich. Zum Vergleich: Dixon (DAFx-06) misst für das
beste von acht Verfahren F = 0,964 bei 8,8 ms über 106.054 Onsets.

## CPU: gemessen am Gerät, 2026-09-03

Die Frage stand seit dem Audio-Umbau offen und wurde nie beantwortet. Jetzt gemessen mit
`scripts/measure_cpu.sh`, ESP32-C3 @160 MHz, ein Fixture gepatcht, Mikrofon an, keine Musik:

    Schleifenrate            172 /s      schlimmste Schleifenlücke   22 ms
    updateEngines            252 us  ->  4,3 %   eines Kerns
    Audio-Block gesamt      3036 us  ->  9,5 %   bei 31 Blöcken/s
      davon FFT             1211 us  ->  3,8 %
      davon psyProcessFrame   46 us  ->  0,14 %  (mit psy=1)
    ------------------------------------------------
    Summe                             ~14 %

**Der C3 ist nicht CPU-begrenzt.** Damit ist auch die Chipfrage beantwortet: eine FPU (nur
ESP32, S3, H4 und P4 haben eine — C5, C6 und S2 **nicht**) würde heute nichts kaufen. Und die
dauerhaft laufende FFT kostet 3,8 %, ist also klar bezahlbar; der psychoakustische Detektor
obendrauf ist mit 0,14 % praktisch umsonst.

### Warum die erste Messung wertlos war

Zwei Telemetriefelder maßen etwas anderes als ihr Name — beide am 2026-09-03 korrigiert:

- **`fftUs`** wurde vom Anfang des Audio-Blocks aus gestoppt und enthielt damit die
  Sample-Skalierung über 512 Werte, das Auto-Gain und den **kompletten Sample-Rate-Detektor**.
  Es meldete 2913 µs für etwas, das 1211 µs kostet — Faktor 2,4 zu hoch, und die Antwort auf
  „ist die dauernde FFT bezahlbar" wäre entsprechend falsch ausgefallen.
- **`audUs`** wurde von jedem Aufruf von `pollAudioEngine()` überschrieben, auch von den vielen,
  die noch gar keinen vollständigen Block haben (~170 Aufrufe/s gegen ~31 echte Blöcke/s). Es
  zeigte also meist einen Leerlauf.

Auffällig war die Kombination: der Mittelwert von `audUs` (959 µs) lag **unter** dem von `fftUs`
(2913 µs), obwohl die FFT innerhalb des Audio-Blocks läuft. Ein Teil kann nicht teurer sein als
das Ganze — *diese* Unmöglichkeit hat den Fehler aufgedeckt, nicht die absolute Größe der Zahl.

### Beim Lesen der Werte beachten

- `engUs`/`audUs`/`fftUs`/`psyUs` sind **Momentaufnahmen des letzten Aufrufs**, keine
  Mittelwerte. `engMax`/`audMax`/`loopMax` sind Maxima über ein gleitendes **5-Sekunden-Fenster**
  und werden gemeinsam zurückgesetzt.
- `engMax` erreichte 5176 µs (entspräche 89 % bei Dauerlast). Das ist eine einzelne Spitze im
  Fenster, kein Dauerzustand — WLAN und Webserver landen gelegentlich in derselben Iteration.
  Dasselbe erklärt, warum `fftUs` zwischen 795 und 2738 µs schwankt.
- `fftUs` ist **0**, solange die FFT nicht läuft. Sie ist bedarfsgesteuert, und `/api/state`
  verlängert die Freigabe **nicht**. Bei dieser Messung lief sie durchgehend, weil ein Browser
  den AUDIO-Tab offen hatte — die geplante „ohne FFT"-Vergleichsphase kam deshalb nicht zustande.
  Wer sie braucht: alle Browser-Tabs schließen und erneut messen.

## LFO-Kurven: zwei Familien, und warum das zählt

`lfoShape()` verkettet zwei Schritte — der **Modus** formt die Phase, die **Kurve** formt das
Ergebnis. Daraus folgt eine Unterscheidung, die keine Beschriftung verrät:

- **Rampen** (Linear, Quad, Cubic, Sine) laufen monoton 0 → 1. Sie *brauchen* einen Modus, der
  sie wieder herunterbringt; Sine + PingPong ergibt einen sauberen Auf-und-Ab-Impuls.
- **Gauss ist bereits ein vollständiger Impuls** — dunkel → hell → dunkel, mit der Spitze beim
  Eingangswert 0,5.

Deshalb ergibt **Gauss + PingPong zwei Blitze**: PingPong schickt den Eingang 0 → 1 → 0, er
passiert die 0,5 zweimal, und die Glocke feuert jedes Mal. Kein Fehler, sondern eine Glocke auf
eine bereits gefaltete Rampe. Für einen einzelnen weichen Impuls: **Gauss + Forward.**

    Gauss  Forward   |      ....:::--==+**###@@@@@@@@@###**+==--:::....      |  1 Spitze
    Gauss  PingPong  |  ..:-=+*#@@@@@#*+=-:..         ..:-=+*#@@@@@#*+=-:.. |  2 Spitzen
    Sine   PingPong  |   ....::--===++***###@@@@@@@@@@###***++===--::....   |  1 Spitze

Nebenbefund: **Gauss + Reverse ist identisch zu Gauss + Forward**, weil die Glocke symmetrisch um
0,5 liegt und das Umdrehen sie auf sich selbst spiegelt. Bei dieser Kurve hat das Modus-Menü
faktisch nur zwei Ergebnisse statt drei.

## Kommandierte Amplitude ist tempo-unabhaengig, die tatsaechliche nicht

Am 2026-09-04 live bemerkt: nach einem Flash schien die Bewegung "groesser und langsamer", und
man hoerte es am Motor. Gemessen ueber `op`/`ot` auf `/api/get_dmx` (die wirklich ausgegebene
Pan/Tilt-Position von Fixture 0), gleiche FX-Parameter, nur das Tempo variiert:

    172 BPM (Kreis 1,40 s)   Pan 39318 Schritte
    120 BPM (Kreis 2,00 s)   Pan 39319 Schritte

Der Controller sendet also bei jedem Tempo dieselbe Amplitude. Was sich aendert, ist das
**Fixture**: bei 1,4 s pro Umdrehung folgt der Motor der kommandierten Bahn nicht mehr, die
tatsaechliche Auslenkung faellt kleiner aus und der Lauf klingt angestrengter. Das ist Mechanik,
kein Fehler -- aber es heisst, dass ein schneller Sync-Teiler die Figur physisch verkleinert.

Praktische Folge: **nach jedem Flash ist der Tap-Anker weg und `globalBPM` faellt auf seinen
Startwert.** Eine beat-synchrone Bewegung laeuft danach in einem anderen Tempo und sieht deshalb
anders aus, ohne dass sich an ihr etwas geaendert haette. Vor jedem Vorher/Nachher-Vergleich am
Licht also das Tempo festnageln (Manuell-Modus, `/beat?bpm=`), nicht nur die FX-Parameter.

## Rechnen ohne FPU: die Regel, die 24 Operationen je Frame gekostet hat

Der ESP32-C3 hat **keine FPU**. Jede float-Operation ist eine Bibliotheksfunktion, jede
double-Operation eine deutlich teurere. Daraus folgt eine Regel, die im Quelltext unsichtbar ist:

**Jedes Literal in Fließkomma-Ausdrücken braucht ein `f`.** Und Arduinos eigenes `PI` ist ein
double-Literal (`Arduino.h:46`, ohne Suffix), weshalb dieses Projekt `PI_F` benutzt.

Ohne das promoviert der ganze Ausdruck auf double, rechnet dort und konvertiert zurück. Am
2026-09-04 im disassemblierten Binary gezählt: **24 double-Operationen je Frame**, allein aus
`PI` und zwei `60000.0`. Nach der Korrektur null, und am Gerät 7,9 % → 7,4 % eines Kerns.

So findet man es wieder:

    riscv32-esp-elf-objdump -d .pio/build/supermini/firmware.elf > fw.asm
    # dann je Funktion die Aufrufe von __muldf3 / __extendsfdf2 / __truncdfsf2 zaehlen

## Performance & Skalierung

- **Kein FPU am ESP32-C3** (RV32IMC, 1 Core @ 160 MHz). Jedes
  `sinf/cosf/powf/expf` in `FX_Engine.h` ist Software-Emulation
  (libgcc soft-float) — das ist der einzige nennenswerte CPU-Kostenpunkt im
  System, skaliert mit *Fixture-Anzahl × aktive FX*.
- **Aktueller Stand (2 Fixtures): unkritisch.** `getValues()` macht ~4
  Form-Trig + 2 Rotations-Trig pro Fixture, bei 2 Fixtures ≈ 12
  soft-float-Trig-Calls pro Loop-Durchlauf — einstelliger Prozentbereich der
  Loop-Zeit. DMX-Frame bei 2 Fixtures ~1,6 ms gegen 30-ms-Sendetakt
  (~33 Hz) — nie das Nadelöhr. Headroom bis ~8 Fixtures gut.
- **Hauptsächlicher Optimierungs-Hebel (noch nicht implementiert):**
  `updateEngines()` baut den kompletten Output-Buffer inkl. `getValues()`
  und `memset`/`memcpy(513)` in *jedem* Loop-Durchlauf (Hunderte Hz),
  gesendet wird aber nur alle 30 ms (~15× Overhead). Output-Assemblierung
  (und ggf. die ganze FX-Engine-Verarbeitung) in den
  `if (now - lastDmxOut >= 30)`-Block ziehen → Movement-Soft-Float-Last
  sinkt um ~Faktor 15. Gefahrlos, weil alle `.process()`-Methoden dt-basiert
  integrieren und `getValues()` zustandslos ist (33 Hz reicht für einen
  Moving Head locker). Macht 8 Fixtures + Hardware-Joystick entspannt —
  siehe `backlog.md`.
- **Audio-Pfad (überarbeitet 2026-09-02).** Gemessen lag `audUs` bei 1200–2400 µs pro
  32-ms-Frame, davon `fftUs` 1127–1207 — die FFT war der mit Abstand größte Einzelposten.
  Sie ist **nicht mehr Teil der Erkennung** (die Sample-Raten-Kette arbeitet auf Rohsamples)
  und läuft nur noch, solange jemand `/api/audio_debug` abfragt, also den AUDIO-Tab offen hat.
  Ebenso laufen Mid und High nur, wenn ein FX auf sie geroutet ist. Der Normalfall — live
  spielen, Browser zu, Movement auf Kick und Dimmer auf Hats — fährt damit **zwei Detektoren
  und keine FFT**, wo vorher ein Detektor und die FFT bedingungslos liefen. Am Gerät noch zu
  bestätigen: `audUs`/`fftUs`/`loopMax` einmal mit geschlossenem und einmal mit offenem Tab.
- **Kleinere Wins:** Rotationsmatrix (`cosf/sinf(rRad)`) ist über alle
  Fixtures identisch, wird aber pro Fixture neu berechnet — einmal pro
  Frame reicht. JSON-String-Bauen in `/api/get_dmx` churnt Heap, läuft aber
  nur bei 0,5 Hz Polling → kein CPU-Thema (WebSocket-Migration steht im
  Backlog).

### Hardware-Alternativen (bewertet, keine Migration geplant)

FPU-Status: **C3** (RV32IMC) keine FPU, 1 Core 160 MHz · **S2** (Xtensa LX7)
keine FPU, 1 Core 240 MHz, kein Bluetooth · **S3** (Xtensa LX7)
**Hardware-FPU**, Dual-Core 240 MHz · **C5** (RV32IMAC) keine FPU, 1 HP-Core
240 MHz + LP-Core, Dualband-Wi-Fi 6 (2,4 + 5 GHz).

- **S3** wäre das einzige echte Compute-Upgrade (Hardware-FPU + zweiter
  Core, der DMX/Engine von WiFi/HTTP entkoppeln könnte).
- **C5** bringt keinen FPU-Gewinn, aber sauberes 5-GHz-Band für Art-Net in
  vollen Venues — adressiert Funkstabilität, nicht FX-Performance.
- **S2** ist für dieses Projekt ohne Mehrwert.

Entscheidung (siehe `history.md`, 2026-06-14): **beim ESP32-C3 Supermini
bleiben.** Der Output-Build-Hebel (siehe oben) gibt genug Headroom für die
aktuell absehbare Skalierung, ohne Hardware-Wechsel.

## Geplante Erweiterungen (Design-Notizen)

### Hardware-Joystick via ADS1115 (I²C)

- Schreibt direkt `joyInputX/Y` in der Firmware → spart HTTP-Roundtrip,
  entlastet die CPU zusätzlich.
- **Pflicht: nicht-blockierend lesen.** Default 128 SPS = ~8 ms/Conversion;
  ein blockierender `Wire`-Read stallt den Loop und zerstört DMX-Timing.
  Lösung: rate-limited Poll (20–40 ms, wie `pollAudioEngine()`), Datenrate
  hoch (bis 860 SPS ≈ 1,2 ms) oder Single-Shot-Statemachine.
- I²C ist frei (Audio nutzt I²S). Pin-Budget C3 beachten: 4/5/6 = I²S,
  7 = DMX-TX → 2 freie GPIOs für SDA/SCL wählen.

### Preset-Engine-Split (complete / movement / color / effects)

- Ziel: Movement live aus einem Slot recallen, ohne Color/Dimmer/Effekte zu
  verstellen.
- Performance-Impact ~null (reine Architektur).
- `SceneData` trennt Gruppen bereits per Präfix: `f*` = Movement,
  `c*/sg*/rg*` = Color/Gobo, `d*/gr*/pr*` = Modulatoren. Partial-Recall
  heißt also v. a. „welche Feldgruppe kopiere ich".
- **Knackpunkt:** `executePreset` resettet aktuell global (`centerPan16`,
  `centerTilt16`, `dimSmoothTarget`, `joySmoothX/Y`, `mapIsMoving`). Ein
  Movement-only-Recall darf statische DMX-/Color-Werte und Dimmer NICHT
  anfassen — dort liegt die eigentliche Refactoring-Arbeit. Partielle
  Recall-Endpunkte oder eine Feldgruppen-Maske statt eines atomaren Apply.

## Test ohne Hardware

`sim/` übersetzt das echte `Audio_Engine.h` nativ gegen ein nachgebautes `Arduino.h` und einen
nachgebauten I2S-Treiber und fährt es gegen synthetische Musik mit **exakt bekannter**
Beat-Position — oder gegen eine `.wav`-Datei. Modelliert werden die DMA-Ringtiefe, die
Abtastrate und der Jitter der Hauptschleife; `simI2sDropped` zählt Samples, für die im Ring kein
Platz war.

Der Grund für seine Existenz: drei Einmessreihen auf Hardware wurden dadurch wertlos, dass die
Musik während der Messung weiterlief, und eine davon erzeugte einen falschen „Durchbruch" — ein
freilaufender Oszillator sah wie ein funktionierender Detektor aus, weil seine Sperre auf das
Beat-Intervall eingemessen worden war. Der Simulator fand bei seinen ersten Läufen drei echte
Fehler, darunter eine dauerhafte Stummschaltung nach dem ersten Onset.

**Er ersetzt die Hardware nicht.** Kein Mikrofon, kein Raum, keine PA-Kompression, und die
Kick/Bass-Balance des Kunsttracks ist erfunden. Ein Ergebnis dort ist notwendig, nicht
hinreichend. Details in `sim/README.md`.

Dazu `scripts/check_ui.sh`: transpiliert alle Babel-Blöcke aus `data/index.html` mit dem
ohnehin mitgelieferten Babel, damit ein Syntaxfehler hier auffällt statt als weiße Seite auf dem
Gerät. Es gibt keinen Build-Schritt für die UI, also prüft sonst nichts.

## Setup & Flashen

1. Arduino IDE (oder `arduino-cli`): Board **"ESP32C3 Dev Module"** wählen.
2. **Wichtig:** `USB CDC On Boot` auf **Disabled**, damit der Hardware-Reset
   nach dem Flashen funktioniert.
3. Upload Speed auf **115200** drosseln, um Timeouts zu vermeiden.
4. `ArtnetWifi`-Library installieren (einzige Nicht-Core-Abhängigkeit).
5. Den kompletten `data/`-Ordner (inkl. `data/vendor/`!) auf LittleFS
   hochladen (Filesystem-Uploader-Plugin, oder `pio run -t uploadfs` bei
   PlatformIO). **Der `/upload_gui`-Fallback-Endpunkt reicht seit
   2026-08-15 nicht mehr als alleiniger Upload-Weg** — er ersetzt nur
   `index.html` selbst, nicht `data/vendor/`. Eine so installierte UI würde
   auf `/vendor/react.js` mit 404 laufen und nicht laden. Er ist nur als
   Erstinbetriebnahme-/Recovery-Pfad für die HTML-Datei gedacht, solange
   noch gar kein `index.html` im Flash liegt.

### Flashen per Kommandozeile / OTA (verifiziert 2026-08-25)

Direkt per USB, ohne Arduino IDE — Port explizit angeben, das Board meldet
sich als Espressif USB-JTAG (`VID:PID=303A:1001`):

```
pio run -t upload   --upload-port /dev/cu.usbmodem1101   # Firmware -> 0x10000
pio run -t uploadfs --upload-port /dev/cu.usbmodem1101   # data/    -> 0x290000
```

Beides schreibt an die eigenen Offsets und lässt die `nvs`-Partition
(`0x9000`, WiFi/Patch/Presets) unangetastet — im Gegensatz zu
`firmware.factory.bin` an `0x0`, das NVS mitwischt (siehe `CLAUDE.md` und
den Unfall vom 2026-08-20). Nach dem Flashen kontrollieren, dass
`/api/state` noch die richtigen Preset-Namen liefert.

**OTA funktioniert seit 2026-08-25 wirklich** (davor war `ArduinoOTA`
eingebunden und `handle()` lief, aber `begin()` wurde nie aufgerufen — es gab
schlicht keinen Listener, trotz README-Werbung). Die Partitionstabelle ist
echtes Dual-OTA (`app0`/`app1` je 1280K plus `otadata`), die Firmware belegt
~1216K. Verifizierter Weg:

```
python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
        -i 192.168.8.113 -p 3232 -f .pio/build/supermini/firmware.bin -r
```

(`--upload-protocol` ist **keine** `pio run`-Kommandozeilenoption, nur eine
`platformio.ini`-Einstellung — daher der direkte `espota.py`-Aufruf.)
OTA überträgt nur die Firmware; für `data/` weiterhin `uploadfs` per USB.

### Kompilieren ohne Arduino IDE (PlatformIO)

`platformio.ini` im Repo-Root erlaubt einen reinen Kompilier-Check per
Kommandozeile (`pio run`), z. B. für Claude Code zur Selbstverifikation nach
Codeänderungen — **ersetzt nicht** das Flashen/Testen auf echter Hardware.
Board-ID `nologo_esp32c3_super_mini` (PlatformIO-Profil, das exakt dem
ESP32-C3 Supermini entspricht), `src_dir = .` lässt PlatformIO direkt auf
den bestehenden `.ino`/`.h`-Dateien im Root bauen (keine Kopie nach `src/`
nötig — das Sketch bleibt für die Arduino IDE unverändert nutzbar).
`.pio/` ist reiner Build-Cache, gefahrlos löschbar.

**Verifiziert am 2026-08-15:** Nach dem Fix der `colFX`/`sgobFX`/`rgobFX`-
Deklarationen kompiliert das Sketch sauber. Flash-Auslastung dabei: **90,3 %**
(1.183.197 von 1.310.720 Bytes der App-Partition) — wenig Puffer für neue
Features, siehe `backlog.md`. Eine harmlose Compiler-Warnung bleibt: die
I2S-API in `Audio_Engine.h` nutzt das von ESP-IDF als deprecated markierte
`driver/i2s.h` statt `driver/i2s_std.h` — kompiliert noch, aber Migration
vor einem künftigen ESP-IDF-/arduino-esp32-Versionssprung sinnvoll.
