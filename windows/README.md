# Marquee-Pi für Windows

Das Windows-Tray-Tool ist eine native C++20-Anwendung für Windows 11. Es empfängt Spielereignisse des LaunchBox-Plugins per Named Pipe, sendet Medien an die Pi-API und zeigt Verbindung, Einstellungen und Standardmedien im Infobereich an. Für die EXE ist keine .NET-Laufzeit erforderlich. Das LaunchBox-Plugin bleibt eine .NET-Framework-DLL, da es die LaunchBox-Plugin-API verwendet.

## Build

Benötigt werden MinGW-w64 mit `g++` und `windres` (getestet mit GCC 13.1), PowerShell und für die Plugin-DLL das .NET-SDK sowie eine lokale LaunchBox-Installation. Die proprietäre LaunchBox-API-DLL wird nur beim Build referenziert und nicht mitgeliefert.

```powershell
./windows/native/build.ps1 -LaunchBoxRoot "C:\LaunchBox"
```

Das Skript baut die native EXE, führt JSON-/INI-/Hotkey-Tests aus und erstellt `dist/Marquee-Pi/Marquee-Pi-portable-win-x64.zip`. Das ZIP enthält EXE, LaunchBox-Plugin-DLL, `portable.flag`, Anleitung und Lizenz. Es enthält keine Tokens oder persönlichen Medien. Die EXE verlinkt C++- und GCC-Laufzeit statisch und importiert nur Windows-System-DLLs.

## Installation und Konfiguration

Für die portable Version das ZIP entpacken und `Marquee-Pi.exe` starten. Die Datei `portable.flag` neben der EXE bewirkt, dass `Data/settings.ini` und `Data/media/` im selben Ordner liegen. Ohne diese Datei werden die Daten unter `%LOCALAPPDATA%\Marquee-Pi\` abgelegt. Beim ersten Start ohne portablen Modus werden die alten Werte und Medien aus `%LOCALAPPDATA%\ArcadePiDisplay\` übernommen. Der alte Ordner bleibt als Rückfall erhalten.

Die Plugin-DLL `MarqueePiLaunchBox.dll` gehört nach `LaunchBox\Plugins\Marquee-Pi\`. LaunchBox/Big Box danach neu starten. Die bestehende Named-Pipe-Kennung bleibt für die Kompatibilität mit älteren Plugin-Versionen erhalten.

Im Tray-Menü **Einstellungen…** lassen sich Pi-Adresse, API-Token, vier Wischgesten, RetroArch-Steuerungsart und -Tastenkombination, Netzwerk-Port und Autostart einstellen. **Hilfe: Token erstellen** zeigt die Schritte am Pi. **INI-Datei öffnen** und **Einstellungen neu laden** erlauben direkte Dateibearbeitung. Der Token liegt im Klartext in der INI und gehört nicht ins Git-Repository.

Für die Aktion **RetroArch-Menü öffnen** kann Marquee-Pi entweder das eingestellte Tastaturkürzel senden oder RetroArchs lokalen Netzwerkbefehl `MENU_TOGGLE` verwenden. Für den Netzwerkmodus in RetroArch unter **Einstellungen > Netzwerk > Netzwerkbefehle** aktivieren oder `network_cmd_enable = "true"` in `retroarch.cfg` setzen und RetroArch neu starten. Der Port muss mit `network_cmd_port` übereinstimmen (Standard: 55355). Marquee-Pi spricht nur `127.0.0.1` an; RetroArchs Netzwerkfunktion kann je nach Firewall auch aus dem LAN erreichbar sein.

**Windows-Firewall beim ersten RetroArch-Start:** Nach dem Aktivieren der Netzwerkbefehle kann Windows eine Freigabe für `retroarch.exe` verlangen. Marquee-Pi sendet den Befehl nur an `127.0.0.1`. Teste zunächst ohne zusätzliche Freigabe. Falls die lokale Menüaktion dann nicht funktioniert, erlaube RetroArch höchstens in vertrauenswürdigen privaten Netzwerken; „Öffentliche Netzwerke“ bleibt abgewählt. Gib den Netzwerkbefehls-Port nicht absichtlich für fremde Geräte frei. Hintergrund: [Windows-Firewall-Profile](https://learn.microsoft.com/windows/security/operating-system-security/network-security/windows-firewall/).

**Standardmedien verwalten…** importiert JPG, PNG, GIF, WebP und MP4 bis 20 MB. **Auf Pi aktivieren** überträgt das Medium; der Pi speichert es für den nächsten eigenen Start. Das Tray bietet außerdem Standardlogo, Neuladen, Pi-Neustart und Pi-Shutdown. Beim vollständigen Windows-Shutdown wird der Pi nur nach einem passenden Windows-Ereignis heruntergefahren; ein Windows-Neustart lässt ihn eingeschaltet. Diagnosemeldungen stehen in `shutdown.log` im jeweiligen Datenordner. `Marquee-Pi.exe --pi-shutdown` sendet den Befehl manuell.

## Prüfung

Die native EXE wurde mit GCC 13.1 gebaut. JSON-/INI-/Hotkey-Tests, Fenster-Smoke-Test und eine reale Spielstart-/Spielende-Nachricht über die Named Pipe zur Pi-API waren erfolgreich. Der lokale RetroArch-Netzwerkbefehl wurde am Arcade-PC mit einer Wischgeste während eines laufenden Spiels erfolgreich getestet. Der vollständige Shutdown über Big Box wurde am Arcade-PC getestet: Der Pi fuhr herunter und seine API war danach nicht mehr erreichbar. Das normale Windows-Ausschaltmenü wurde nach dem Parser-Fix noch nicht erneut geprüft.
