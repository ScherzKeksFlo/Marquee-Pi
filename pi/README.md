# Pi-Anzeigeprogramm

Erster lauffähiger Stand für Raspberry Pi OS mit Python 3 und Chromium. Der Python-Server liefert die lokale Vollbildanzeige und eine token-geschützte API. Die Anzeige lädt ein auf dem Pi gespeichertes Standardbild oder Video, zeigt Spielgrafiken und verarbeitet Tippen sowie vier Wischrichtungen. Für Wischgesten sind noch keine Aktionen zugewiesen.

## Voraussetzungen

- Raspberry Pi OS mit grafischer Sitzung und Chromium
- Python 3
- `ffprobe` aus FFmpeg für MP4-Uploads
- Ein dauerhaft beschreibbarer Datenordner für das Standardmedium
- Netzwerkverbindung zum Windows-PC für Spielereignisse; zum Starten der Standardanzeige ist sie nicht erforderlich

## Lokal starten

1. `config.example.json` nach `config.json` kopieren und einen zufälligen Token von mindestens 24 Zeichen setzen. Diese Datei nicht in Git aufnehmen.
2. `data_dir` auf einen dauerhaft beschreibbaren Pfad setzen und den Ordner dem Pi-Benutzer zuordnen.
3. `python3 arcade_pi.py --config config.json` starten.
4. `http://127.0.0.1:8765/ui/` im Browser öffnen. `start-kiosk.sh` startet Chromium im Vollbild.

`arcade-pi-display.service.example` ist eine Vorlage für den Systemstart. Benutzername und Pfade müssen zur Pi-Installation passen. Die grafische Sitzung muss Chromium automatisch starten. Bei einem schreibgeschützten Overlay muss der Datenordner auf einer separat beschreibbaren Partition liegen; sonst gehen Uploads beim nächsten Neustart verloren.

Die API-Befehle zum Neustarten und Herunterfahren sind standardmäßig deaktiviert und antworten mit HTTP 503. Der vorhandene Befehlsweg nutzt `sudo -n systemctl reboot` bzw. `poweroff`; die Dienstvorlage setzt `NoNewPrivileges=true`, weshalb dieser Weg dort noch nicht funktionsfähig ist. `power_commands_enabled` erst nach Einrichtung und Prüfung einer eng begrenzten Rechtevergabe aktivieren.

## Tests

Vom Repository-Stamm:

```text
PYTHONPATH=pi python3 -m unittest discover -s pi/tests -v
node pi/tests/gesture.test.js
```

Die Windows-PowerShell-Entsprechung für den ersten Befehl ist `$env:PYTHONPATH='pi'; python -m unittest discover -s pi/tests -v`. Weitere Details stehen in [docs/media.md](../docs/media.md) und [docs/touch.md](../docs/touch.md).
