param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-FableEngine -EngineRoot $EngineRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer is not available.' }
$instances = @(& $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64 -format json | ConvertFrom-Json)
if ($instances.Count -eq 0) { throw 'MSVC 14.50 is missing. Install the C++ components described in README.md.' }
$toolsPath = Join-Path $instances[0].installationPath 'VC\Tools\MSVC'
$compiler = Get-ChildItem -LiteralPath $toolsPath -Directory | Where-Object Name -like '14.50.*' | Sort-Object Name -Descending | Select-Object -First 1
if (-not $compiler) { throw 'MSVC 14.50 directory is missing.' }
$configDirectory = Join-Path $script:FableProjectRoot 'Saved\UnrealBuildTool'
[System.IO.Directory]::CreateDirectory($configDirectory) | Out-Null
$configPath = Join-Path $configDirectory 'BuildConfiguration.xml'
if (-not (Test-Path -LiteralPath $configPath)) {
    $xml = '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><BuildConfiguration><MaxParallelActions>2</MaxParallelActions><bAllowUBAExecutor>false</bAllowUBAExecutor></BuildConfiguration><WindowsPlatform><CompilerVersion>' + $compiler.Name + '</CompilerVersion></WindowsPlatform></Configuration>'
    [System.IO.File]::WriteAllText($configPath,$xml,[System.Text.UTF8Encoding]::new($false))
}
$build = Join-Path $engine 'Engine\Build\BatchFiles\Build.bat'
Write-Host 'Building FablewoodEditor. The first build may take several minutes.'
& $build FablewoodEditor Win64 Development "-Project=$script:FableProjectFile" -NoUBA -NoPCH -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "Unreal C++ build failed with exit code $LASTEXITCODE." }
$map = Join-Path $script:FableProjectRoot 'Content\Maps\ForestClearing.umap'
# Always verify a pre-existing map; an earlier failed generator may have saved a partial level.
& {
    $editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $generator = Join-Path $script:FableProjectRoot 'Tools\generate_map.py'
    $runId = [Guid]::NewGuid().ToString('N')
    $report = Join-Path $script:FableProjectRoot ('Saved\MapGeneration\' + $runId + '.json')
    $previousRunId = [Environment]::GetEnvironmentVariable('FABLEWOOD_MAP_RUN_ID','Process')
    try {
        [Environment]::SetEnvironmentVariable('FABLEWOOD_MAP_RUN_ID',$runId,'Process')
        & $editor $script:FableProjectFile /Engine/Maps/Entry "-ExecutePythonScript=$generator" -culture=en -unattended -nosplash -nullrhi -NoSound -stdout -FullStdOutLogOutput
        if ($LASTEXITCODE -ne 0) { throw "Forest map generation failed with exit code $LASTEXITCODE." }
    } finally {
        [Environment]::SetEnvironmentVariable('FABLEWOOD_MAP_RUN_ID',$previousRunId,'Process')
    }
    if (-not (Test-Path -LiteralPath $report)) { throw 'Map generation did not produce a completion report.' }
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if ($result.run_id -ne $runId -or $result.status -notin @('created','preserved_verified')) { throw "Map generation did not complete successfully. See $report" }
}
Assert-FableReady
Write-Host 'Editor module and forest map are ready. Use Start-Coop.cmd to launch two game windows.'
