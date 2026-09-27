# Windows-Begleitprogramm

`ArcadePiTray` ist eine Windows-Forms-App mit Symbol im Infobereich der Taskleiste. Sie zeigt den Pi-Status, verwaltet eigene Standardmedien und bietet Anzeige neu laden, Standardbild anzeigen, Pi-Neustart und Pi-Shutdown. Die App empfängt LaunchBox-Spielereignisse über eine lokale Named Pipe und sendet Grafiken an den Pi. Ein realer Big-Box-Spielstart und die Rückkehr zur Standardanimation nach Spielende wurden auf dem Zielsystem geprüft.

## Bauen und starten

```powershell
dotnet build windows/ArcadePiTray.csproj -c Release
dotnet run --project windows/ArcadePiTray.csproj
```

Zum Bauen wird das .NET-10-SDK benötigt. Ein selbstständiges Windows-x64-Paket lässt sich mit `dotnet publish windows/ArcadePiTray.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true` erstellen.

Das selbstständige Paket enthält die .NET-Laufzeit und Windows Forms; auf dem Ziel-PC muss deshalb keine passende Laufzeit installiert sein. Das getestete .NET-7-Testpaket war ohne Kompression etwa 151 MB groß, mit `EnableCompressionInSingleFile=true` etwa 68 MB. Die Kompression kann den Programmstart etwas verlangsamen. Wer die zum Target Framework passende Windows-Desktop-Laufzeit separat installiert hat, kann mit `--self-contained false` ein kleineres, laufzeitabhängiges Paket veröffentlichen.
Nach dem Start im Infobereich **Einstellungen…** öffnen. Unter **Verbindung** die Pi-Adresse (`http://PI-IP:8765`) und den API-Token eintragen. **Hilfe: Token erstellen** zeigt die Schritte am Pi direkt im Tool. Unter **Wischgesten** werden die vier Richtungen sowie die RetroArch-Tastenkombination eingestellt. Zur Auswahl stehen Marquee, Box Art, LaunchBox-Clear-Logo, Steuerungsbelegung, Standardmedium, RetroArch-Menü und keine Aktion. Das Tastaturkürzel (z. B. `F1` oder `Ctrl+Shift+F1`) geht nur an ein aktives RetroArch-Fenster. Unter **Allgemein** lässt sich der Autostart für das angemeldete Konto einstellen und die Standardmedienverwaltung öffnen.

Alle Konfigurationswerte liegen pro Windows-Benutzer in `%LOCALAPPDATA%\ArcadePiDisplay\settings.ini`. Über **INI-Datei öffnen** lässt sie sich direkt bearbeiten; danach **Einstellungen neu laden** wählen oder die App neu starten. Die INI enthält den Token im Klartext und gehört nicht ins Git-Repository. Eine vorhandene `settings.json` wird beim ersten Start einmalig übernommen und als Rückfallkopie belassen. Der Autostart wird zusätzlich im Windows-Benutzerkonto eingerichtet; dafür muss die veröffentlichte `ArcadePiTray.exe` gestartet sein.

Das Tray-Symbol verwendet die mitgelieferten Icons: verbunden nach erfolgreicher Pi-Abfrage, getrennt beim Start und wenn der Pi nicht erreichbar ist. Die Icons sind in die EXE eingebettet. Die 16- bis 48-Pixel-Stufen haben einen vergrößerten Statuspunkt; die ICO-Dateien lassen sich aus den PNG-Quellen mit `windows/Resources/Generate-TrayIcon.ps1` reproduzieren.
`Standardmedien verwalten…` importiert eigene Dateien in eine lokale Bibliothek. `Auf Pi aktivieren` überträgt die ausgewählte Datei. Der Pi speichert sie dauerhaft und verwendet sie beim nächsten eigenen Start ohne Windows-Verbindung. Die Vorschau öffnet derzeit die unter Windows zugeordnete Medien-App.

## Aktueller Stand

- Windows-App und Pi-API sind lokal gebaut bzw. getestet.
- Das LaunchBox-Plugin wird gegen die DLL aus der eigenen LaunchBox-Installation gebaut; sie wird nicht mitgeliefert. Auf dem Arcade-PC liegt ein selbstständiges .NET-7-Testpaket (vor dem .NET-10-Upgrade installiert) unter `C:\ArcadePiDisplay` für den Benutzer `flo`.
- Die automatische Pi-Abschaltung ist implementiert, aber noch nicht am Zielrechner geprüft. Beim bestätigten Windows-Sitzungsende liest die App das aktuelle User32-Ereignis 1074 und sendet den Pi-Shutdown nur bei eindeutigem Ausschalt-Typ. Diagnosemeldungen stehen unter `%LOCALAPPDATA%\ArcadePiDisplay\shutdown.log`.
- `ArcadePiTray.exe --pi-shutdown` bleibt als manueller Kommandoaufruf verfügbar. Ein Windows-Neustart löst über die automatische Erkennung keinen Pi-Shutdown aus.
