# LaunchBox/Big-Box-Plugin

`GameEventsPlugin` implementiert `IGameLaunchingPlugin`. Nach erfolgreichem Spielstart meldet es Titel, Marquee bzw. Banner/Logo und eine vorhandene Arcade-Steuerungsgrafik sowie Box-Front-Art und Clear Logo an die lokal laufende Windows-App. Nach dem von LaunchBox erkannten Spielende meldet es `exit`. Die Bildpfade werden im LaunchBox-Verzeichnis aufgelöst. Ist die App nicht erreichbar, läuft der Spielstart weiter.

## Build

Die benötigte `Unbroken.LaunchBox.Plugins.dll` muss aus der eigenen LaunchBox-Installation kommen (`Core` bei der getesteten Version 14.0, bei älteren Versionen ggf. `Metadata`) und wird nicht mitgeliefert. Der Installationspfad wird beim Build als `LaunchBoxRoot` übergeben:

```powershell
dotnet build launchbox-plugin/MarqueePiLaunchBox.csproj -c Release -p:LaunchBoxRoot="C:\Pfad\zu\LaunchBox"
```

Die fertige `MarqueePiLaunchBox.dll` gehört nach `LaunchBox\Plugins\Marquee-Pi` und liegt auch dem portablen Windows-Paket bei. Gegen LaunchBox 14.0.1.2 auf dem Arcade-PC wurde sie mit x64 ohne Warnungen gebaut; das Laden durch Big Box und echte Spielereignisse werden noch geprüft.

LaunchBox dokumentiert, dass `OnGameExited()` bei manchen Launchern, darunter Steam, unmittelbar nach dem Start ausgelöst werden kann. Solche Spiele brauchen später eine gesonderte Prozessüberwachung oder Zuordnung.
