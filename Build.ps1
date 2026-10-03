[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [string]$FactionConfig = (Join-Path $PSScriptRoot 'config\factions.json'),
    [string]$BuildProfile = (Join-Path $PSScriptRoot 'profiles\kards-20261002-win64.json'),
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
    [string]$OutputDir = (Join-Path $PSScriptRoot 'dist'),
    [string]$Python = 'python',
    [string]$CMake = '',
    [string]$Generator = '',
    [string[]]$AdditionalTestExe = @(),
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$GameExe = (Resolve-Path -LiteralPath $GameExe).Path
$FactionConfig = (Resolve-Path -LiteralPath $FactionConfig).Path
$BuildProfile = (Resolve-Path -LiteralPath $BuildProfile).Path
$Python = (Get-Command $Python -ErrorAction Stop).Source
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$VisualStudio = $null
if (Test-Path -LiteralPath $Vswhere) {
    $Installations = @(& $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
    if ($Installations.Count -gt 0) { $VisualStudio = $Installations[0] }
}
if (-not $CMake) {
    $FoundCmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($FoundCmake) { $CMake = $FoundCmake.Source }
    elseif ($VisualStudio) {
        $CMake = Join-Path $VisualStudio.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
if (-not $CMake -or -not (Test-Path -LiteralPath $CMake)) {
    throw 'CMake not found. Install C++ CMake tools in Visual Studio or pass -CMake.'
}
$CMake = (Resolve-Path -LiteralPath $CMake).Path
$CTest = Join-Path (Split-Path $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $CTest)) { throw 'ctest.exe is missing beside cmake.exe.' }
if (-not $Generator) {
    if (-not $VisualStudio) { throw 'MSVC not found. Install Desktop development with C++, or pass a Visual Studio -Generator.' }
    switch (([version]$VisualStudio.installationVersion).Major) {
        17 { $Generator = 'Visual Studio 17 2022' }
        18 { $Generator = 'Visual Studio 18 2026' }
        default { throw 'Unrecognized Visual Studio version. Pass -Generator explicitly.' }
    }
}
if ($Generator -notlike 'Visual Studio *') { throw 'Build.ps1 uses Visual Studio generators. Use CMake directly for Ninja.' }
& $Python -c 'import pefile'
if ($LASTEXITCODE -ne 0) { throw 'Run: python -m pip install -r requirements.txt' }

& $CMake -S $PSScriptRoot -B $BuildDir -G $Generator -A x64 `
    "-DPython3_EXECUTABLE=$Python" "-DKARDS_GAME_EXE=$GameExe" `
    "-DKARDS_FACTION_CONFIG=$FactionConfig" "-DKARDS_BUILD_PROFILE=$BuildProfile" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $CMake --build $BuildDir --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if (-not $SkipTests) {
    & $CTest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; no new release was installed into OutputDir.' }
    foreach ($ExtraExe in $AdditionalTestExe) {
        $ExtraExe = (Resolve-Path -LiteralPath $ExtraExe).Path
        & (Join-Path $BuildDir "$Configuration\native_tests.exe") $ExtraExe
        if ($LASTEXITCODE -ne 0) { throw "Additional native test failed: $ExtraExe" }
    }
}
& $CMake --install $BuildDir --config $Configuration --prefix $OutputDir
if ($LASTEXITCODE -ne 0) { throw 'Release staging failed.' }
$Dll = Join-Path $OutputDir 'dwmapi.dll'
$Hash = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  dwmapi.dll" | Set-Content -LiteralPath (Join-Path $OutputDir 'SHA256SUMS.txt') -Encoding ascii
Write-Output "Built: $Dll"
Write-Output "SHA256: $Hash"
Write-Output 'Build complete. No files were deployed to the game directory.'
