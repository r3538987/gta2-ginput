param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
python prepare.py
if ($LASTEXITCODE -ne 0) { throw 'Dependency/source preparation failed; run fetch-dependencies.ps1 first.' }
& ../../premake5.exe vs2022
if ($LASTEXITCODE -ne 0) { throw 'Premake failed.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $installation) { throw 'Visual Studio 2022 C++ build tools are required.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
& $msbuild build/projects/ginput-sdl3-experiment.sln /m:1 /v:minimal /p:Configuration=Release /p:Platform=Win32
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Copy-Item -LiteralPath deps/SDL3-3.4.18/lib/x86/SDL3.dll -Destination dist/SDL3.dll
Copy-Item -LiteralPath resources/GInputSDL3.ini -Destination dist/GInputSDL3.ini
Copy-Item -LiteralPath resources/GInputSDL3Hooks.ini -Destination dist/GInputSDL3Hooks.ini
New-Item -ItemType Directory -Path dist/licenses -Force | Out-Null
Copy-Item -LiteralPath deps/SDL3-3.4.18/LICENSE.txt -Destination dist/licenses/SDL3.txt
Copy-Item -LiteralPath 'deps/misi/Public Domain License.txt' -Destination dist/licenses/MISI.txt
Copy-Item -LiteralPath 'deps/misi/MISI/lua-5.3.4/MIT Software.txt' -Destination dist/licenses/Lua.txt
if (-not $SkipTests) {
    & ./dist/provider-tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Provider tests failed.' }
    & ./dist/vibration-tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vibration tests failed.' }
}
New-Item -ItemType Directory -Path dist/package/licenses -Force | Out-Null
foreach ($name in @('GInputSDL3.asi', 'GInputSDL3.ini', 'GInputSDL3Hooks.dll', 'GInputSDL3Hooks.ini', 'MISI.ASI', 'SDL3.dll')) {
    Copy-Item -LiteralPath (Join-Path 'dist' $name) -Destination (Join-Path 'dist/package' $name)
}
foreach ($name in @('SDL3.txt', 'MISI.txt', 'Lua.txt')) {
    Copy-Item -LiteralPath (Join-Path 'dist/licenses' $name) -Destination (Join-Path 'dist/package/licenses' $name)
}
Copy-Item -LiteralPath README.md -Destination dist/package/README.md
Copy-Item -LiteralPath build/misi-sdl3.patch -Destination dist/package/misi-sdl3.patch
$sourceRevision = git -C ../.. rev-parse HEAD
$manifest = @(
    "GInput source revision: $sourceRevision (plus experiments/sdl3-provider sources)"
    'SDL3: 3.4.18; official VC x86 development package'
    'MISI: Dege TwinStick v0.5.0 source archive, with SDL3 controller callback bridge'
    'Default: EnableGameInputHooks=0, EnableGameVibration=1; include GInputSDL3Hooks.dll'
    'Keep the existing TwinStick Lua script; remove the old XInput-only vibration ASI'
    'Validation: see README.md; gameplay testing is still required'
)
$manifest | Set-Content -LiteralPath dist/package/BUILD.txt -Encoding UTF8
Write-Output "Experimental package built at $PSScriptRoot/dist. No game files were installed."
