param([string]$EngineRoot,[string]$PythonExecutable)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-FableEngine -EngineRoot $EngineRoot
if (-not $PythonExecutable) { $PythonExecutable=(Get-Command python -ErrorAction Stop).Source }
foreach ($generator in @('create_character_meshes.py','create_world_meshes.py','create_guardian_meshes.py')) {
    & $PythonExecutable (Join-Path $PSScriptRoot ('Art\'+$generator))
    if ($LASTEXITCODE -ne 0) { throw "Art generator failed: $generator" }
}
$runId=[Guid]::NewGuid().ToString('N')
$previousRunId=$env:FABLE_ART_IMPORT_RUN_ID
$previousFilter=$env:FABLE_ART_MESH_FILTER
try {
    $env:FABLE_ART_IMPORT_RUN_ID=$runId
    $env:FABLE_ART_MESH_FILTER='*.obj'
    & (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $script:FableProjectFile /Engine/Maps/Entry "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'Art\import_art.py')" -culture=en -unattended -nosplash -nullrhi -NoSound
    if ($LASTEXITCODE -ne 0) { throw 'Art import editor failed.' }
    $report=Get-Content -LiteralPath (Join-Path $script:FableProjectRoot 'Saved\ArtImport\result.json') -Raw | ConvertFrom-Json
    if ($report.run_id -ne $runId -or $report.status -ne 'imported' -or $report.mesh_count -ne 68) { throw 'Art import did not produce a fresh, complete report.' }
    Write-Host 'All 68 original meshes and three materials imported; coordinate bounds verified.'
} finally {
    $env:FABLE_ART_IMPORT_RUN_ID=$previousRunId
    $env:FABLE_ART_MESH_FILTER=$previousFilter
}
