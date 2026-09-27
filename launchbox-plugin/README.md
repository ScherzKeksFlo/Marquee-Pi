# LaunchBox/Big-Box-Plugin

`GameEventsPlugin` implementiert `IGameLaunchingPlugin`. Nach erfolgreichem Spielstart meldet es Titel, Marquee bzw. Banner/Logo und eine vorhandene Arcade-Steuerungsgrafik an die lokal laufende Windows-App. Nach dem von LaunchBox erkannten Spielende meldet es `exit`. Die Bildpfade werden im LaunchBox-Verzeichnis aufgelöst. Ist die App nicht erreichbar, läuft der Spielstart weiter.

## Build

Die benötigte `Unbroken.LaunchBox.Plugins.dll` muss aus der eigenen LaunchBox-Installation kommen (`Core` bei der getesteten Version 14.0, bei älteren Versionen ggf. `Metadata`) und wird nicht mitgeliefert. Der Installationspfad wird beim Build als `LaunchBoxRoot` übergeben:

```powershell
dotnet build launchbox-plugin/ArcadePiLaunchBox.csproj -c Release -p:LaunchBoxRoot="C:\Pfad\zu\LaunchBox"
```

Die fertige DLL gehört in einen Unterordner von `LaunchBox\Plugins`. Gegen LaunchBox 14.0.1.2 auf dem Arcade-PC wurde sie mit x64 ohne Warnungen gebaut; das Laden durch Big Box und echte Spielereignisse werden noch geprüft.

LaunchBox dokumentiert, dass `OnGameExited()` bei manchen Launchern, darunter Steam, unmittelbar nach dem Start ausgelöst werden kann. Solche Spiele brauchen später eine gesonderte Prozessüberwachung oder Zuordnung.
