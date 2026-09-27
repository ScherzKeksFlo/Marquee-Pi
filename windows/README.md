# Windows-Begleitprogramm

`ArcadePiTray` ist eine Windows-Forms-App mit Symbol im Infobereich der Taskleiste. Sie zeigt den Pi-Status, verwaltet eigene Standardmedien und bietet Anzeige neu laden, Standardbild anzeigen, Pi-Neustart und Pi-Shutdown. Die App empfängt LaunchBox-Spielereignisse über eine lokale Named Pipe und sendet Grafiken an den Pi. Ein realer Big-Box-Spielstart und die Rückkehr zur Standardanimation nach Spielende wurden auf dem Zielsystem geprüft.

## Bauen und starten

```powershell
dotnet build windows/ArcadePiTray.csproj -c Release
dotnet run --project windows/ArcadePiTray.csproj
```

Zum Bauen wird das .NET-10-SDK benötigt. Ein selbstständiges Windows-x64-Paket lässt sich mit `dotnet publish windows/ArcadePiTray.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true` erstellen.

Nach dem Start im Infobereich `Verbindung einrichten…` wählen und die Pi-Adresse als `http://PI-IP:8765` sowie den Token aus der Pi-Konfiguration eintragen. Die Werte werden pro Windows-Benutzer unter `%LOCALAPPDATA%\ArcadePiDisplay\settings.json` gespeichert. Über `Mit Windows starten` lässt sich der Autostart für das angemeldete Konto einschalten, sobald die veröffentlichte EXE installiert ist. Das ist eine lokale Konfigurationsdatei und gehört nicht ins Git-Repository.

`Standardmedien verwalten…` importiert eigene Dateien in eine lokale Bibliothek. `Auf Pi aktivieren` überträgt die ausgewählte Datei. Der Pi speichert sie dauerhaft und verwendet sie beim nächsten eigenen Start ohne Windows-Verbindung. Die Vorschau öffnet derzeit die unter Windows zugeordnete Medien-App.

## Aktueller Stand

- Windows-App und Pi-API sind lokal gebaut bzw. getestet.
- Das LaunchBox-Plugin wird gegen die DLL aus der eigenen LaunchBox-Installation gebaut; sie wird nicht mitgeliefert. Auf dem Arcade-PC liegt ein selbstständiges .NET-7-Testpaket (vor dem .NET-10-Upgrade installiert) unter `C:\ArcadePiDisplay` und ein Autostart-Link für den Benutzer `flo`.
- Die automatische Pi-Abschaltung beim vollständigen Windows-Shutdown ist noch nicht am Zielrechner eingerichtet und geprüft.
- `ArcadePiTray.exe --pi-shutdown` ist als Aufruf für eine spätere Windows-Abschaltintegration vorbereitet. Ein Windows-Neustart darf diesen Aufruf nicht auslösen.
