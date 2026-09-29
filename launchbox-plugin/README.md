# LaunchBox/Big Box plugin

`GameEventsPlugin` implements `IGameLaunchingPlugin`. After a successful game launch, it reports the title, marquee or banner/logo, an existing arcade controls artwork, as well as box front art and clear logo to the locally running Windows app. After the game end detected by LaunchBox, it reports `exit`. Image paths are resolved within the LaunchBox directory. If the app is not reachable, the game launch continues.

## Build

The required `Unbroken.LaunchBox.Plugins.dll` must come from your own LaunchBox installation (`Core` in the tested version 14.0, possibly `Metadata` in older versions) and is not included. The installation path is passed as `LaunchBoxRoot` during the build:

```powershell
dotnet build launchbox-plugin/MarqueePiLaunchBox.csproj -c Release -p:LaunchBoxRoot="C:\Path\to\LaunchBox"
```

The finished `MarqueePiLaunchBox.dll` belongs in `LaunchBox\Plugins\Marquee-Pi` and is also included in the portable Windows package. It was built for x64 against LaunchBox 14.0.1.2 on the arcade PC without warnings; loading by Big Box as well as game start and game end were tested successfully on the target system.

LaunchBox documents that `OnGameExited()` can be triggered immediately after launch for some launchers, including Steam. Such games will later need separate process monitoring or mapping.
