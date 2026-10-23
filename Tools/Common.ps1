Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:FableProjectRoot = Split-Path -Parent $PSScriptRoot
$script:FableProjectFile = Join-Path $script:FableProjectRoot 'Fablewood.uproject'
function Resolve-FableEngine {
    param([string]$EngineRoot)
    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($EngineRoot) { $candidates.Add($EngineRoot) }
    elseif ($env:UNREAL_ENGINE_ROOT) { $candidates.Add($env:UNREAL_ENGINE_ROOT) }
    else {
        $installed = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
        if (Test-Path -LiteralPath $installed) {
            $data = Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json
            foreach ($entry in $data.InstallationList) {
                if ($entry.AppName -eq 'UE_5.8') { $candidates.Add([string]$entry.InstallLocation) }
            }
        }
        foreach ($candidate in @('C:\Program Files\Epic Games\UE_5.8','D:\Epic Games\UE_5.8')) { $candidates.Add($candidate) }
    }
    foreach ($candidate in $candidates) {
        $versionPath = Join-Path $candidate 'Engine\Build\Build.version'
        $editorPath = Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe'
        if ((Test-Path -LiteralPath $versionPath) -and (Test-Path -LiteralPath $editorPath)) {
            $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
            if ($version.MajorVersion -eq 5 -and $version.MinorVersion -eq 8) { return (Resolve-Path -LiteralPath $candidate).Path }
        }
    }
    throw 'Unreal Engine 5.8 is not installed. In Epic Games Launcher, sign in and install UE 5.8.3 or a later 5.8 hotfix. Then run Setup-Project.cmd again.'
}
function Assert-FableReady {
    if (-not (Test-Path -LiteralPath (Join-Path $script:FableProjectRoot 'Binaries\Win64\UnrealEditor-Fablewood.dll'))) {
        throw 'The C++ editor module has not been built. Run Setup-Project.cmd first.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $script:FableProjectRoot 'Content\Maps\ForestClearing.umap'))) {
        throw 'The forest map has not been generated. Run Setup-Project.cmd first.'
    }
}
