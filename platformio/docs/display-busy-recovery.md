# BUSY-Überwachung des GDEY073D46

Das 7,3-Zoll-Display verwendet die lokale Treibervariante
`include/epd/RobustGxEPD2_730c.h` / `src/epd/RobustGxEPD2_730c.cpp`.
Sie basiert auf dem Paneltreiber aus GxEPD2 **1.6.8**, Autor Jean-Marc Zingg,
GPL-3.0. Die SPI-Kommandos, RST-Sequenz (50 ms HIGH, 20 ms LOW) und
Pixelkodierung stammen aus dieser Version. Die BUSY-Überwachung und die
Wiederholungslogik sind Projekterweiterungen. Die installierte Bibliothek unter
`.pio/libdeps` wird nicht verändert. Bei einem GxEPD2-Update muss dieser lokale
Paneltreiber mit der neuen Panelimplementierung verglichen werden.

## Einstellungen

`EPD_BUSY_CONFIG` in `src/config.cpp` enthält alle Einstellungen. **Die Werte
sind Mikrosekunden.** Jedes Fenster ist `{assert_timeout_us, min_us, max_us}`:

- `assert_timeout_us`: Bis wann muss BUSY LOW geworden sein? LOW muss außerdem
  mindestens `glitch_us` lang sein, damit es als Aktivität zählt.
- `min_us`: Mindestdauer einer gültigen BUSY-Phase. Kurze HIGH-Unterbrechungen
  werden dabei überbrückt. Ein kurzer LOW-Puls, der erst spät kommt, kann diese
  Mindestdauer nicht durch die vorherige Wartezeit erfüllen.
- `max_us`: Absolute Frist für die gesamte Phase ab RST-Freigabe bzw. ab dem
  Senden des jeweiligen Kommandos, einschließlich der abschließenden HIGH-Prüfung.
  Dieses Zeitlimit wird durch Pegelwechsel nie neu gestartet.

| Phase | LOW spätestens nach | Mindestdauer BUSY | Absolute Frist |
| --- | ---: | ---: | ---: |
| `_InitDisplay reset` | 20 ms | 2 ms | 40 ms |
| `_PowerOn` | 50 ms | 150 ms | 250 ms |
| `_refresh` | 100 ms | 30 s | 45 s |
| `_PowerOff` | 50 ms | 120 ms | 250 ms |

Die Fenster orientieren sich an den erfolgreichen Messungen vom 10.10.2026:
Reset 16.187 µs, PowerOn 172.140 µs, Refresh 35.058.848 µs und PowerOff
145.098 µs. Sie enthalten Reserve und sind keine zugesicherten Herstellergrenzen.
Die Fenster müssen an das
konkrete Panel angepasst werden, falls es z. B. bei anderer Temperatur länger
braucht.

Beim gemessenen Reset wurde ein vorzeitiges Ende abgefangen (`early ends=1`),
bevor eine gültige BUSY-Phase folgte. Deshalb bleiben die 2 ms Mindestdauer
für die gültige LOW-Phase bestehen: Die gemessenen 16,187 ms sind die gesamte
Reset-Phase einschließlich des frühen Endes und der Wartezeit dazwischen,
nicht die bekannte Dauer eines einzelnen LOW-Pulses.

`glitch_us = 100` zählt und ignoriert LOW- und HIGH-Pulse **unter** 100 µs.
Ein Puls mit genau 100 µs ist kein Glitch. Eine gültige Phase endet erst,
wenn HIGH mindestens 100 µs stabil ist und die Mindestdauer erfüllt wurde.
Eine länger anhaltende HIGH-Phase vor Ablauf der Mindestdauer zählt als
`early ends`. Der Treiber wartet dann innerhalb derselben absoluten Frist auf
eine weitere gültige BUSY-Phase; bloßes Warten bis zur Mindestdauer erzeugt
keinen Erfolg.

`additional_attempts = 2` bedeutet **einen ersten Versuch plus zwei weitere**.
Dieser gemeinsame Zähler gilt für Fehler aller vier Phasen. Es gibt keinen
separaten zusätzlichen Retry-Zähler je Phase. `0` erlaubt genau einen Versuch.

## Ablauf und Diagnose

