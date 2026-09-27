# Windows-Begleitprogramm

`ArcadePiTray` ist eine Windows-Forms-App mit Symbol im Infobereich der Taskleiste. Sie zeigt den Pi-Status, verwaltet eigene Standardmedien und bietet Anzeige neu laden, Standardbild anzeigen, Pi-Neustart und Pi-Shutdown. Die App empfängt LaunchBox-Spielereignisse über eine lokale Named Pipe und sendet Grafiken an den Pi.

## Bauen und starten

```powershell
dotnet build windows/ArcadePiTray.csproj -c Release
dotnet run --project windows/ArcadePiTray.csproj
```

Der aktuelle Entwicklungsstand baut mit dem lokal verfügbaren .NET-7-SDK. Für eine Community-Veröffentlichung wird ein unterstütztes .NET-Ziel und ein selbstständiges Release-Paket vorgesehen.

Nach dem Start im Infobereich `Verbindung einrichten…` wählen und die Pi-Adresse als `http://PI-IP:8765` sowie den Token aus der Pi-Konfiguration eintragen. Die Werte werden pro Windows-Benutzer unter `%LOCALAPPDATA%\ArcadePiDisplay\settings.json` gespeichert. Über `Mit Windows starten` lässt sich der Autostart für das angemeldete Konto einschalten, sobald die veröffentlichte EXE installiert ist. Das ist eine lokale Konfigurationsdatei und gehört nicht ins Git-Repository.

`Standardmedien verwalten…` importiert eigene Dateien in eine lokale Bibliothek. `Auf Pi aktivieren` überträgt die ausgewählte Datei. Der Pi speichert sie dauerhaft und verwendet sie beim nächsten eigenen Start ohne Windows-Verbindung. Die Vorschau öffnet derzeit die unter Windows zugeordnete Medien-App.

## Aktueller Stand

- Windows-App und Pi-API sind lokal gebaut bzw. getestet.
- Das LaunchBox-Plugin benötigt die DLL aus der eigenen LaunchBox-Installation; sie wird nicht mitgeliefert.
- Installation auf dem Arcade-PC und die automatische Pi-Abschaltung beim vollständigen Windows-Shutdown sind noch nicht am Zielrechner eingerichtet und geprüft.
- `ArcadePiTray.exe --pi-shutdown` ist als Aufruf für eine spätere Windows-Abschaltintegration vorbereitet. Ein Windows-Neustart darf diesen Aufruf nicht auslösen.
