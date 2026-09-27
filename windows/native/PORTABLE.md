# Marquee-Pi – portable Windows-Version

1. ZIP vollständig in einen Ordner entpacken. `Marquee-Pi.exe` starten.
2. Im Infobereich **Einstellungen…** öffnen und die Pi-Adresse und den API-Token eintragen. Der Knopf **Hilfe: Token erstellen** erklärt die Einrichtung am Pi.
3. Das LaunchBox-Plugin aus `MarqueePiLaunchBox.dll` nach `LaunchBox/Plugins/Marquee-Pi/` kopieren. LaunchBox/Big Box danach neu starten.

Die Datei `portable.flag` aktiviert den portablen Modus. Marquee-Pi legt `Data/settings.ini` und `Data/media/` im selben Ordner an. Beim Kopieren des gesamten Ordners werden Einstellungen und Medien mitgenommen. Die INI enthält den Token im Klartext und sollte nicht veröffentlicht werden.

Die EXE benötigt unter Windows 11 keine separat installierte .NET-Laufzeit und keinen Installer. Der optionale Autostart speichert den absoluten EXE-Pfad im Benutzerkonto; nach dem Verschieben des Ordners den Autostart im Einstellungsfenster erneut speichern.

Für den Raspberry Pi gilt die Installationsanleitung im Git-Repository. Pi-Adresse und Token müssen erreichbar und identisch mit der Pi-Konfiguration sein.
