param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-FableEngine -EngineRoot $EngineRoot
Assert-FableReady
Start-Process -FilePath (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList ('"' + $script:FableProjectFile + '"')
