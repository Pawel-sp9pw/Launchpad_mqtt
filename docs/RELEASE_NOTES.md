Wersja 1.0.1 usuwa logowanie do panelu ustawień `/settings` i umożliwia zapis konfiguracji bez hasła administratora. Formularz nie wymaga tworzenia hasła. Zapisane Wi-Fi/MQTT pozostają zgodne w NVS, schema 1. Brak hasła nie powoduje ciągłego otwierania AP ani blokowania ArduinoOTA. Wcześniej zapisane hasło z 1.0.0 pozostaje używane wyłącznie przez ręczne OTA, jeśli zostało ustawione.

Domyślny broker poprawiono na `192.168.211.3:1883`, bez logowania. Wcześniej zapisany błędny adres `192.168.211.2` należy poprawić w `/settings`; OTA zachowuje zapisane ustawienia. `/status` pokazuje używany adres, port i informację o uruchomieniu klienta MQTT.

Panel `/` pozwala teraz włączać i wyłączać każdą żarówkę, regulować jasność, wybierać kolor RGB lub temperaturę CCT oraz włączać ciepłą biel 100%. Offline blokuje przyciski i polecenia po stronie urządzenia. Udane polecenie odświeża również pady Launchpada. Sterowanie z WWW nie wymaga logowania.

Firmware dla Waveshare ESP32-S3 Zero 4 MB, Novation Launchpad Mini MK3 i czterech TRÅDFRI przez Zigbee2MQTT.

Przycisk „Sprawdź wersję na GitHub” sprawdza podpisane opublikowane wydanie i pokazuje wersję zainstalowaną oraz najnowszą w repozytorium. Ręczne sprawdzenie działa w tle, odświeża wynik w panelu i samo nie instaluje firmware.

- 1 RGB + 3 CCT, po dwie kolumny, availability JSON i blokada offline.
- Ciepła biel 370 mired (~2700 K) i jasność 254 po zaobserwowanym przejściu offline → online.
- Panel ustawień NVS, otwarta sieć konfiguracji po błędzie połączenia, HTTP debug i OTA oraz ArduinoOTA.
- Restart po 24 h, obsługa DHCP przez lwIP i reconnect Wi-Fi/MQTT w tle.
- Automatyczne stabilne Releases przez HTTPS, podpis ECDSA P-256 i SHA256 przed aktywacją.

Do OTA służy wyłącznie `firmware.bin`. Nie wgrywaj bootloadera, tabeli partycji ani obrazu całego flash.
Starsze namespace NVS pozostają na miejscu. Bez kodu poprzedniej wersji nie można zagwarantować odczytu jej własnych kluczy MQTT; można wprowadzić je w panelu.
Pierwsze uruchomienie wymaga potwierdzenia ustawień w sieci `Launchpad-Setup-*`, pod adresem http://192.168.4.1.

