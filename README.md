# Arcade Pi Display

Ein geplanter Begleitbildschirm für LaunchBox/Big Box auf Windows 11 und einen Raspberry Pi 3 B+ mit 7-Zoll-Touchdisplay (800 × 480).

> Projektstatus: Architektur und Git-Grundgerüst. Die Programme sind noch nicht implementiert oder am Arcade-System getestet.

## Geplantes Verhalten

- Nach dem Einschalten zeigt der Pi ein lokal gespeichertes Standardlogo oder eine Animation.
- Beim Spielstart sendet das Windows-Programm die Spielkennung und passende Grafiken an den Pi. Das Display zeigt ein Marquee, alternativ Banner oder Logo.
- Ein kurzes Tippen schaltet während des Spiels zwischen Marquee und Control-Panel-Ansicht um. Vier Wischrichtungen (oben nach unten, unten nach oben, links nach rechts, rechts nach links) werden als eigene Gesten erkannt; ihre Aktionen legen wir später fest. Beim Spielende erscheint wieder das Standardlogo.
- Bei einem Windows-Neustart bleibt der Pi eingeschaltet; ein Verbindungsabbruch setzt nur die Anzeige zurück.
- Ein Windows-Symbol im Infobereich zeigt den Verbindungsstatus und bietet Anzeige neu laden, Pi neu starten, Pi herunterfahren und Standardlogo anzeigen. Über das Windows-Tool lassen sich eigene Standardmedien hochladen, auswählen und verwalten.
- Beim vollständigen Herunterfahren über Windows oder Big Box soll der Pi einen Shutdown-Befehl erhalten. Danach kann die Funksteckdose manuell ausgeschaltet werden.

## Komponenten

| Ordner | Zweck |
| --- | --- |
| `windows/` | Windows-Begleitprogramm mit Infobereich, Kommunikation und Abschaltsteuerung |
| `launchbox-plugin/` | LaunchBox/Big-Box-Plugin für Spielstart und Spielende |
| `pi/` | Vollbildanzeige, Touchbedienung und lokaler Empfänger |
| `docs/` | Architektur, Protokoll, Installation und Tests |

Die Release-Pakete sollen Konfigurationsbeispiele enthalten. IP-Adressen, Installationspfade, Zugangsdaten und eigene Medien werden nicht fest eingebaut. Spielgrafiken und LaunchBox-Binärdateien werden nicht mitgeliefert.

## Hardware des ersten Zielsystems

- Windows 11 mit LaunchBox/Big Box und RetroArch
- Raspberry Pi 3 B+
- Raspberry Pi 7-Inch Touch Screen Display, 800 × 480
- Direkte Netzwerkverbindung zwischen Windows und Pi
- Gemeinsame Funksteckdose, nach dem Herunterfahren manuell ausgeschaltet

Die spätere Community-Version soll Konfiguration für andere Pi-Modelle, Displays und Netzwerkadressen erlauben.

## Entwicklung

Die geplante Schnittstelle und die noch offenen Hardwareprüfungen stehen in [docs/architecture.md](docs/architecture.md). Die Formate und Upload-Regeln stehen in [docs/media.md](docs/media.md). Beiträge und Build-Artefakte werden erst nach Implementation und Tests dokumentiert. Der Quellcode steht unter der [MIT-Lizenz](LICENSE).
