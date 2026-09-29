# Pi-Anzeigeprogramm

Die vollständige Anleitung für eine frische Raspberry Pi OS Lite-Installation steht in [INSTALL.md](INSTALL.md). Die Trixie-Installation und Anzeige wurden auf einem Pi 3 B+ mit 7-Zoll-DSI-Display geprüft. Bei schwarzem DSI-Bild auf Kernel 6.18 beschreibt INSTALL.md den getesteten FKMS-Workaround.

Der Python-Server liefert die lokale Vollbildanzeige und eine token-geschützte API. Die Anzeige lädt ein auf dem Pi gespeichertes Standardbild oder Video, zeigt Spielgrafiken und verarbeitet Tippen sowie vier Wischrichtungen. Boot-Splash und Shutdown-Medium werden ebenfalls über das Windows-Tool hochgeladen und dauerhaft auf dem Pi gespeichert.

## Touchmenü

Ein Overlay-Menü direkt am Display, standardmäßig per langem Drücken (ca. 0,8 s, Finger ruhig halten) zu öffnen. Es bietet die Ansichten Marquee, Box Art, Logo, Controls und Standard, die Helligkeit in 10-%-Schritten (mindestens 5 %, bleibt nach einem Neustart erhalten) und eine Statusanzeige (Verbindung zum Arcade-PC, IP-Adressen, Spiel, Version). Neustart und Herunterfahren liegen mit Bestätigung auf einer eigenen Seite hinter dem Button **System …**. Welche Geste das Menü öffnet, legt die Aktion `touch_menu` fest: Sie lässt sich jeder der fünf Gesten `long-press`, `swipe-down`, `swipe-up`, `swipe-right` und `swipe-left` zuordnen (Windows-Tool, Tab **Gesten**, oder `POST /v1/gesture-config`). Ohne Konfiguration bleibt `long-press` auf `touch_menu`; wird `long-press` auf `none` gesetzt, löst langes Drücken nichts aus. Nach 20 s ohne Eingabe schließt sich das Menü von selbst. Die zugehörigen Endpunkte `/ui/system`, `/ui/brightness` und `/ui/power` sind wie alle `/ui/`-Pfade nur von `127.0.0.1` aus erreichbar. Neustart und Herunterfahren nutzen dieselbe Polkit-Prüfung wie die API und erfordern `power_commands_enabled`. Für die Helligkeit braucht der Dienst Schreibzugriff auf `/sys/class/backlight/*/brightness`; die Service-Vorlage gibt dafür die Gruppe `video` und den Pfad `/sys/devices/platform/rpi_backlight` frei.

## Voraussetzungen

- Raspberry Pi OS Lite mit X11, `xinit`, `xset` (`x11-xserver-utils`) und Chromium (am Pi 3 B+ mit Trixie/Python 3.13 geprüft)
- Python 3
- `ffprobe` aus FFmpeg für MP4-Uploads
- `fbi` für das statische Bild vor dem X11-Kiosk
- `curl` für die lokale Shutdown-Anzeige
- Ein dauerhaft beschreibbarer Datenordner für das Standardmedium
- Netzwerkverbindung zum Windows-PC für Spielereignisse; zum Starten der Standardanzeige ist sie nicht erforderlich

## Lokal starten

1. `config.example.json` nach `config.json` kopieren und einen zufälligen Token von mindestens 24 Zeichen setzen. Diese Datei nicht in Git aufnehmen.
2. `data_dir` auf einen dauerhaft beschreibbaren Pfad setzen und den Ordner dem Pi-Dienstbenutzer zuordnen.
3. `python3 arcade_pi.py --config config.json` starten.
4. `http://127.0.0.1:8765/ui/` im Browser öffnen. `start-kiosk.sh` startet Chromium im Vollbild.

`arcade-pi-display.service.example`, `arcade-pi-kiosk.service.example`, `marquee-pi-boot-splash.service.example` und `marquee-pi-shutdown-animation.service.example` sind Vorlagen für den Systemstart. Benutzername und Pfade müssen zur Pi-Installation passen. Der Kioskdienst startet Xorg auf `tty7` und Chromium ohne Desktop-Sitzung. `start-kiosk.sh` deaktiviert beim X11-Start den Bildschirmschoner und DPMS. Der Boot-Dienst zeigt das hochgeladene PNG/JPEG auf `tty1`; der Shutdown-Dienst zeigt das hinterlegte Medium, solange API und Kiosk noch laufen. Die aktuelle Trixie-Installation verwendet die Chromium-Richtlinie unter `/etc/chromium/policies/managed/`.

## Neustart und Shutdown

Die API-Befehle sind standardmäßig deaktiviert und antworten mit HTTP 503. Auf Trixie laufen sie über `systemctl` und eine Polkit-Regel für den eigenen Dienstbenutzer `arcadepi`; `NoNewPrivileges=true` bleibt aktiv.

Für die Dienstvorlage den Systembenutzer `arcadepi` ohne Login-Shell anlegen, `/var/lib/arcade-pi-display` diesem Benutzer zuordnen und `/etc/arcade-pi-display/config.json` als `root:arcadepi` mit Modus `640` speichern. Die PKLA-Vorlage gehört nur auf Buster nach `/etc/polkit-1/localauthority/50-local.d/arcade-pi-display.pkla`. Auf Trixie gilt die JavaScript-Regel `arcade-pi-display.rules.example` unter `/etc/polkit-1/rules.d/`. Danach die vier in INSTALL.md beschriebenen Dienste aktivieren. `power_commands_enabled` erst nach Installation der Regel, Prüfung mit `pkcheck` und einem Neustarttest aktivieren. API-Neustart und Shutdown sind am Zielgerät geprüft.

## Tests

Vom Repository-Stamm:

```text
PYTHONPATH=pi python3 -m unittest discover -s pi/tests -v
node pi/tests/gesture.test.js
```

Die Windows-PowerShell-Entsprechung für den ersten Befehl ist `$env:PYTHONPATH='pi'; python -m unittest discover -s pi/tests -v`. Weitere Details stehen in [docs/media.md](../docs/media.md) und [docs/touch.md](../docs/touch.md).

## Gerätetest

Am Raspberry Pi 3 B+ mit Raspberry Pi OS Trixie, Kernel 6.18, Chromium und 800 × 480 Touchdisplay geprüft: API-Tests, Touchgesten, H.264-Standardvideo, reale Big-Box-Spielwechsel, RetroArch-Menüaufruf sowie Shutdown über Big Box und das Windows-Startmenü funktionieren. Das DSI-Display wurde mit `bootcode_delay=5` in drei aufeinanderfolgenden Kaltstarts erkannt.
