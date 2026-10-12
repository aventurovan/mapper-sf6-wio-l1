# SF6 Mapper – firmware dla Seeed Wio Tracker L1 / L1 Pro

Samodzielny tracker do mapowania zasięgu sieci MeshCore Południe (SF6).
Działa bez telefonu. Dane zrzuca się później przez BLE do aplikacji Mapper SF6.

## Co robi

- Radio ustawione na sztywno na preset SF6: 869.618 MHz, SF6, BW 62.5 kHz, CR 4/5.
- Po fixie GPS (co najmniej 4 satelity) co **20 / 30 / 40 / 50 / 60 s** (domyślnie 20 s) wysyła zero-hop discovery (tylko repeatery).
  Przez 10 s zbiera odpowiedzi.
- Każdy ping zapisuje z pozycją do flasha QSPI: trafienie (lista repeaterów z SNR w obie strony i RSSI) albo pudło.
  Pojemność to 1.5 MB, czyli około 35–40 tys. punktów.
- Po 4 pingach bez ruchu (przesunięcie poniżej 30 m) robi pauzę i nie nadaje. Wznawia po przemieszczeniu.
- Nie przekazuje pakietów i nie wysyła adwertów. Jest niewidoczny w sieci poza samymi pingami.
- Ma własny klucz publiczny, który identyfikuje mappera w systemie.

## Obsługa

| Przycisk | Akcja |
|---|---|
| Środek joysticka | start / stop mapowania (przy zgaszonym ekranie tylko go budzi) |
| Środek joysticka – przytrzymanie 1 s | dźwięk trafienia wł./wył. |
| Joystick lewo / prawo | interwał pingów krócej / dłużej (20–60 s, co 10 s) |
| Przycisk wstecz | zgaś / zapal ekran |

Interwał widać na ekranie w nawiasie (np. `<30s>`). Jest zapamiętywany po wyłączeniu
i można go też zmienić z aplikacji (komenda `0xA5`). Każdy punkt ma zapisany interwał,
z jakim powstał.

Ekran gaśnie po 60 s bez przycisku.

## Budowanie

Repozytorium zawiera tylko kod mappera. Workflow `.github/workflows/build.yml`
pobiera MeshCore w przypiętej wersji (`MESHCORE_REF`), wstawia mapper do
`examples/sf6_mapper` i buduje firmware. Rusza sam po każdym pushu na `main`,
a ręcznie przez Actions → „Build SF6 Mapper” → Run workflow. Gotowy plik `.uf2`
pobierzesz z sekcji *Artifacts* danego przebiegu.

## Wgrywanie

1. Podłącz tracker przez USB.
2. Kliknij szybko dwa razy reset. Pojawi się dysk USB.
3. Przeciągnij na niego plik `.uf2`.

## Ustawienia przy kompilacji

Można je nadpisać w `build_flags`:

| Flaga | Domyślnie | Opis |
|---|---|---|
| `COLLECT_MS` | 10000 | czas czekania na odpowiedzi |
| `STATIONARY_PINGS` | 4 | pingi w miejscu przed pauzą |
| `MOVE_THRESHOLD_M` | 30.0 | próg ruchu (m) |
| `MIN_SATS` | 4 | minimum satelitów |
| `BLE_PIN_CODE` | 123456 | PIN parowania BLE |

Protokół BLE dla aplikacji opisuje `sf6_mapper/PROTOCOL.md`.
