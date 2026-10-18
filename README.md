# Launchpad MQTT

PlatformIO / Arduino dla Waveshare ESP32-S3 Zero (4 MB flash). USB host obsługuje **Novation Launchpad Mini MK3**, VID:PID `1235:0113`, USB MIDI 1.0, kabel MIDI 0. PSRAM nie jest wymagana ani używana. Firmware jest przypięty do PlatformIO espressif32 6.10.0 / Arduino 2.0.17 / ESP-IDF 4.4.7.

## Obsługa

| Kolumny od lewej | Typ | Domyślna żarówka |
| --- | --- | --- |
| 1–2 | RGB | `Ala_nocna` |
| 3–4 | CCT | `Ala_biurko` |
| 5–6 | CCT | `Ala_lewa` |
| 7–8 | CCT | `Ala_prawa` |

Pierwsza kolumna pary: jasność 1/8–8/8 od dołu do góry. Ponowne naciśnięcie wybranego poziomu wyłącza żarówkę. Druga: kolor RGB lub temperatura barwowa 250–454 mired. Nazwy, bazowy temat i dane MQTT zmienia się przez WWW. Domyślny broker: `192.168.211.3:1883`, bez danych logowania; prywatny adres nie jest hasłem. Zapisany wcześniej adres brokera pozostaje w NVS: jeśli w wersji 1.0.0 wpisano omyłkowo `192.168.211.2`, popraw go w `/settings`. `/status` pokazuje obecnie używany host, port i wynik uruchomienia klienta MQTT.

`zigbee2mqtt/<nazwa>/availability` przyjmuje `{"state":"online"}` / `{"state":"offline"}`, także tekstowe `online` / `offline`. Niepoprawne dane nie zmieniają stanu. Offline wygasza obie kolumny i blokuje sterowanie. Rozłączenie brokera też blokuje sterowanie do potwierdzenia availability. Powtarzane `online` nie wysyła presetu. Zaobserwowane offline → online ustawia jasność 254 i `color_temp:370` (~2700 K), również na żarówce RGB obsługującej CCT. Pierwsza wiadomość retained `online` po restarcie nie włącza samoczynnie lampy. Firmware subskrybuje również stan żarówki, aby synchronizować jasność z Zigbee2MQTT.

## Pierwsza konfiguracja i odzyskiwanie

Po pierwszej instalacji albo po 60 s bez działającego Wi-Fi/DHCP urządzenie udostępnia **otwartą** sieć `Launchpad-Setup-XXXXXX`. Połącz się z nią i otwórz http://192.168.4.1/settings. Wprowadź Wi-Fi i broker. Panel ustawień oraz zapis konfiguracji nie wymagają logowania ani hasła, także ze zwykłej sieci LAN. Otwarty AP jest świadomym wymaganiem instalacji: każdy w jego zasięgu może przeprowadzić konfigurację, a ruch tego AP nie jest szyfrowany. Hasła Wi-Fi/MQTT zapisane w urządzeniu nie są odsyłane do formularza ani `/status`.

Puste pole hasła Wi-Fi/MQTT pozostawia wcześniejszą wartość. Jawne pola „Wi-Fi bez hasła” i „Usuń hasło MQTT” umożliwiają usunięcie konkretnego hasła. Zapis do jednego rekordu Preferences jest atomowy; restart następuje dopiero po powodzeniu. Formularz ustawień nie zawiera hasła administratora; token formularza jest obsługiwany automatycznie. AP wyłącza się po 60 s prawidłowego połączenia przy potwierdzonej konfiguracji. Przytrzymaj BOOT (GPIO0) przez 3 s podczas pracy, aby otworzyć AP ponownie. Błąd brokera przy działającym Wi-Fi nie otwiera AP; panel jest wtedy dostępny pod adresem IP urządzenia.

`/` pokazuje debug, `/status` JSON, `/settings` ustawienia, `/update` ręczne HTTP OTA. HTTP oraz ArduinoOTA są przeznaczone do zaufanej sieci LAN, nie do publikacji portów w Internecie. Automatyczne pobieranie z GitHub zawsze używa HTTPS z kontrolą certyfikatu.

Na `/` są również przyciski Włącz/Wyłącz, suwak jasności, wybór koloru RGB lub temperatury CCT i preset Ciepła biel 100% dla każdej żarówki. Poziomy odpowiadają padom Launchpada. Sterowanie nie wymaga logowania. Polecenia `POST /control` sprawdzają token formularza, zakres parametrów i availability; offline blokuje zarówno przyciski, jak i wysłanie MQTT. Po przyjęciu polecenia pady są odświeżane, a stan jasności jest synchronizowany z wiadomościami Zigbee2MQTT.