BUSY-Flanken werden während der überwachten Phasen per ESP32-GPIO-Interrupt
erfasst. Die Überwachung beginnt vor RST-Freigabe bzw. vor dem jeweiligen
SPI-Kommando. Der wartende Task kann damit weiterhin schlafen/yielden, ohne
kurze Pulse grundsätzlich wegen seines 1-ms-Pollintervalls zu verlieren.
Das Programm zählt tatsächlich erfasste Flanken; beliebig kurze Pulse oder
mehrere Flanken während gesperrter Interrupts lassen sich nicht garantiert
erfassen. Eine analoge Signalprüfung kann durch diesen Zähler nicht ersetzt werden.

Am Ende jeder Phase wird z. B. ausgegeben:

```text
EPD attempt 1/3: hardware reset and full initialization
_InitDisplay reset : 16187 us; ok; glitches LOW=0 HIGH=0 (<100 us); early ends=1
_PowerOn : 172140 us; ok; glitches LOW=0 HIGH=0 (<100 us); early ends=0
_refresh : 35058848 us; ok; glitches LOW=0 HIGH=0 (<100 us); early ends=0
_PowerOff : 145098 us; ok; glitches LOW=0 HIGH=0 (<100 us); early ends=0
```

Die Zeiten sind nicht direkt mit den alten GxEPD2-Zahlen vergleichbar:
Die alte Reset-Messung begann erst nach zusätzlichen 2 ms und dem 1-ms-Vorlauf
der Warteschleife. Jetzt beginnt sie vor RST-Freigabe. Außerdem enthält ein
Erfolg die abschließende HIGH-Qualifizierung. Ein Unterschied von wenigen
Mikrosekunden in alten Messungen allein beweist keinen Glitch.

Fehler heißen `no valid BUSY assertion`, `no valid completion in window` oder
`invalid BUSY configuration`. Die Glitch-Zähler werden je Phase ausgegeben,
auch bei einem Fehler. Nach einem Fehler werden keine Folgebefehle für diesen
Versuch gesendet. Der nächste Versuch beginnt mit RST und vollständiger
Initialisierung. Ein Fehler beim Refresh oder PowerOff führt außerdem zur
erneuten Übertragung **aller vier Bildseiten**, da RST den Controller-RAM-Zustand
ungültig macht. Wetterdaten werden für den Versuch wiederverwendet.

Nach ausgeschöpften Versuchen wird die Displayversorgung ausgeschaltet und
der normale Deep-Sleep-Zeitplan verwendet. Der Diagnosetest schläft bei Fehlern
für `SLEEP_DURATION` Minuten. Die aktive LOW-Schaltung von GPIO26/D3 bleibt
Bestandteil der vorhandenen Versorgungsschaltung.

## Prüfung

Die Tests verwenden die echte C++-Filterlogik sowie den lokalen Paneltreiber
mit einer simulierten GPIO/SPI-Grenze. Geprüft werden u. a. alle Pulsbreiten
von 1–99 µs, zu frühe Antworten, ausbleibendes BUSY, feste Deadlines,
`micros()`-Überlauf, RST-Wiederholungen, der gemeinsame Versuchszähler und
das erneute Übertragen aller vier Seiten. Ein zusätzlicher Regressionstest prüft
die gemessenen Gesamtzeiten mit den kalibrierten Fenstern. Die dort simulierten
Reset-Flanken sind ein repräsentatives Beispiel mit einem frühen Ende; ihre
Einzelzeitpunkte wurden nicht am Panel gemessen.

Unter Linux mit `g++`:

```sh
bash test/host/run_tests.sh
```

Im vorhandenen Windows/WSL-Setup:

```powershell
wsl.exe -d Ubuntu-20.04 -- bash /mnt/d/Coding/PlatformIO/Projects/esp32-weather-epd/platformio/test/host/run_tests.sh
```

Firmware bauen und zum Hardwaretest flashen:

```powershell
pio run -e dfrobot_firebeetle2_esp32e
pio run -e dfrobot_firebeetle2_esp32e -t upload --upload-port COM12
pio device monitor -p COM12 -b 115200
```

Der Hardwaretest muss bestätigen, dass gültige Durchläufe weiterhin sichtbar
aktualisieren und fehlerhafte Antworten entweder im Zeitfenster abgefangen
oder als begrenzter Retry erkannt werden. Die Softwareänderung beweist noch
nicht, dass BUSY-Glitches die bisherige Störung verursacht haben.
