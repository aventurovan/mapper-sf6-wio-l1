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

**Start:** po włączeniu animacja – napis „SF6 Mapper” wjeżdża z prawej, pod nim rozchodzą się fale radiowe, potem pojawia się
nazwa trackera i wersja firmware. Po ok. 2 s (albo po naciśnięciu dowolnego przycisku) pokazuje się ekran główny z dwoma
krótkimi piknięciami. Piknięcia brzmią zawsze, także przy dźwięku wyłączonym w menu.

**Ekran główny** – trzy strony, przełączane joystickiem w lewo/prawo:
1. **Stan:**
   - pasek: godzina (czas polski z GPS), GPS z liczbą satelitów, Bluetooth, dźwięk, bateria;
   - stan mapowania z odliczaniem;
   - duże liczby pingów w sesji i punktów do wysłania;
   - ostatni repeater z SNR.

   Repeatery są pokazywane jako pierwsze 2 bajty klucza (np. `A1B2`). Nazwy zna serwer i pokazuje je na mapie.
2. **Sesja:** numer, dystans, pingi, skuteczność, czas oraz 5 ostatnich repeaterów z godziną i SNR.
3. **GPS:** satelity, ruch lub postój, współrzędne, wysokość, data i godzina.

**Przyciski na ekranie głównym:** środek joysticka otwiera menu, przycisk wstecz gasi ekran.

**Menu:** góra i dół wybierają pozycję, środek lub prawo zmienia wartość, lewo zmienia w drugą stronę, wstecz wychodzi.
Menu zamyka się samo po 30 s.

| Pozycja | Działanie |
|---|---|
| Mapowanie | WŁ / STOP; STOP wymaga drugiego naciśnięcia w ciągu 4 s |
| Ping co | 20 / 30 / 40 / 50 / 60 s |
| Nowa sesja | zeruje liczniki sesji (każde włączenie trackera to też nowa sesja) |
| Dźwięk | sygnał trafienia i ostrzeżeń |
| GPS | wł./wył.; wyłączony GPS oszczędza baterię, ale tracker wtedy nie mapuje |
| Bluetooth | wł./wył. |
| Blokada | blokada ekranu wł./wył. |
| Wygaszanie | 30 s / 1 min / 2 min / nigdy; przy zasilaniu z USB ekran nie gaśnie |
| Informacje | nazwa, PIN BT i klucz, firmware, pamięć, ostatni zrzut, napięcie baterii |
| Serwis | usuń dane, usuń parowania BT, ustawienia fabryczne, nowa tożsamość |
| Wyłącz | uśpienie trackera; włączenie: naciśnij środek joysticka |

**Operacje z menu Serwis i wyłączanie** wymagają przytrzymania środka joysticka (2–3 s, z paskiem postępu):
- **Usuń dane:** kasuje punkty. Ostrzega, ile z nich nie zostało wysłanych.
- **Usuń parowania BT:** telefony trzeba będzie sparować ponownie.
- **Ustawienia fabryczne:** usuwa ustawienia, nazwę, dane i parowania BT. Klucz zostaje, więc tracker wraca na to samo konto, choć z nazwą `SF6Map-XXXX`.
- **Nowa tożsamość:** jak ustawienia fabryczne, ale z nowym kluczem, czyli nowym kontem. Wymaga dwóch kroków.

Wszystkie ustawienia zostają po wyłączeniu trackera.

**Blokada ekranu** (domyślnie włączona). Gdy ekran zgaśnie, tracker się blokuje. Przycisk tylko zapala ekran, a odblokowanie
wymaga przytrzymania środka joysticka przez 3 s (pasek postępu na dole ekranu). Przy zasilaniu z USB (tryb samochodowy) blokada i wygaszanie są wyłączone.

**Postój:** po 4 pingach w jednym miejscu tracker przestaje nadawać. Wznawia pracę dopiero przy prawdziwym ruchu,
czyli przemieszczeniu w ciągu ~20 s. Skok pozycji po ponownym złapaniu GPS albo odbicia sygnału nie wznawiają pingów.

**Ostrzeżenia:**
- **Pamięć:** przy 80%, 90% i 100% zajętości pojawia się komunikat i sygnał.
- **Słaba bateria (< 3,4 V):** mapowanie zostaje wstrzymane i wznawia się powyżej 3,55 V albo po podłączeniu USB.
- **Rozładowana bateria (< 3,3 V):** tracker sam się wyłącza, żeby nie uszkodzić zapisu.

**Możliwy limit repeatera:** pudło, przy którym któryś repeater odpowiedział nam już 4 razy w ostatnich 2 minutach,
dostaje znacznik. Na serwerze trafia on do kolumny `lim`, a w eksporcie do kolumny `mozliwy_limit`.

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
