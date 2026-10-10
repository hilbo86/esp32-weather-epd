# RTC-Fehlerspeicher

Der Speicher hält die letzten zehn fehlgeschlagenen Aktualisierungsversuche
als Ringpuffer in `RTC_NOINIT_ATTR`. Ein weiterer Fehler verdrängt den ältesten
Eintrag. Erfolgreiche Aktualisierungen löschen keine Fehler. Ein Versuch ist
ein Durchlauf von `setup()`; interne API- und Display-Wiederholungen erzeugen
keine separaten Einträge. Ein endgültig gescheiterter Displayversuch wird
auch beim Zeichnen einer Fehlerseite oder des Diagnosemusters erfasst.
Sensorfehler und Speicherfehler beim Zeichnen der Warnmeldungen werden ebenfalls
erfasst, auch wenn noch ein eingeschränktes Wetterbild angezeigt werden kann.

Es gibt keine Flash-/NVS-Schreibzugriffe für diesen Fehlerspeicher. Die Daten
bleiben über Deep Sleep und Software-Neustarts erhalten. Nach Verlust des
RTC-Speicherinhalts, etwa durch Stromverlust, wird der Speicher anhand der
Formatkennung und Ringgrenzen neu initialisiert. Die RTC-Speicherhaltung entspricht der
[Espressif-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-reference/system/sleep_modes.html#power-down-of-rtc-peripherals-and-memories).

## Datenformat für die App

`include/UpdateFailureHistory.h` definiert die stabilen numerischen Codes und
das Format; `getUpdateFailureHistory()` liefert lesenden Zugriff auf den
gespeicherten Ringpuffer. Der Speicher benötigt **88 Bytes**: 8 Bytes Metadaten
und zehn Einträge zu je 8 Bytes. Es werden keine Fehlertexte gespeichert.

| Feld eines Eintrags | Typ | Bedeutung |
| --- | --- | --- |
| `timestamp` | `uint32_t` | Unix-Zeit in Sekunden, UTC; `0` bedeutet unbekannte Uhrzeit |
| `codes` | `uint16_t` | Bitmaske aller Fehlerursachen dieses Versuchs |
| `detail` | `int16_t` | Originalstatus bei API-Fehlern; sonst `0` |

| Fehlercode / Bit | Bedeutung |
| --- | --- |
| `0x0001` | Batterie niedrig |
| `0x0002` | Batterie sehr niedrig |
| `0x0004` | Batterie kritisch niedrig |
| `0x0008` | Zeitsynchronisation fehlgeschlagen |
| `0x0010` | One-Call-API fehlgeschlagen |
| `0x0020` | Air-Pollution-API fehlgeschlagen |
| `0x0040` | BME-Sensor nicht initialisierbar |
| `0x0080` | Ungültige BME-Messwerte |
| `0x0100` | Displayaktualisierung endgültig fehlgeschlagen |
| `0x0200` | Speicherreservierung für Warnmeldungen fehlgeschlagen |

Beispielsweise bedeutet `codes=0x0110`: One-Call-API **und** Anzeige ihrer
Fehlerseite fehlgeschlagen. `detail=401` enthält dabei den HTTP-Status der API.
Negative HTTPClient-Statuswerte bleiben unverändert erhalten;
JSON-Fehler sind wie im API-Client als `-256 - DeserializationError::Code`
kodiert. Die App prüft einzelne Bits mit `codes & Fehlercode`.

Metadaten: `magic=0x55460001` kennzeichnet Formatversion 1, `count` ist die
Anzahl gültiger Einträge und `next` der nächste Schreibplatz. Der älteste
Eintrag liegt bei `(next + 10 - count) % 10`; die folgenden Einträge werden
modulo 10 gelesen. `batteryErrorActive` unterdrückt doppelte Batteriefehler;
`reserved` ist derzeit `0`. Ein Speicherabbild ist Little Endian; für eine
Übertragung an eine App sollten Felder ausdrücklich serialisiert werden.

Der Zeitstempel stammt beim ersten Fehler des Versuchs aus der Systemzeit.
Diese läuft über Deep Sleep weiter. Vor der ersten gültigen Zeitsynchronisation
wird `0` gespeichert; es wird kein WLAN-/NTP-Aufruf nur für die Protokollierung
ausgeführt. Die Einträge werden einmal unmittelbar vor Deep Sleep geschrieben.

## Batterie und WLAN

Eine zusammenhängende Unterspannungsphase erzeugt nur beim ersten Auftreten
einen Batterieeintrag, mit der dann festgestellten Schwere. Weitere Messungen
und ein Wechsel zu einer kritischeren Stufe schreiben keinen neuen Batteriefehler.
Erst wenn die Spannung wieder über `LOW_BATTERY_VOLTAGE` liegt, kann eine neue
Unterspannungsphase protokolliert werden. Diese Unterdrückung ist unabhängig
vom vorhandenen NVS-Flag `lowBat`, das die erfolgreich angezeigte Batterie-
Warnseite kennzeichnet. Gescheiterte Displayaktualisierungen bei weiteren
Batterieprüfungen bleiben eigenständige Fehler und werden weiterhin erfasst.

WLAN-Verbindungsfehler erzeugen keinen Fehlereintrag. Das gilt für den
Verbindungsaufbau sowie für Verbindungsabbrüche bei NTP/API-Aufrufen, einschließlich
der vom API-Client als `-512 - wl_status_t` zurückgegebenen Fehler. Bei diesen
Fehlern wird nach **300 Sekunden Deep Sleep** erneut versucht, unabhängig von
Minutenausrichtung und Nachtruhe. Scheitert zusätzlich das Zeichnen der
Fehlerseite, wird ausschließlich der Displayfehler gespeichert; der WLAN-Retry
bleibt bei fünf Minuten. Andere API- und NTP-Fehler nutzen den normalen Zeitplan.

## Serielle Boot-Ausgabe und Tests

Direkt nach `Serial.begin()` werden alle gespeicherten Einträge ausgegeben,
vom ältesten zum neuesten, auch vor Batterieprüfung und Diagnosetests:

```text
RTC error history v1: 2/10 (oldest first)
RTC_ERROR timestamp=0 codes=0x0008 detail=0
RTC_ERROR timestamp=1791600000 codes=0x0110 detail=401
```

`test/host/run_tests.sh` prüft den Ringüberlauf, den Erhalt bei erfolgreichen
Versuchen und simulierten Neustarts aus Deep Sleep, kombinierte Ursachen,
Batterieunterdrückung und Erholung, WLAN-Klassifikation, unbekannte Uhrzeit sowie
die tatsächliche serielle Ausgabe der Firmware-Hilfsfunktionen. Die vorhandenen
Display-Recovery-Tests laufen ebenfalls weiter. Die RTC-Speicherhaltung und der
Wecktimer müssen zusätzlich am ESP32 geprüft werden.