Przycisk „Sprawdź wersję na GitHub” na `/` uruchamia ręczne sprawdzanie w tle przez `POST /check-update`. Pokazuje wersję zainstalowaną, najnowsze zweryfikowane wydanie i wynik, odświeżając status bez kolejnego kliknięcia. Sprawdzenie wymaga Wi-Fi i czasu NTP, weryfikuje HTTPS/podpis manifestu i działa również przy wyłączonych automatycznych aktualizacjach. Sam przycisk nie instaluje firmware. Po ręcznym sprawdzeniu kolejna automatyczna kontrola przypada za godzinę.

## NVS i przejście ze starszego firmware

Konfiguracja jest zapisywana wyłącznie w namespace **`lp_mqtt_v1`**, klucz **`config`**, JSON ze **`schema:1`**. Ten identyfikator pozostaje zgodny w kolejnych wydaniach; odczyt nie zapisuje domyślnych wartości. Wszystkie ustawienia Wi-Fi/MQTT i wybór automatycznych aktualizacji przetrwają OTA oraz reboot, gdy zachowana jest partycja NVS. OTA tej aplikacji nigdy nie zapisuje NVS ani tabeli partycji. Pole `admin` z wersji 1.0.0 pozostaje zgodne i nie jest kasowane; jeśli wcześniej zapisano tam hasło, chroni ono wyłącznie ręczne HTTP OTA/ArduinoOTA, bez wpływu na dostęp do ustawień. Brak tego hasła jest poprawną konfiguracją.

Jeżeli własnego rekordu jeszcze nie ma, program odczytuje zapisane dane Wi-Fi z konfiguracji sterownika ESP-IDF, po czym używa ich w RAM. `WiFi.persistent(false)` zapobiega nadpisywaniu konfiguracji sterownika. Nie próbuje zgadywać namespace poprzedniego panelu ani kasować ich. Dane MQTT poprzedniego panelu wymagają znajomości jego kodu/kluczy; w razie braku tych informacji wpisz je ręcznie. Przed pierwszym potwierdzeniem ustawień MQTT i automatyczne aktualizacje są wyłączone.

`NvsGuard.cpp` przechwytuje inicjalizację NVS: standardowy Arduino 2.0.17 kasowałby NVS przy `NO_FREE_PAGES` lub `NEW_VERSION_FOUND`; tutaj te przypadki nie uruchamiają formatowania. Wi-Fi przechodzi wtedy na RAM, dane pozostają do odzyskania, `/status` zgłasza `nvs_healthy:false`, a zapis ustawień jest zablokowany. Nie naprawiaj tego przez pełne kasowanie flash bez kopii. Ta ochrona nie zastępuje kopii danych przed zmianą nieznanej wersji/układu flash.

## OTA teraz i partycje

Do istniejącego HTTP `/update` lub ArduinoOTA wgrywaj **tylko aplikację `firmware.bin`** z Releases. Obraz ma nagłówek ESP32-S3 / 4 MB / DIO i mieści się w standardowym slocie `0x140000` (1 310 720 B). Nie wysyłaj `bootloader.bin`, `partitions.bin`, połączonego obrazu flash ani `erase_flash`. Sama aplikacja OTA nie zmienia istniejącej tabeli partycji; używa jej w czasie pracy.

Tabela do czystej instalacji jest w `partitions.csv`: NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, app0 `0x10000/0x140000`, app1 `0x150000/0x140000`. Jeśli obecny firmware nie ma drugiego slotu OTA albo ma mniejszy slot niż plik, **nie wykonuj wgrania**. Stary `/status` nie podaje tych informacji; potrzebny jest poprzedni projekt, odczyt tabeli przez przewód lub wiarygodne potwierdzenie konfiguracji. Nowy `/status` pokazuje wszystkie partycje, rozmiar aplikacji i flash. Nie obiecujemy automatycznego rollbacku po błędzie nowego programu: zależy on od bootloadera i konfiguracji rollback w fizycznym urządzeniu, których OTA aplikacji nie zmienia.

Nowe `/update` wymaga tokenu formularza i SHA256 z wydania; sprawdza chip, limit slotu i hash przed aktywacją. Jeśli w wersji 1.0.0 zapisano hasło `admin`, ręczne HTTP OTA i ArduinoOTA nadal go używają. Przy braku zapisanego hasła oba kanały OTA działają bez logowania. ArduinoOTA zachowuje walidację formatu/MD5 stosu Arduino. Podpis ECDSA jest wymagany dla automatycznego pobierania Releases.

## Automatyczne aktualizacje i publikacja

Co godzinę, po synchronizacji czasu NTP, potwierdzeniu konfiguracji i uzyskaniu sieci, urządzenie sprawdza `https://github.com/Pawel-sp9pw/Launchpad_mqtt/releases/latest/download/manifest.json` oraz `manifest.sig`. GitHub `latest` obejmuje opublikowane stabilne wydania; firmware odrzuca wersje inne niż rosnące `major.minor.patch`. Commity, CI artifacts, drafty i prerelease nie są kanałem aktualizacji. Gdy nie ma wydania/Internetu/NTP lub weryfikacja nie powiedzie się, stara aplikacja działa dalej; ponowna próba następuje po godzinie.

