# Pi-Anzeigeprogramm

Die vollständige Anleitung für eine frische Raspberry Pi OS Lite-Installation steht in [INSTALL.md](INSTALL.md). Die Trixie-Schritte müssen noch am Zielgerät geprüft werden.

Der Python-Server liefert die lokale Vollbildanzeige und eine token-geschützte API. Die Anzeige lädt ein auf dem Pi gespeichertes Standardbild oder Video, zeigt Spielgrafiken und verarbeitet Tippen sowie vier Wischrichtungen. Für Wischgesten sind noch keine Aktionen zugewiesen.

## Voraussetzungen

- Raspberry Pi OS mit X11, `xinit`, `xset` (`x11-xserver-utils`) und Chromium (am Pi 3 B+ mit Buster/Python 3.7 geprüft)
- Python 3
- `ffprobe` aus FFmpeg für MP4-Uploads
- Ein dauerhaft beschreibbarer Datenordner für das Standardmedium
- Netzwerkverbindung zum Windows-PC für Spielereignisse; zum Starten der Standardanzeige ist sie nicht erforderlich

## Lokal starten

1. `config.example.json` nach `config.json` kopieren und einen zufälligen Token von mindestens 24 Zeichen setzen. Diese Datei nicht in Git aufnehmen.
2. `data_dir` auf einen dauerhaft beschreibbaren Pfad setzen und den Ordner dem Pi-Dienstbenutzer zuordnen.
3. `python3 arcade_pi.py --config config.json` starten.
4. `http://127.0.0.1:8765/ui/` im Browser öffnen. `start-kiosk.sh` startet Chromium im Vollbild.

`arcade-pi-display.service.example` und `arcade-pi-kiosk.service.example` sind Vorlagen für den Systemstart. Benutzername und Pfade müssen zur Pi-Installation passen. Der Kioskdienst startet Xorg auf `tty7` und Chromium ohne Desktop-Sitzung. `start-kiosk.sh` deaktiviert beim X11-Start den Bildschirmschoner und DPMS, damit das Display während des Kioskbetriebs nicht schwarz wird. Auf dem getesteten Buster-System verhindert `chromium-policy.example.json` als `/etc/chromium-browser/policies/managed/arcade-pi-display.json` die Übersetzungsleiste. Bei einem schreibgeschützten Overlay muss der Datenordner auf einer separat beschreibbaren Partition liegen; sonst gehen Uploads beim nächsten Neustart verloren.

## Neustart und Shutdown

Die API-Befehle sind standardmäßig deaktiviert und antworten mit HTTP 503. Auf dem getesteten Buster-Pi laufen sie über `systemctl` und eine Polkit-Regel für den eigenen Dienstbenutzer `arcadepi`; `NoNewPrivileges=true` bleibt aktiv. Die Regelvorlage liegt in `arcade-pi-display.pkla.example`.

Für die Dienstvorlage den Systembenutzer `arcadepi` ohne Login-Shell anlegen, `/var/lib/arcade-pi-display` diesem Benutzer zuordnen und `/etc/arcade-pi-display/config.json` als `root:arcadepi` mit Modus `640` speichern. Die PKLA-Vorlage gehört nur auf Buster nach `/etc/polkit-1/localauthority/50-local.d/arcade-pi-display.pkla`. Auf Trixie gilt die JavaScript-Regel `arcade-pi-display.rules.example` unter `/etc/polkit-1/rules.d/`. Danach beide Dienste mit `systemctl enable --now` aktivieren. `power_commands_enabled` erst nach Installation der Regel, Prüfung mit `pkcheck` und einem Neustarttest aktivieren. Der API-Neustart ist am Gerät geprüft, Shutdown noch nicht, weil danach ein manueller Stromzyklus nötig ist.

## Tests

Vom Repository-Stamm:

```text
PYTHONPATH=pi python3 -m unittest discover -s pi/tests -v
node pi/tests/gesture.test.js
```

Die Windows-PowerShell-Entsprechung für den ersten Befehl ist `$env:PYTHONPATH='pi'; python -m unittest discover -s pi/tests -v`. Weitere Details stehen in [docs/media.md](../docs/media.md) und [docs/touch.md](../docs/touch.md).

## Gerätetest

Am Raspberry Pi 3 B+ mit Raspbian Buster, Python 3.7, Chromium 92 und 800 × 480 Touchdisplay geprüft: API-Tests bestanden, H.264-MP4 als lokales Standardmedium gespeichert, Spielgrafik im Kiosk angezeigt und nach einem Pi-Neustart das Standardvideo automatisch wiedergegeben. Die Windows-Anbindung wurde mit einem realen Big-Box-Spielstart geprüft; das passende Marquee erschien und beim Spielende kehrte die Standardanimation zurück. Touch-Eingaben am realen Display stehen noch aus.
