# Packages MD Drums for Windows from a Release build: the VST3 bundle, the README and the licences in one zip, as
# the macOS workflow packages its build (.github/workflows/md-drums-macos.yml). Build first with the version given,
# for example:
#   cmake . -B temp/cmake_vs22 "-DMD_DRUMS_VERSION=0.1.1" ...   (quoted: PowerShell cuts it at the first dot)
#   cmake --build temp/cmake_vs22 --config Release --target mdDrumsPlugin_VST3
# then: scripts\windows\package-md-drums.ps1 -Version 0.1.1 -Out <folder>
param(
	[Parameter(Mandatory = $true)][string]$Version,
	[Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\..\.."
$bundle = Join-Path $root "bin\plugins\Release\VST3\MD Drums.vst3"
if (-not (Test-Path $bundle)) { throw "No Release build at $bundle" }

$name = "MD-Drums-$Version-preview-Windows-x64"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
New-Item -ItemType Directory -Force $Out | Out-Null

# The bundle without the metadata JUCE writes as it builds: hosts read the plug-in itself
Copy-Item -Recurse $bundle (Join-Path $stage "MD Drums.vst3")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $stage "MD Drums.vst3\Contents\Resources\moduleinfo.json")
Copy-Item (Join-Path $root "scripts\windows\md-drums-README.txt") (Join-Path $stage "README.txt")
Copy-Item (Join-Path $root "NOTICE.md"), (Join-Path $root "LICENSE.md") $stage
Copy-Item (Join-Path $root "source\elektron\md\mdEngine\LICENSE") (Join-Path $stage "LICENSE-AGPL.txt")
Copy-Item (Join-Path $root "source\elektron\md\mdDrumsPlugin\skins\mdDrums\Inter-OFL.txt") $stage

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item -Recurse -Force $stage
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash
"$hash  $name.zip" | Tee-Object -FilePath (Join-Path $Out "SHA256-Windows.txt")
