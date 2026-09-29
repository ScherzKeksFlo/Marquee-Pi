# Installation auf Raspberry Pi OS

Diese Anleitung beschreibt eine Neuinstallation von Marquee-Pi auf Raspberry Pi OS Lite (32 Bit, derzeit Debian 13 „Trixie“). Getestet am 27. September 2026 auf einem Raspberry Pi 3 B+ mit 800 × 480 DSI-Touchdisplay und einer 8-GB-microSD-Karte. Bei späteren OS-Versionen Paketnamen und Polkit-Regeln erneut prüfen.

## Startmedium und Betriebssystem

Ein frisches Startmedium von mindestens 16 GB wird empfohlen. Auf der getesteten 8-GB-Karte blieben nach OS-Update, Chromium, X11, FFmpeg und `apt clean` knapp 3 GB frei. Für weitere Videos und Spielgrafiken ist mehr Platz sinnvoll. Der Pi 3 B+ kann von microSD oder USB-Massenspeicher booten. Schreibe mit [Raspberry Pi Imager](https://www.raspberrypi.com/software/) **Raspberry Pi OS Lite (32-bit)** auf das Startmedium. Das Schreiben löscht alle bisherigen Daten darauf. Richte im Imager Benutzername, SSH-Zugang, WLAN samt Land, Zeitzone und Hostname ein. Für die Anzeige wird keine vollständige Desktop-Edition benötigt. Bewahre das alte Startmedium bis zum erfolgreichen Funktionstest auf.

Raspberry Pi empfiehlt für einen Wechsel der Hauptversion eine [Neuinstallation](https://www.raspberrypi.com/documentation/computers/os.html) statt eines Upgrades im laufenden System. Kopiere `config.txt` und `cmdline.txt` einer alten Buster-Installation nicht blind auf Trixie: Bootpfade und Grafiktreiber haben sich geändert.

## Pakete

Nach dem ersten Start per SSH anmelden und aktualisieren:

```sh
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y python3 ffmpeg fbi curl chromium xserver-xorg xserver-xorg-input-libinput xinit x11-xserver-utils xauth polkitd openssl
sudo apt clean
```

- `python3`: lokaler API-Server ohne zusätzliche Python-Pakete.
- `ffmpeg`: `ffprobe` prüft hochgeladene MP4-Dateien auf H.264.
- `fbi`: zeigt den statischen Boot-Splash im Linux-Framebuffer, bevor X11 bereit ist.
- `curl`: aktiviert bei einem direkten Pi-Shutdown das hinterlegte Shutdown-Medium im lokalen Kiosk.
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

Die API verwendet bewusst einfaches HTTP; der Header mit dem API-Token ist auf der Leitung nicht verschlüsselt. Port 8765 daher weder am Router freigeben noch aus dem Internet erreichbar machen. Für die direkte Verbindung `"bind": "10.0.0.10"` und `"allowed_client_ips": ["10.0.0.1"]` verwenden. Soll die API zusätzlich über WLAN erreichbar sein, kann `bind` auf `0.0.0.0` bleiben, aber `allowed_client_ips` muss alle erlaubten Windows-Adressen ausdrücklich aufzählen. Firewallregeln können den Zugriff zusätzlich auf die direkte Schnittstelle begrenzen.

## Anwendung und Dienste

Die folgenden Befehle im `pi`-Ordner einer lokalen Kopie dieses Repositorys auf dem Pi ausführen. Für die Kiosk-Anmeldung muss ein normaler Benutzer vorhanden sein; hier wird er `pi` genannt. Bei anderem Namen `PI_USER` entsprechend setzen.

```sh
PI_USER=pi
sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin arcadepi
sudo install -d -o root -g root -m 755 /opt/arcade-pi-display
sudo cp -a arcade_pi.py start-kiosk.sh show-shutdown.sh static /opt/arcade-pi-display/
sudo chmod 755 /opt/arcade-pi-display/start-kiosk.sh /opt/arcade-pi-display/show-shutdown.sh
sudo install -o root -g root -m 755 configure-quiet-boot.sh /usr/local/sbin/marquee-pi-configure-quiet-boot
sudo install -d -m 755 /etc/X11/xorg.conf.d /etc/chromium/policies/managed
sudo install -m 644 xorg-modesetting.example.conf /etc/X11/xorg.conf.d/20-arcade-modesetting.conf
sudo install -m 644 chromium-policy.example.json /etc/chromium/policies/managed/arcade-pi-display.json
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
sudo install -o root -g root -m 644 marquee-pi-boot-splash.service.example /etc/systemd/system/marquee-pi-boot-splash.service
sudo install -o root -g root -m 644 marquee-pi-shutdown-animation.service.example /etc/systemd/system/marquee-pi-shutdown-animation.service
sudo systemctl daemon-reload
sudo systemctl enable --now arcade-pi-display.service arcade-pi-kiosk.service marquee-pi-shutdown-animation.service
sudo systemctl enable marquee-pi-boot-splash.service
```

Die Kiosk-Vorlage startet auch auf der Lite-Edition (`multi-user.target`). `start-kiosk.sh` setzt den X11-Bildschirmschoner auf Timeout 0, deaktiviert DPMS und nutzt Software-Rendering, um die GPU des Pi 3 B+ zu entlasten. Die Xorg-Konfiguration wählt nur den `modesetting`-Treiber für DSI und verhindert einen zweiten `fbdev`-Bildschirm. Die Chromium-Richtlinie deaktiviert die Übersetzungsleiste. Prüfen mit:

```sh
systemctl is-active arcade-pi-display arcade-pi-kiosk
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xset q
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xrandr --current
```

## Boot-Splash und stiller Systemstart

Das Windows-Tool lädt den Boot-Splash später über **Medien verwalten… > Als Boot-Splash** hoch. Er muss ein PNG oder JPEG sein und wird als `/var/lib/arcade-pi-display/boot-splash` gespeichert. Bis ein eigenes Bild gewählt wurde, kann beispielsweise ein PNG manuell an diese Stelle kopiert werden:

```sh
sudo install -o arcadepi -g arcadepi -m 640 boot.png /var/lib/arcade-pi-display/boot-splash
```

Anschließend den normalen Konsolentext ausblenden. Das Skript sichert die ursprüngliche Kernel-Befehlszeile einmalig als `/boot/firmware/cmdline.txt.marquee-pi-before-quiet-boot`, prüft vor dem atomaren Ersetzen den erforderlichen `root=`-Eintrag, ergänzt die leisen Startoptionen und deaktiviert den normalen Getty auf tty1. `console=tty1` bleibt bewusst erhalten, damit Kernel-, Boot- und Dateisystemfehler weiterhin sichtbar bleiben; nach einem normalen Start erscheint dort wegen des deaktivierten Gettys kein Login-Prompt:

```sh
sudo marquee-pi-configure-quiet-boot
sudo reboot
```

Die serielle Konsole und tty1 bleiben für frühe Diagnosemeldungen erhalten. Ganz frühe Firmwareausgaben vor dem Linux-Framebuffer sowie tatsächliche Bootfehler können deshalb sichtbar sein. Sobald `/dev/fb0` verfügbar ist, zeigt `marquee-pi-boot-splash.service` das statische Bild; X11 übernimmt danach mit dem Standardmedium.

Quiet Boot rückgängig machen:

```sh
sudo cp -p /boot/firmware/cmdline.txt.marquee-pi-before-quiet-boot /boot/firmware/cmdline.txt
sudo systemctl enable getty@tty1.service
sudo reboot
```

Wenn der Pi nicht mehr startet, die FAT-Bootpartition mit einem Kartenleser unter Windows öffnen und `cmdline.txt.marquee-pi-before-quiet-boot` als `cmdline.txt` zurückkopieren. Die Datei muss eine einzige Zeile bleiben.

## Medien über das Windows-Tool einrichten

Im Windows-Tray **Medien verwalten…** öffnen, eine Datei hinzufügen und eine Rolle wählen:

- **Als Standard:** JPG, PNG, GIF, WebP oder H.264-MP4; erscheint ohne laufendes Spiel.
- **Als Boot-Splash:** PNG oder JPEG; erscheint ab dem nächsten Pi-Start vor dem Kiosk.
- **Als Shutdown-Medium:** JPG, PNG, GIF, WebP oder H.264-MP4; erscheint vor dem Ausschalten.

Ein Shutdown-Video spielt einmal. Der Pi ermittelt die Dauer bereits beim Upload und speichert sie im Medienmanifest. Beim Ausschalten wartet er diese Dauer plus eine Sekunde, mindestens vier und höchstens 30 Sekunden. Wird der Pi direkt per `systemctl poweroff` heruntergefahren, aktiviert `marquee-pi-shutdown-animation.service` die lokale Anzeige, bevor API und Kiosk beendet werden. Bei einem Neustart wird die Shutdown-Animation übersprungen. Beim Shutdown aus dem Windows-Tool oder während des Windows-Shutdowns wechselt die API bereits vor dem eigentlichen Poweroff auf das Medium.

## Display testen und zurücksetzen

Das Hilfsskript `marquee-display-test.sh` kann als `/usr/local/sbin/marquee-display-test` installiert werden. Es benötigt für `blink` und `reset` Root-Rechte:

```sh
sudo install -o root -g root -m 755 marquee-display-test.sh /usr/local/sbin/marquee-display-test
marquee-display-test status
sudo marquee-display-test blink
sudo marquee-display-test reset
```

`status` zeigt DSI-, Touch-, X11- und Dienststatus. `blink` schaltet ein erkanntes Display drei Sekunden aus und wieder ein. `reset` setzt einen vorhandenen DSI-Ausgang und den Kiosk zurück. Fehlt DSI vollständig, führt `reset` einmalig einen Warmstart des Pi aus; die Displaystromversorgung muss dabei eingeschaltet bleiben.

## Schwarzes DSI-Display auf dem Pi 3 B+

Fehlt nach einem Kaltstart `card0-DSI-1` und erscheint stattdessen nur `Composite-1`, war die Displayplatine bei der frühen Firmwareerkennung möglicherweise noch nicht bereit. Auf dem getesteten Pi wurde das 7-Zoll-Display über einen separat versorgten USB-Hub gespeist. Ein unveränderter FKMS-Warmstart erkannte das Display zuverlässig.

Als dauerhafte Lösung die vorhandene `/boot/firmware/config.txt` sichern und `bootcode_delay=10` direkt in diese Datei eintragen:

```ini
bootcode_delay=10
display_auto_detect=1
dtoverlay=vc4-fkms-v3d
#disable_fw_kms_setup=1
```

`bootcode_delay` gibt der Displayplatine vor der Erkennung zehn Sekunden zusätzliche Startzeit. Fünf Sekunden bestanden zunächst drei Kaltstarts, später trat die Nichterkennung jedoch erneut auf. Deshalb verwendet das Zielsystem inzwischen zehn Sekunden.

Wenn FKMS trotz erkanntem DSI den 720×480-Composite-Ausgang als primären Framebuffer verwendet, die aktuelle Kernel-Befehlszeile zuerst sichern. Anschließend in derselben einzelnen Zeile von `/boot/firmware/cmdline.txt` ergänzen:

```text
video=Composite-1:d video=DSI-1:800x480@60
```

Vor dem Neustart prüfen, dass die Zeile weiterhin den vorhandenen `root=`-Parameter enthält. Auf dem Zielsystem führte diese Einstellung zu `DSI-1 connected primary 800x480` und `Composite-1 disconnected`. Das Quiet-Boot-Skript behält beide `video=`-Parameter bei.

Status prüfen mit:

```sh
ls /sys/class/drm/
ls /dev/input/
marquee-display-test status
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xrandr --current
```

Im Fehlerfall kann `sudo marquee-display-test reset` als Rückfalllösung verwendet werden. Fehlt DSI vollständig, startet das Skript den Pi einmal warm neu, während die Displayplatine am eingeschalteten Hub versorgt bleibt.

Ein Wechsel auf vollständiges KMS war auf dem Testgerät keine Lösung. Die feste Konfiguration mit `vc4-kms-v3d`, `vc4-kms-dsi-7inch`, `ignore_lcd=1` und `disable_touchscreen=1` erkannte zwar DSI und die Backlight-Schnittstelle, das reale Display blieb jedoch schwarz. Kernelmeldungen zeigten I/O-Fehler beim Aktivieren der Hintergrundbeleuchtung und beim Touchcontroller. FKMS mit `bootcode_delay=10` und der festen DSI-Auswahl bleibt daher die getestete Konfiguration für dieses Gerät. Andere Pi-Modelle und Displayvarianten können KMS benötigen.

## Standardvideo auf einem Pi 3 B+

Für MP4-Videos H.264 mit `yuv420p` verwenden und die Bildgröße möglichst an das 800 × 480-Display anpassen. Ein hochgeladenes 1254 × 1254-H.264-Video ließ sich auf dem Testgerät nicht zuverlässig im Chromium-Kiosk abspielen; eine 480 × 480-Version lief. Beispiel für eine quadratische Vorlage:

```sh
ffmpeg -i eingabe.mp4 -vf "scale=480:480:flags=lanczos" -c:v libx264 -preset veryfast -profile:v baseline -level 3.0 -pix_fmt yuv420p -crf 23 -an ausgabe.mp4
```

Die Datei `ausgabe.mp4` über das Windows-Tool als Standardmedium hochladen. Das Original außerhalb des Pi-Datenordners aufbewahren.

## Neustart und Ausschalten per Windows-Tool

Trixie verwendet JavaScript-Regeln unter `/etc/polkit-1/rules.d`; die alte Buster-Datei `arcade-pi-display.pkla.example` gilt hier nicht. Die Vorlage erlaubt nur dem Dienstbenutzer `arcadepi` die vier nötigen login1-Aktionen.

```sh
sudo install -o root -g root -m 644 arcade-pi-display.rules.example /etc/polkit-1/rules.d/50-arcade-pi-display.rules
PID=$(systemctl show -p MainPID --value arcade-pi-display)
START_TIME=$(python3 -c 'import sys; print(open(sys.argv[1]).read().rsplit(")", 1)[1].split()[19])' "/proc/$PID/stat")
PROCESS="$PID,$START_TIME,$(id -u arcadepi)"
sudo -u arcadepi pkcheck --action-id org.freedesktop.login1.reboot --process "$PROCESS"
sudo -u arcadepi pkcheck --action-id org.freedesktop.login1.power-off --process "$PROCESS"
```

Die Prüfung muss für den Dienstbenutzer erfolgen; beide `pkcheck`-Aufrufe müssen erfolgreich sein. Erst danach `power_commands_enabled` in `config.json` auf `true` setzen und den API-Dienst neu starten. Den API-Neustart zuerst prüfen. Der Shutdown-Test kommt zuletzt, weil der Pi danach erst durch einen neuen Stromzyklus wieder startet. `NoNewPrivileges=true` in der Dienstdatei bleibt aktiv.

## Funktionstest

1. Im Windows-Tool ein statisches Boot-Bild hochladen. Ohne Windows-Verbindung neu booten: Boot-Splash und danach das gespeicherte Standardbild oder Video erscheinen automatisch; dazwischen wird kein normaler Konsolentext gezeigt.
2. Prüfen, dass `xset q` `timeout: 0` und `DPMS is Disabled` meldet, `xrandr` `DSI-1 connected 800x480` anzeigt und die Anzeige mindestens zehn Minuten sichtbar bleibt.
3. Windows-Tool mit Pi-IP und Token verbinden. Spiel in LaunchBox/Big Box starten: passendes Marquee erscheint.
4. Auf das Display tippen: Bei vorhandener Steuerungsgrafik zwischen Marquee und Control Panel wechseln. Die vier Wischgesten werden im Windows-Tool konfiguriert; die gewählte Bildansicht erscheint sofort.
5. Spiel verlassen: Standardmedium erscheint. Pi-Neustart über das Tray-Menü testen; Medium erscheint nach dem Booten erneut.
6. Ein Shutdown-Medium hochladen. Pi-Shutdown über das Tray-Menü erst nach allen anderen Tests durchführen: Medium erscheint vollständig beziehungsweise höchstens 30 Sekunden, anschließend ist der Pi per Ping/SSH nicht mehr erreichbar.

Bei Fehlern `journalctl -u arcade-pi-display -u arcade-pi-kiosk -b --no-pager` lesen. Für eine Community-Installation eigene IP-Adressen, Displayausrichtung, Benutzername und Standardmedium anpassen.
