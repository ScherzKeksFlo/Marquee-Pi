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

Im Tray-Menü **Einstellungen…** lassen sich Pi-Adresse, API-Token, vier Wischgesten, RetroArch-Tastenkombination und Autostart einstellen. **Hilfe: Token erstellen** zeigt die Schritte am Pi. **INI-Datei öffnen** und **Einstellungen neu laden** erlauben direkte Dateibearbeitung. Der Token liegt im Klartext in der INI und gehört nicht ins Git-Repository.

**Standardmedien verwalten…** importiert JPG, PNG, GIF, WebP und MP4 bis 20 MB. **Auf Pi aktivieren** überträgt das Medium; der Pi speichert es für den nächsten eigenen Start. Das Tray bietet außerdem Standardlogo, Neuladen, Pi-Neustart und Pi-Shutdown. Beim vollständigen Windows-Shutdown wird der Pi nur nach einem passenden Windows-Ereignis heruntergefahren; ein Windows-Neustart lässt ihn eingeschaltet. Diagnosemeldungen stehen in `shutdown.log` im jeweiligen Datenordner. `Marquee-Pi.exe --pi-shutdown` sendet den Befehl manuell.

## Prüfung

Die native EXE wurde mit GCC 13.1 gebaut. JSON-/INI-/Hotkey-Tests, Fenster-Smoke-Test und eine reale Spielstart-/Spielende-Nachricht über die Named Pipe zur Pi-API waren erfolgreich. Die Windows-Abschalterkennung ist implementiert; der echte Shutdown des Arcade-PCs ist noch nicht getestet.
