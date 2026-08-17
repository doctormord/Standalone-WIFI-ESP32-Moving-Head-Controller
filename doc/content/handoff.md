# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Erstes Mal auf echter Hardware geflasht und live getestet** (Gerät des
> Users, per USB verbunden). Firmware + Dateisystem laufen, das Gerät ist
> im echten Heimnetz erreichbar (`http://movinghead.local`, WLAN-
> Zugangsdaten waren schon vorher in NVS gespeichert und blieben beim
> Flash unangetastet). Reale, vorher gespeicherte Presets/Fixture-Patch
> sind intakt (5 echte Presets, 8 gepatchte Fixtures).
>
> **Dabei ein neuer Bug live gefunden und gefixt:** `/vendor/*`-Routen
> sendeten `Content-Encoding: gzip` doppelt (Framework setzt den Header
> für `.gz`-Dateien selbst, der eigene manuelle Aufruf kam oben drauf) —
> hätte die Offline-Bündelung im echten Browser kaputt machen können,
> obwohl `pio run -t buildfs` das nie hätte auffangen können (reine
> Größenprüfung, kein HTTP-Test). Per `curl -D -`/`curl --compressed`
> gefunden und verifiziert, neu geflasht, bestätigt.
>
> **Was noch NICHT getestet ist** (diese Session war CLI-basiert, `curl`
> gegen die API, kein Browser, keine visuelle/physische Beobachtung der
> Lampe):
> - Die UI selbst im Browser öffnen und ansehen.
> - Die 4 frisch reparierten FX-Panels (Movement/Dimmer/Gobo-Rot/
>   Prisma-Rot) tatsächlich am Fixture beobachten.
> - Blackout-Panic-Button (sollte jetzt hart schneiden statt faden).
> - Chaser laufen lassen und mitten drin einen Regler anfassen (sollte
>   jetzt weiterlaufen statt neu zu starten).
> - Stage-Map-Foto-Upload + Kalibrierung.
>
> **Bekannter offener Punkt:** Der One-Click-Web-Installer (`install.html`
> → `firmware/manifest.json`) ist kaputt, seit der alte `firmware/`-Ordner
> beim GitHub-Push entfernt wurde. Braucht einen frisch gebauten
> `firmware/`-Ordner (jetzt vorhandene, funktionierende Build-Artefakte
> aus `.pio/build/supermini/` könnten dafür genutzt werden) — bewusst noch
> nicht automatisch erledigt.
>
> **Drei Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Browser öffnen (`http://movinghead.local`) und die oben gelistete
>    "noch nicht getestet"-Liste durchgehen — das ist der eigentlich
>    wichtige nächste Schritt, den nur der User (mit Augen auf UI + Lampe)
>    erledigen kann.
> 2. `firmware/`-Ordner neu aufbauen, damit der One-Click-Installer wieder
>    funktioniert.
> 3. Danach frei: die drei zurückgestellten Restrukturierungen, Preset-
>    Engine-Split, ADS1115-Hardware-Joystick, oder die übrigen
>    Tech-Debt-Punkte in `backlog.md`.

## Aktueller Status

Sechs Review-/Test-Runden durch: `/code-review max` (15 Findings),
`/code-review` Vollcodebase (9 Findings), `/ultrareview` teilweise (18
gemeldet, 15 gefixt) + vollständig nachgeholt (8 Findings, alle gefixt),
plus der erste echte Hardware-Test (1 weiterer Bug gefunden+gefixt).
Zielhardware: **ESP32-C3 Supermini**. Repository ist sowohl lokal als auch
auf GitHub (`future`-Branch) git-versioniert und **läuft jetzt auf echter
Hardware**. Verbleibender Blocker: visuelle/physische Verifikation im
Browser und am Fixture steht noch aus.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet — dabei
   den Fund gemacht, dass React/Babel per CDN geladen wurden. Gefixt:
   lokal gehostet, gzip-komprimiert.
2. Stage-Map-Bild-Speichern/Laden geprüft, drei kleine Punkte gefixt.
3. `/code-review` (Vollcodebase) ergab 9 Findings — alle gefixt.
4. Projekt auf GitHub gepusht (`future`-Branch): `V1`/`V2`/`V3`/`firmware`
   entfernt, `images` behalten, `README.md` gezielt aktualisiert.
5. `/ultrareview` (1. Anlauf, Session-Limit-Ausfälle) — 15 von 18 gefixt.
6. `/ultrareview` (2. Anlauf, „nachholen") — 8 von 8 gefixt, größter Fund:
   4 von 6 FX-Panels an tote State-Keys gebunden.
7. Erster echter Hardware-Test: geflasht (Firmware + LittleFS), Boot-Log
   und API live per `curl` geprüft, dabei den doppelten
   `Content-Encoding`-Header gefunden und gefixt, erneut geflasht und
   verifiziert.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
