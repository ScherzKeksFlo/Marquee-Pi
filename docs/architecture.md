# Architektur und Abnahmekriterien

## Zuständigkeiten

1. Das Pi-Programm startet automatisch und zeigt ohne Windows-Verbindung ein lokales Standardmedium.
2. Ein Windows-Programm stellt Verbindung, Status und das Menü im Infobereich bereit. Es sendet Bilddaten bzw. Befehle an den Pi.
3. Ein LaunchBox/Big-Box-Plugin meldet Spielstart und Spielende an das Windows-Programm. Die Plugin-Schnittstelle stellt Bildtypen wie `Arcade - Marquee` und `Arcade - Controls Information` bereit.
4. Die Windows-Abschaltsteuerung unterscheidet vollständiges Ausschalten von Neustart. Diese Unterscheidung darf nicht allein vom Netzwerkverlust abgeleitet werden.

## Anzeigezustände

| Zustand | Pi-Anzeige | Berührung |
| --- | --- | --- |
| Start / kein Spiel | Lokales Standardlogo oder Animation | Keine Änderung |
| Spiel aktiv, Marquee vorhanden | Marquee | Zur Control-Panel-Ansicht wechseln, wenn vorhanden |
| Spiel aktiv, kein Marquee | Banner oder Logo; sonst Standardbild | Zur Control-Panel-Ansicht wechseln, wenn vorhanden |
| Control-Panel-Ansicht | Zum Spiel gehörende Steuerungsgrafik | Zur Spielgrafik zurückkehren |
| Spielende | Standardmedium | Keine Änderung |
| Windows-Verbindung unterbrochen | Nach Ablauf eines Timeouts Standardmedium | Keine Änderung |

Die Grafik wird proportional in 800 × 480 eingepasst. Abschneiden ist standardmäßig deaktiviert. Neue Spielereignisse verwerfen ältere Anzeigezustände. Nach einem erneuten Verbindungsaufbau synchronisiert Windows den aktuellen Zustand.

## Pi-Steuerbefehle

- `reload`: Anzeige und Konfiguration neu laden, ohne Betriebssystem-Neustart.
- `show-default`: Standardmedium anzeigen.
- `reboot`: Pi sauber neu starten.
- `shutdown`: Pi sauber herunterfahren. Wiederanlauf erfordert einen neuen Stromzyklus der Funksteckdose.
- `status`: Erreichbarkeit, Programmversion, aktueller Zustand und aktives Spiel abfragen.

Die konkrete Netzwerk-API ist in [protocol.md](protocol.md) festzulegen. Sie bleibt auf die direkte Verbindung beschränkt und benötigt Authentifizierung. Lokale Zugangsdaten werden nicht in Git gespeichert.

## Ausschalten

Windows-Startmenü und Big-Box-Menü sollen denselben Vorgang auslösen: Pi herunterfahren, danach Windows herunterfahren, zuletzt die Funksteckdose manuell ausschalten. Bei Windows-Neustart darf kein Pi-Shutdown ausgelöst werden. Ein allgemeines Windows-Shutdown-Skript eignet sich nicht, weil es auch bei Neustarts läuft. Als Ausgangspunkt für die Erkennung dient Windows-Ereignis 1074 mit Shutdown-Typ; Zustellung und Timing sind auf dem Zielrechner zu testen. Bis dieser Test erfolgreich ist, ist der automatische Pi-Shutdown nicht als zuverlässig nachgewiesen.

Ein Pi-Overlay-Dateisystem kann die SD-Karte zusätzlich gegen versehentliches frühes Abschalten schützen. Es ersetzt nicht den geordneten Ausschaltablauf. Lokale Konfiguration und Medien müssen vor Aktivierung des schreibgeschützten Overlays vorbereitet werden.

## Vor Ort zu prüfen

- Pi-OS-Version und 32-/64-Bit-Architektur
- Tatsächliche Displayausrichtung und Touch-Koordinaten
- Statische IP-Adressen oder feste Namen der direkten Verbindung
- LaunchBox-Installationspfad, Version und vorhandene Bildtypen je Beispielspiel
- Verhalten von Windows-Startmenü und Big-Box-Menü beim Ausschalten und Neustart
- Zeit von Pi-Shutdown-Befehl bis zum sicheren Stillstand
- Ob die Funksteckdose nach vollständigem Windows-Stillstand ausgeschaltet wird

## Community-Paket

Release-Artefakte sollen Windows-App, Plugin, Pi-Installationspaket, Beispieldateien und Prüfsummen enthalten. Weder ROMs noch Spielgrafiken, Zugangsdaten oder LaunchBox-Binärdateien gehören ins Repository oder Release. Installation, Update, Deinstallation und Wiederherstellung nach Verbindungsabbruch müssen dokumentiert und getestet werden.
