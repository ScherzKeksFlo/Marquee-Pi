# Architektur und Abnahmekriterien

## Zuständigkeiten

1. Das Pi-Programm startet beim Booten automatisch, lädt das dauerhaft auf dem Pi gespeicherte Standardmedium ohne Netzwerkverbindung und zeigt bei einem Dateifehler ein lokales Ersatzbild.
2. Ein Windows-Programm stellt Verbindung, Status und das Menü im Infobereich bereit. Es sendet Bilddaten bzw. Befehle an den Pi.
3. Ein LaunchBox/Big-Box-Plugin meldet Spielstart und Spielende an das Windows-Programm. Die Plugin-Schnittstelle stellt Bildtypen wie `Arcade - Marquee` und `Arcade - Controls Information` bereit.
4. Die Windows-Abschaltsteuerung unterscheidet vollständiges Ausschalten von Neustart. Diese Unterscheidung darf nicht allein vom Netzwerkverlust abgeleitet werden.

## Anzeigezustände

| Zustand | Pi-Anzeige | Kurzes Tippen |
| --- | --- | --- |
| Start / kein Spiel | Lokales Standardlogo oder Animation | Keine Änderung |
| Spiel aktiv, Marquee vorhanden | Marquee | Zur Control-Panel-Ansicht wechseln, wenn vorhanden |
| Spiel aktiv, kein Marquee | Banner oder Logo; sonst Standardbild | Zur Control-Panel-Ansicht wechseln, wenn vorhanden |
| Control-Panel-Ansicht | Zum Spiel gehörende Steuerungsgrafik | Zur Spielgrafik zurückkehren |
| Spielende | Standardmedium | Keine Änderung |
| Windows-Verbindung unterbrochen | Nach Ablauf eines Timeouts Standardmedium | Keine Änderung |

Die Grafik wird proportional in 800 × 480 eingepasst. Abschneiden ist standardmäßig deaktiviert. Neue Spielereignisse verwerfen ältere Anzeigezustände. Nach einem erneuten Verbindungsaufbau synchronisiert Windows den aktuellen Zustand.

## Touch-Gesten

Das Pi-Programm erkennt ein kurzes Tippen und vier voneinander getrennte Wischgesten: oben nach unten, unten nach oben, links nach rechts und rechts nach links. Für jede erkannte Geste wird ein Ereignis mit Richtung und aktuellem Anzeigezustand erzeugt. Die Wischgesten werden im Windows-Tool einzeln zugeordnet. Bildaktionen verarbeitet der Pi lokal; RetroArch-Gesten meldet er an die Windows-App. Eine nicht zugeordnete Geste ändert die Anzeige nicht.

Die Erkennung nutzt Beginn, Bewegung und Ende einer einzelnen Berührung. Mindeststrecke, maximale Dauer und zulässige Querbewegung sollen konfigurierbar sein. Eine Wischbewegung darf kein Tippen auslösen; ein kurzer Tipp darf keine Wischbewegung auslösen. Mehrere gleichzeitige Berührungen werden zunächst ignoriert. Details und Abnahmekriterien stehen in [touch.md](touch.md).

## Standardmedium

Das Windows-Tool verwaltet eigene Standardbilder und Animationen, zeigt eine Vorschau und überträgt das ausgewählte Medium zum Pi. Der Pi speichert das aktive Medium dauerhaft lokal, damit es schon vor dem Windows-Start sichtbar ist. Ein fehlgeschlagener Upload darf das bisherige Medium nicht ersetzen. Formate, Grenzen und der persistente Speicherort bei aktiviertem Overlay stehen in [media.md](media.md).

## Pi-Steuerbefehle

- `reload`: Aktives Standardmedium aus dem lokalen Speicher neu laden, ohne Betriebssystem-Neustart.
- `show-default`: Standardmedium anzeigen.
- `reboot`: Pi sauber neu starten.
- `shutdown`: Pi sauber herunterfahren. Wiederanlauf erfordert einen neuen Stromzyklus der Funksteckdose.
- `status`: Erreichbarkeit, Programmversion, aktueller Zustand und aktives Spiel abfragen.

Die konkrete Netzwerk-API steht in [protocol.md](protocol.md). Auf dem getesteten Pi regelt Polkit nur Reboot und Poweroff für den eigenen API-Dienstbenutzer. Sie bleibt auf die direkte Verbindung beschränkt und benötigt Authentifizierung. Lokale Zugangsdaten werden nicht in Git gespeichert.

## Ausschalten

Windows-Startmenü und Big-Box-Menü sollen denselben Vorgang auslösen: Pi herunterfahren, danach Windows herunterfahren, zuletzt die Funksteckdose manuell ausschalten. Bei Windows-Neustart darf kein Pi-Shutdown ausgelöst werden. Ein allgemeines Windows-Shutdown-Skript eignet sich nicht, weil es auch bei Neustarts läuft. Die Tray-App verarbeitet `WM_ENDSESSION` nur bei bestätigtem Sitzungsende ohne Logoff oder App-Neustart. Sie liest dann das jüngste User32-Ereignis 1074 aus dem Systemprotokoll, das seit App-Start und vor höchstens zwei Minuten geschrieben wurde. Nur ein eindeutig erkannter Ausschalt-Typ sendet einen Pi-Shutdown mit drei Sekunden Zeitlimit. Bei Neustart, fehlendem Ereignis oder Lesefehler bleibt der Pi eingeschaltet. Zustellung und Timing sind auf dem Zielrechner zu testen; bis dahin ist der automatische Pi-Shutdown nicht als zuverlässig nachgewiesen.

Ein Pi-Overlay-Dateisystem kann die SD-Karte zusätzlich gegen versehentliches frühes Abschalten schützen. Es ersetzt nicht den geordneten Ausschaltablauf. Lokale Konfiguration und Medien müssen vor Aktivierung des schreibgeschützten Overlays vorbereitet werden.

## Vor Ort zu prüfen

- Pi-OS-Version und 32-/64-Bit-Architektur
- Boottest mit ausgeschaltetem Windows: gespeichertes Standardmedium erscheint ohne Netzwerk; Video oder Animation startet nach Bereitstellung der grafischen Ausgabe
- Tatsächliche Displayausrichtung und Touch-Koordinaten
- Statische IP-Adressen oder feste Namen der direkten Verbindung
- LaunchBox-Installationspfad, Version und vorhandene Bildtypen je Beispielspiel
- Verhalten von Windows-Startmenü und Big-Box-Menü beim Ausschalten und Neustart
- Zeit von Pi-Shutdown-Befehl bis zum sicheren Stillstand
- Ob die Funksteckdose nach vollständigem Windows-Stillstand ausgeschaltet wird

## Community-Paket

Release-Artefakte sollen Windows-App, Plugin, Pi-Installationspaket, Beispieldateien und Prüfsummen enthalten. Weder ROMs noch Spielgrafiken, Zugangsdaten oder LaunchBox-Binärdateien gehören ins Repository oder Release. Installation, Update, Deinstallation und Wiederherstellung nach Verbindungsabbruch müssen dokumentiert und getestet werden.