Podpis **ECDSA P-256 / SHA256** wiąże dokładne bajty manifestu z wbudowanym kluczem publicznym. Manifest wiąże wersję, płytkę, rozmiar, SHA256 i URL pliku. Akceptowany jest wyłącznie adres wersjonowanego `firmware.bin` w Releases tego repo. Certyfikaty HTTPS są sprawdzane na podstawie `certs/roots.pem` (wybrane korzenie z Mozilla/certifi); brak ważnego certyfikatu lub czasu blokuje aktualizację. Binarne dane są zapisywane do nieaktywnego slotu, a aktywacja następuje dopiero po porównaniu SHA256 i walidacji obrazu. Pobieranie działa w osobnym zadaniu, a dostęp do flash jest blokowany wspólną blokadą OTA. Restart po 24 h czeka na zakończenie aktualizacji.

Są dwie ścieżki publikacji:

1. **Bez sekretu na GitHub:** CI kompiluje i testuje kod. Osoba posiadająca klucz prywatny pobiera artefakt z konkretnego udanego CI, podpisuje go lokalnie przez `scripts/release.py`, po czym zapisuje `releases/manifest.json`, `releases/manifest.sig` i `releases/approved.json` zawierający `ci_run_id` i pełny `commit`. Workflow `Publish approved signed artifact` weryfikuje udany CI na main, zgodność commita, podpis i hash; tworzy draft, wysyła kompletny zestaw plików i dopiero wtedy publikuje Release. Klucz prywatny nigdy nie trafia do repozytorium ani Actions.
2. **Z sekretem Actions:** ustaw sekret repo `OTA_SIGNING_KEY_PEM` z istniejącym kluczem P-256, podnieś `VERSION` i uruchom ręcznie `Publish signed release` na main. Workflow buduje, testuje, podpisuje i publikuje w tej samej kolejności. Sekret ma być zgodny z `keys/release-public.pem`; skrypt odrzuca inne klucze. Nie generuj nowego klucza dla już wdrożonych urządzeń bez zaplanowanej migracji zaufania.

Wersji i assetów nie zastępuj w miejscu; zawsze zwiększaj `VERSION`. Posiadacz klucza powinien zachować jego bezpieczną kopię. Repozytorium zawiera wyłącznie klucz publiczny i certyfikaty CA, bez haseł i tokenów.

## Budowa i testy

```sh
python -m venv .venv
# aktywuj środowisko; Windows: .venv/Scripts/activate
pip install -r requirements-dev.txt
pio run
g++ -std=c++11 -Wall -Wextra -Werror -I include tests/domain_test.cpp -o domain-test
./domain-test
pytest -q
python scripts/release.py --firmware .pio/build/esp32s3zero/firmware.bin --key /bezpieczna/sciezka/klucz.pem
python scripts/release.py --verify-only
```

Na Windows użyj krótkiej ścieżki projektu i `PLATFORMIO_CORE_DIR` albo tymczasowego mapowania `subst`: bardzo długie ścieżki przekraczają limit linii poleceń kompilatora Xtensa. Testy obejmują przejścia availability, mapowanie wszystkich 64 padów, poprawne zakończenie SysEx z 3 bajtami (CIN 7), porównanie wersji oraz odrzucanie zmienionych binariów/manifestów/podpisów i innego klucza.

## Sprzęt i ograniczenia weryfikacji

USB D−: GPIO19, D+: GPIO20. Wbudowany port przejmuje host, więc logowanie przez USB CDC jest wyłączone; do logów użyj UART0. Launchpad wymaga prawidłowego hostowego zasilania VBUS 5 V i odpowiedniego kabla/adaptera. Nie podłączaj równocześnie dwóch źródeł VBUS. IDF 4.4 obsługuje tu jedno urządzenie bez huba; enumeracja innych VID:PID jest odrzucana.

DHCP odnawia lwIP zgodnie z dzierżawą routera. Nadzorca co 30 s ponawia połączenie przy utracie Wi-Fi/braku IP, bez usuwania danych. MQTT ma osobne zadania IDF i automatyczny reconnect co 5 s; nie blokuje pętli WWW/USB przy niedostępnym brokerze. Preset i zakres CCT zależą od capabilities konkretnego modelu TRÅDFRI; przetestuj fizycznie przed uznaniem wdrożenia za zakończone.

Źródła: [Waveshare](https://docs.waveshare.com/ESP32-S3-Zero), [USB host ESP-IDF 4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/usb_host.html), [Novation Programmer Reference](https://downloads.novationmusic.com/novation/launchpad-mk3/launchpad-mini-mk3-0), [standardowe partycje Arduino 2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/partitions/default.csv).
