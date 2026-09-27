# Pi-Anzeigeprogramm

Erster lauffähiger Stand für Raspberry Pi OS mit Python 3 und Chromium. Der Python-Server liefert die lokale Vollbildanzeige und eine token-geschützte API. Die Anzeige lädt ein auf dem Pi gespeichertes Standardbild oder Video, zeigt Spielgrafiken und verarbeitet Tippen sowie vier Wischrichtungen. Für Wischgesten sind noch keine Aktionen zugewiesen.

## Voraussetzungen

- Raspberry Pi OS mit X11, `xinit` und Chromium (am Pi 3 B+ mit Buster/Python 3.7 geprüft)
- Python 3
- `ffprobe` aus FFmpeg für MP4-Uploads
- Ein dauerhaft beschreibbarer Datenordner für das Standardmedium
- Netzwerkverbindung zum Windows-PC für Spielereignisse; zum Starten der Standardanzeige ist sie nicht erforderlich

## Lokal starten

1. `config.example.json` nach `config.json` kopieren und einen zufälligen Token von mindestens 24 Zeichen setzen. Diese Datei nicht in Git aufnehmen.
2. `data_dir` auf einen dauerhaft beschreibbaren Pfad setzen und den Ordner dem Pi-Benutzer zuordnen.
3. `python3 arcade_pi.py --config config.json` starten.
4. `http://127.0.0.1:8765/ui/` im Browser öffnen. `start-kiosk.sh` startet Chromium im Vollbild.

`arcade-pi-display.service.example` und `arcade-pi-kiosk.service.example` sind Vorlagen für den Systemstart. Benutzername und Pfade müssen zur Pi-Installation passen. Der Kioskdienst startet Xorg auf `tty7` und Chromium ohne Desktop-Sitzung. Auf dem getesteten Buster-System verhindert `chromium-policy.example.json` als `/etc/chromium-browser/policies/managed/arcade-pi-display.json` die Übersetzungsleiste. Bei einem schreibgeschützten Overlay muss der Datenordner auf einer separat beschreibbaren Partition liegen; sonst gehen Uploads beim nächsten Neustart verloren.

Die API-Befehle zum Neustarten und Herunterfahren sind standardmäßig deaktiviert und antworten mit HTTP 503. Der vorhandene Befehlsweg nutzt `sudo -n systemctl reboot` bzw. `poweroff`; die Dienstvorlage setzt `NoNewPrivileges=true`, weshalb dieser Weg dort noch nicht funktionsfähig ist. `power_commands_enabled` erst nach Einrichtung und Prüfung einer eng begrenzten Rechtevergabe aktivieren.

## Tests

Vom Repository-Stamm:

```text
PYTHONPATH=pi python3 -m unittest discover -s pi/tests -v
node pi/tests/gesture.test.js
```

Die Windows-PowerShell-Entsprechung für den ersten Befehl ist `$env:PYTHONPATH='pi'; python -m unittest discover -s pi/tests -v`. Weitere Details stehen in [docs/media.md](../docs/media.md) und [docs/touch.md](../docs/touch.md).

## Gerätetest

Am Raspberry Pi 3 B+ mit Raspbian Buster, Python 3.7, Chromium 92 und 800 × 480 Touchdisplay geprüft: API-Tests bestanden, H.264-MP4 als lokales Standardmedium gespeichert, Spielgrafik im Kiosk angezeigt und nach einem Pi-Neustart das Standardvideo automatisch wiedergegeben. Touch-Eingaben am realen Display und die Windows-Anbindung stehen noch aus.
