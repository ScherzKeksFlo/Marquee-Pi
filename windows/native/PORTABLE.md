# Marquee-Pi – portable Windows-Version

1. ZIP vollständig in einen Ordner entpacken. `Marquee-Pi.exe` starten.
2. Im Infobereich **Einstellungen…** öffnen und die Pi-Adresse und den API-Token eintragen. Der Knopf **Hilfe: Token erstellen** erklärt die Einrichtung am Pi.
3. Das LaunchBox-Plugin aus `MarqueePiLaunchBox.dll` nach `LaunchBox/Plugins/Marquee-Pi/` kopieren. LaunchBox/Big Box danach neu starten.

Die Datei `portable.flag` aktiviert den portablen Modus. Marquee-Pi legt `Data/settings.ini` und `Data/media/` im selben Ordner an. Beim Kopieren des gesamten Ordners werden Einstellungen und Medien mitgenommen. Die INI enthält den Token im Klartext und sollte nicht veröffentlicht werden.

Die EXE benötigt unter Windows 11 keine separat installierte .NET-Laufzeit und keinen Installer. Der optionale Autostart speichert den absoluten EXE-Pfad im Benutzerkonto; nach dem Verschieben des Ordners den Autostart im Einstellungsfenster erneut speichern.

Für den Raspberry Pi gilt die Installationsanleitung im Git-Repository. Pi-Adresse und Token müssen erreichbar und identisch mit der Pi-Konfiguration sein.

Für die Wischaktion „RetroArch-Menü öffnen“ kann im Einstellungsfenster zwischen Tastaturkürzel und lokalem Netzwerkbefehl gewählt werden. Für den Netzwerkbefehl in RetroArch `network_cmd_enable = "true"` setzen, den Port abgleichen (Standard 55355) und RetroArch neu starten.

Beim ersten Start nach dem Aktivieren der RetroArch-Netzwerkbefehle kann die Windows-Firewall nach Zugriff für `retroarch.exe` fragen. Marquee-Pi nutzt nur `127.0.0.1`: zunächst ohne Freigabe testen; falls nötig nur vertrauenswürdige private Netzwerke erlauben und „Öffentliche Netzwerke“ nicht auswählen.
