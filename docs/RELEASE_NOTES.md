Firmware dla Waveshare ESP32-S3 Zero 4 MB, Novation Launchpad Mini MK3 i czterech TRÅDFRI przez Zigbee2MQTT.

- 1 RGB + 3 CCT, po dwie kolumny, availability JSON i blokada offline.
- Ciepła biel 370 mired (~2700 K) i jasność 254 po zaobserwowanym przejściu offline → online.
- Panel ustawień NVS, otwarta sieć konfiguracji po błędzie połączenia, HTTP debug i OTA oraz ArduinoOTA.
- Restart po 24 h, obsługa DHCP przez lwIP i reconnect Wi-Fi/MQTT w tle.
- Automatyczne stabilne Releases przez HTTPS, podpis ECDSA P-256 i SHA256 przed aktywacją.

Do OTA służy wyłącznie `firmware.bin`. Nie wgrywaj bootloadera, tabeli partycji ani obrazu całego flash.
Starsze namespace NVS pozostają na miejscu. Bez kodu poprzedniej wersji nie można zagwarantować odczytu jej własnych kluczy MQTT; można wprowadzić je w panelu.
Pierwsze uruchomienie wymaga potwierdzenia ustawień w sieci `Launchpad-Setup-*`, pod adresem http://192.168.4.1.

