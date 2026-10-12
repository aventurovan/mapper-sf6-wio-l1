# SF6 Mapper – firmware dla Seeed Wio Tracker L1 / L1 Pro

Samodzielny tracker do mapowania zasięgu sieci MeshCore Południe (SF6).
Działa bez telefonu. Dane zrzuca się później przez BLE do aplikacji Mapper SF6.

## Co robi

- Radio ustawione na sztywno na preset SF6: 869.618 MHz, SF6, BW 62.5 kHz, CR 4/5.
- Po fixie GPS (co najmniej 4 satelity) co **20 / 30 / 40 / 50 / 60 s** (domyślnie 20 s) wysyła zero-hop discovery (tylko repeatery).
  Przez 10 s zbiera odpowiedzi.
- Każdy ping zapisuje z pozycją do flasha QSPI: trafienie (lista repeaterów z SNR w obie strony i RSSI) albo pudło.
  Pojemność to 1.5 MB, czyli około 35–40 tys. punktów.
- Po 4 pingach bez ruchu robi pauzę i nie nadaje. Wznawia dopiero przy prawdziwym ruchu (szczegóły w punkcie „Postój”).
- Nie przekazuje pakietów i nie wysyła adwertów. Jest niewidoczny w sieci poza samymi pingami.
- Ma własny klucz publiczny i nazwę. Każdy tracker to osobne konto w Mapperze, a nazwa trackera jest nickiem.
  Nazwa BLE to zawsze `MeshCore-` + nazwa, np. `MeshCore-Pawlo_T1`. Domyślnie jest to `MeshCore-SF6Map-XXXX`.
  Nazwę ustawia się z aplikacji: 2–20 znaków ASCII, bez spacji i polskich liter.
  Tracker podpisuje wyzwanie logowania swoim kluczem tak samo jak companion.

## Obsługa

**Ekran stanu**

| Przycisk | Akcja |
|---|---|
| Środek joysticka | otwiera menu |
| Przycisk wstecz | gasi ekran (z blokadą: od razu blokuje) |

**Menu**: góra/dół wybiera pozycję, środek lub prawo zmienia wartość, lewo zmienia w drugą stronę, wstecz wraca do ekranu stanu.
Menu zamyka się samo po 30 s bez przycisku.

| Pozycja | Działanie |
|---|---|
| Mapowanie | WŁ / STOP; STOP wymaga drugiego naciśnięcia w ciągu 4 s („STOP? OK”) |
| Ping co | 20 / 30 / 40 / 50 / 60 s |
| Dźwięk | sygnał trafienia wł./wył. |
| GPS | wł./wył.; wyłączony GPS oszczędza baterię, ale tracker wtedy nie mapuje |
| Bluetooth | wł./wył.; wyłączony – tracker niewidoczny dla telefonu, aby zrzucić dane, włącz z powrotem |
| Blokada | blokada ekranu wł./wył. |
| Informacje | nazwa, PIN Bluetooth, początek klucza, wersja firmware, zajętość pamięci |

Wszystkie ustawienia zostają po wyłączeniu trackera.

**Blokada ekranu** (domyślnie włączona). Gdy ekran zgaśnie (po 60 s albo przyciskiem wstecz), tracker się blokuje.
Naciśnięcie dowolnego przycisku tylko zapala ekran i pokazuje stan z napisem „BLOKADA: trzymaj OK”.
Odblokowanie: przytrzymaj środek joysticka przez 1 s. Przypadkowe naciśnięcia w kieszeni nie zmienią więc ustawień
ani nie zatrzymają mapowania.

**Postój:** po 4 pingach w jednym miejscu tracker przestaje nadawać. Wznawia pracę dopiero przy prawdziwym ruchu,
czyli przemieszczeniu w ciągu ~20 s. Skok pozycji po ponownym złapaniu GPS albo odbicia sygnału nie wznawiają pingów.

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
