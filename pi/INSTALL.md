# Installation auf Raspberry Pi OS

Diese Anleitung beschreibt eine Neuinstallation von Arcade Pi Display auf Raspberry Pi OS Lite (32 Bit, derzeit Debian 13 „Trixie“). Getestet am 27. September 2026 auf einem Raspberry Pi 3 B+ mit 800 × 480 DSI-Touchdisplay und einer 8-GB-microSD-Karte. Bei späteren OS-Versionen Paketnamen und Polkit-Regeln erneut prüfen.

## Startmedium und Betriebssystem

Ein frisches Startmedium von mindestens 16 GB wird empfohlen. Auf der getesteten 8-GB-Karte blieben nach OS-Update, Chromium, X11, FFmpeg und `apt clean` knapp 3 GB frei. Für weitere Videos und Spielgrafiken ist mehr Platz sinnvoll. Der Pi 3 B+ kann von microSD oder USB-Massenspeicher booten. Schreibe mit [Raspberry Pi Imager](https://www.raspberrypi.com/software/) **Raspberry Pi OS Lite (32-bit)** auf das Startmedium. Das Schreiben löscht alle bisherigen Daten darauf. Richte im Imager Benutzername, SSH-Zugang, WLAN samt Land, Zeitzone und Hostname ein. Für die Anzeige wird keine vollständige Desktop-Edition benötigt. Bewahre das alte Startmedium bis zum erfolgreichen Funktionstest auf.

Raspberry Pi empfiehlt für einen Wechsel der Hauptversion eine [Neuinstallation](https://www.raspberrypi.com/documentation/computers/os.html) statt eines Upgrades im laufenden System. Kopiere `config.txt` und `cmdline.txt` einer alten Buster-Installation nicht blind auf Trixie: Bootpfade und Grafiktreiber haben sich geändert.

## Pakete

Nach dem ersten Start per SSH anmelden und aktualisieren:

```sh
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y python3 ffmpeg chromium xserver-xorg xserver-xorg-input-libinput xinit x11-xserver-utils xauth polkitd openssl
sudo apt clean
```

- `python3`: lokaler API-Server ohne zusätzliche Python-Pakete.
- `ffmpeg`: `ffprobe` prüft hochgeladene MP4-Dateien auf H.264.
- `chromium`: Vollbildanzeige der lokalen Webseite.
- `xserver-xorg`, `xserver-xorg-input-libinput`, `xinit`, `xauth`: X11-Sitzung und Touch-Eingaben. Der getestete ft5x06-Touchscreen wurde über libinput erkannt.
- `x11-xserver-utils`: `xset` schaltet Bildschirmschoner und DPMS ab.
- `polkitd`: eng begrenzte Berechtigung für Pi-Neustart und Shutdown.
- `openssl`: zufälligen API-Token erzeugen.

Falls Xorg unter dem Kiosk-Benutzer nicht startet, zunächst `journalctl -u arcade-pi-kiosk -b` prüfen. Auf manchen Installationen ist zusätzlich `xserver-xorg-legacy` nötig; erst nach einem entsprechenden Fehler installieren und konfigurieren.

## Netzwerk

Der Pi benötigt zum Starten des Standardmediums kein Netzwerk. Für Spielgrafiken muss Windows die Pi-API auf TCP-Port 8765 erreichen. Bei direkter Ethernet-Verbindung können beispielsweise `10.0.0.1/24` für Windows und `10.0.0.10/24` für den Pi verwendet werden; es darf auf dieser Verbindung nur **eine** passende IP-Konfiguration aktiv sein. Unter aktuellem Raspberry Pi OS verwaltet [NetworkManager](https://www.raspberrypi.com/documentation/configuration/) die Verbindungen. Das aktive Ethernet-Profil mit `nmcli -g GENERAL.CONNECTION device show eth0` ermitteln und dann anpassen:

```sh
sudo nmcli connection modify '<ETHERNET-PROFIL>' ipv4.method manual ipv4.addresses 10.0.0.10/24 ipv4.never-default yes
sudo nmcli connection up '<ETHERNET-PROFIL>'
```

Bei anderer Netzstruktur die Adressen entsprechend ändern. WLAN kann parallel für Updates und SSH verwendet werden. Die API-Zugriffsliste in `config.json` sollte nur die Windows-IP enthalten.

## Anwendung und Dienste

Die folgenden Befehle im `pi`-Ordner einer lokalen Kopie dieses Repositorys auf dem Pi ausführen. Für die Kiosk-Anmeldung muss ein normaler Benutzer vorhanden sein; hier wird er `pi` genannt. Bei anderem Namen `PI_USER` entsprechend setzen.

```sh
PI_USER=pi
sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin arcadepi
sudo install -d -o root -g root -m 755 /opt/arcade-pi-display
sudo cp -a arcade_pi.py start-kiosk.sh static /opt/arcade-pi-display/
sudo chmod 755 /opt/arcade-pi-display/start-kiosk.sh
sudo install -d -o arcadepi -g arcadepi -m 750 /var/lib/arcade-pi-display
sudo install -d -o root -g arcadepi -m 750 /etc/arcade-pi-display
sudo install -o root -g arcadepi -m 640 config.example.json /etc/arcade-pi-display/config.json
openssl rand -hex 32
```

Den ausgegebenen Token in `/etc/arcade-pi-display/config.json` eintragen (`sudo nano ...`). `allowed_client_ips` auf die Windows-IP setzen, etwa `["10.0.0.1"]`; `power_commands_enabled` vorerst `false` lassen. Token, persönliche Medien und lokale Konfiguration gehören nicht ins Git-Repository.

```sh
sudo install -o root -g root -m 644 arcade-pi-display.service.example /etc/systemd/system/arcade-pi-display.service
sed "s/REPLACE_WITH_PI_USER/$PI_USER/g" arcade-pi-kiosk.service.example | sudo tee /etc/systemd/system/arcade-pi-kiosk.service >/dev/null
sudo chmod 644 /etc/systemd/system/arcade-pi-kiosk.service
sudo systemctl daemon-reload
sudo systemctl enable --now arcade-pi-display.service arcade-pi-kiosk.service
```

Die Kiosk-Vorlage startet auch auf der Lite-Edition (`multi-user.target`). `start-kiosk.sh` setzt den X11-Bildschirmschoner auf Timeout 0, deaktiviert DPMS und unterdrückt die Chromium-Übersetzungsleiste. Prüfen mit:

```sh
systemctl is-active arcade-pi-display arcade-pi-kiosk
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xset q
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xrandr --current
```

## Neustart und Ausschalten per Windows-Tool

Trixie verwendet JavaScript-Regeln unter `/etc/polkit-1/rules.d`; die alte Buster-Datei `arcade-pi-display.pkla.example` gilt hier nicht. Die Vorlage erlaubt nur dem Dienstbenutzer `arcadepi` die vier nötigen login1-Aktionen.

```sh
sudo install -o root -g root -m 644 arcade-pi-display.rules.example /etc/polkit-1/rules.d/50-arcade-pi-display.rules
PID=$(systemctl show -p MainPID --value arcade-pi-display)
sudo -u arcadepi pkcheck --action-id org.freedesktop.login1.reboot --process "$PID"
sudo -u arcadepi pkcheck --action-id org.freedesktop.login1.power-off --process "$PID"
```

Die Prüfung muss für den Dienstbenutzer erfolgen; beide `pkcheck`-Aufrufe müssen erfolgreich sein. Erst danach `power_commands_enabled` in `config.json` auf `true` setzen und den API-Dienst neu starten. Den API-Neustart zuerst prüfen. Der Shutdown-Test kommt zuletzt, weil der Pi danach erst durch einen neuen Stromzyklus wieder startet. `NoNewPrivileges=true` in der Dienstdatei bleibt aktiv.

## Funktionstest

1. Ohne Windows-Verbindung neu booten: gespeichertes Standardbild oder Video erscheint automatisch.
2. Prüfen, dass `xset q` `timeout: 0` und `DPMS is Disabled` meldet, `xrandr` `DSI-1 connected 800x480` anzeigt und die Anzeige mindestens zehn Minuten sichtbar bleibt.
3. Windows-Tool mit Pi-IP und Token verbinden. Spiel in LaunchBox/Big Box starten: passendes Marquee erscheint.
4. Auf das Display tippen: Bei vorhandener Steuerungsgrafik zwischen Marquee und Control Panel wechseln. Wischgesten werden erkannt, haben derzeit noch keine Aktion.
5. Spiel verlassen: Standardmedium erscheint. Pi-Neustart über das Tray-Menü testen; Medium erscheint nach dem Booten erneut.
6. Pi-Shutdown über das Tray-Menü erst nach allen anderen Tests durchführen.

Bei Fehlern `journalctl -u arcade-pi-display -u arcade-pi-kiosk -b --no-pager` lesen. Für eine Community-Installation eigene IP-Adressen, Displayausrichtung, Benutzername und Standardmedium anpassen.
