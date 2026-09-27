# Marquee-Pi

Ein Begleitbildschirm für LaunchBox/Big Box auf Windows 11 und einen Raspberry Pi 3 B+ mit 7-Zoll-Touchdisplay (800 × 480).

> Projektstatus: Pi-Anzeige und Windows-Taskleistenprogramm sind auf dem Zielsystem installiert. Ein realer Spielstart aus Big Box hat das passende Marquee angezeigt und nach Spielende wieder zur Standardanimation gewechselt. Die automatische Pi-Abschaltung wurde bei einem Shutdown über Big Box erfolgreich getestet.

## Funktionen

- Nach dem Einschalten startet die Pi-Anzeige ohne Windows-Verbindung und lädt das dauerhaft auf dem Pi gespeicherte Standardlogo, Video oder die Animation.
- Beim Spielstart sendet das Windows-Programm die Spielkennung und passende Grafiken an den Pi. Das Display zeigt ein Marquee, alternativ Banner oder Logo.
- Ein kurzes Tippen schaltet während des Spiels zwischen Marquee und Control-Panel-Ansicht um. Vier Wischrichtungen (oben nach unten, unten nach oben, links nach rechts, rechts nach links) lassen sich im Windows-Tool unabhängig mit Bildansichten oder dem RetroArch-Menü belegen. Für RetroArch sind ein frei einstellbares Tastaturkürzel und ein lokaler Netzwerkbefehl wählbar. Beim Spielende erscheint wieder das Standardlogo.
- Bei einem Windows-Neustart bleibt der Pi eingeschaltet; ein Verbindungsabbruch setzt nur die Anzeige zurück.
- Ein Windows-Symbol im Infobereich zeigt den Verbindungsstatus und bietet Anzeige neu laden, Pi neu starten, Pi herunterfahren und Standardlogo anzeigen. Über das Windows-Tool lassen sich eigene Standardmedien hochladen, auswählen und verwalten.
- Beim vollständigen Herunterfahren über Big Box erhält der Pi einen Shutdown-Befehl und fährt selbstständig herunter. Der gleiche Ablauf ist für das Windows-Ausschaltmenü vorgesehen. Danach kann die Funksteckdose manuell ausgeschaltet werden.

## Komponenten

| Ordner | Zweck |
| --- | --- |
| `windows/` | Natives C++-Tray-Tool mit Infobereich, Kommunikation und Abschaltsteuerung |
| `launchbox-plugin/` | LaunchBox/Big-Box-Plugin für Spielstart und Spielende |
| `pi/` | Vollbildanzeige, Touchbedienung und lokaler Empfänger |
| `docs/` | Architektur, Protokoll, Installation und Tests |

Das portable Windows-Paket enthält EXE, Plugin-DLL, Anleitung und Lizenz. IP-Adressen, Installationspfade, Zugangsdaten und eigene Medien werden nicht fest eingebaut. Spielgrafiken und LaunchBox-Binärdateien werden nicht mitgeliefert.

## Schnellstart

1. [Raspberry Pi OS Lite installieren und Pi-Dienste einrichten](pi/INSTALL.md). Ein eigenes API-Token erzeugen und in der Pi-Konfiguration hinterlegen.
2. Das portable Windows-Paket aus den GitHub-Releases entpacken. [Windows-Tool einrichten](windows/README.md) und Pi-Adresse sowie denselben Token eintragen.
3. Die enthaltene LaunchBox-Plugin-DLL nach `LaunchBox\Plugins\Marquee-Pi\` kopieren und [LaunchBox/Big Box neu starten](launchbox-plugin/README.md).
4. Ein Spiel starten und die Anzeige prüfen. Für die RetroArch-Menü-Wischgeste den [lokalen Netzwerkmodus und den möglichen Firewall-Dialog](windows/README.md) beachten.

Der automatische Pi-Shutdown wurde bei einem vollständigen Ausschalten über Big Box praktisch geprüft. Das Ausschalten über das normale Windows-Menü wurde nach diesem Fix noch nicht erneut getestet. Pi-Neustart und Pi-Shutdown über das Tray-Menü sind getrennte Funktionen.

## Hardware des ersten Zielsystems

- Windows 11 mit LaunchBox/Big Box und RetroArch
- Raspberry Pi 3 B+
- Raspberry Pi 7-Inch Touch Screen Display, 800 × 480
- Direkte Netzwerkverbindung zwischen Windows und Pi
- Gemeinsame Funksteckdose, nach dem Herunterfahren manuell ausgeschaltet

Andere Pi-Modelle, Displays und Netzwerkadressen erfordern eine passende lokale Konfiguration; getestet wurde bislang die oben genannte Hardware.

## Entwicklung

Die Schnittstelle und die offenen Hardwareprüfungen stehen in [docs/architecture.md](docs/architecture.md). Formate und Upload-Regeln stehen in [docs/media.md](docs/media.md). Start- und Build-Schritte stehen in den README-Dateien der Komponenten; die Pi-Neuinstallation beschreibt [pi/INSTALL.md](pi/INSTALL.md). Der Quellcode steht unter der [MIT-Lizenz](LICENSE).
