param(
    [string]$OutputDirectory = "dist/Marquee-Pi",
    [string]$LaunchBoxRoot = ""
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$compiler = (Get-Command g++ -ErrorAction Stop).Source
$windres = (Get-Command windres -ErrorAction Stop).Source
$output = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null
$object = Join-Path $output "resources.o"
& $windres -i (Join-Path $PSScriptRoot "resources.rc") -o $object -O coff
if ($LASTEXITCODE -ne 0) { throw "Icon resources failed" }
$exe = Join-Path $output "Marquee-Pi.exe"
& $compiler -std=c++20 -O2 -Wall -Wextra -municode -mwindows -static -static-libgcc -static-libstdc++ `
    (Join-Path $PSScriptRoot "core.cpp") (Join-Path $PSScriptRoot "strings.cpp") (Join-Path $PSScriptRoot "thumbnail.cpp") `
    (Join-Path $PSScriptRoot "main.cpp") $object `
    -o $exe -lwinhttp -lshell32 -ladvapi32 -lcrypt32 -lcomctl32 -lcomdlg32 `
    -lwevtapi -lgdi32 -luser32 -lole32 -luuid -lws2_32 -lwindowscodecs `
    -lmfplat -lmfreadwrite -lmf -lmfuuid -lpropsys
if ($LASTEXITCODE -ne 0) { throw "Marquee-Pi C++ build failed" }
$tests = Join-Path $output "Marquee-Pi-tests.exe"
& $compiler -std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ `
    (Join-Path $PSScriptRoot "core.cpp") (Join-Path $PSScriptRoot "strings.cpp") (Join-Path $PSScriptRoot "thumbnail.cpp") `
    (Join-Path $PSScriptRoot "tests.cpp") `
    -o $tests -lwinhttp -lshell32 -ladvapi32 -lcrypt32 -luser32 -lgdi32 -lole32 -luuid -lws2_32 -lwindowscodecs `
    -lmfplat -lmfreadwrite -lmf -lmfuuid -lpropsys
if ($LASTEXITCODE -ne 0) { throw "Marquee-Pi test build failed" }
$testDirectory = Join-Path $output "test-run"
New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
$previousTestIni = Join-Path $testDirectory "Data/settings.ini"
if (Test-Path -LiteralPath $previousTestIni) { Remove-Item -LiteralPath $previousTestIni }
Copy-Item -LiteralPath $tests -Destination (Join-Path $testDirectory "Marquee-Pi-tests.exe") -Force
New-Item -ItemType File -Force -Path (Join-Path $testDirectory "portable.flag") | Out-Null
& (Join-Path $testDirectory "Marquee-Pi-tests.exe")
if ($LASTEXITCODE -ne 0) { throw "Marquee-Pi tests failed" }
$plugin = Join-Path $root "launchbox-plugin/bin/Release/net48/MarqueePiLaunchBox.dll"
if ($LaunchBoxRoot) {
    dotnet build (Join-Path $root "launchbox-plugin/MarqueePiLaunchBox.csproj") -c Release "-p:LaunchBoxRoot=$LaunchBoxRoot"
    if ($LASTEXITCODE -ne 0) { throw "LaunchBox plugin build failed" }
}
if (!(Test-Path -LiteralPath $plugin)) { throw "Build the LaunchBox plugin with -LaunchBoxRoot first" }
$portable = Join-Path $output "portable"
New-Item -ItemType Directory -Force -Path $portable | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $portable "Marquee-Pi.exe") -Force
New-Item -ItemType File -Force -Path (Join-Path $portable "portable.flag") | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "PORTABLE.md") -Destination (Join-Path $portable "README.md") -Force
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $portable "LICENSE") -Force
Copy-Item -LiteralPath $plugin -Destination (Join-Path $portable "MarqueePiLaunchBox.dll") -Force
$zip = Join-Path $output "Marquee-Pi-portable-win-x64.zip"
Compress-Archive -LiteralPath (Join-Path $portable "Marquee-Pi.exe"),(Join-Path $portable "portable.flag"),(Join-Path $portable "README.md"),(Join-Path $portable "LICENSE"),(Join-Path $portable "MarqueePiLaunchBox.dll") -DestinationPath $zip -Force
Write-Output "EXE: $exe"
Write-Output "Portable ZIP: $zip"
