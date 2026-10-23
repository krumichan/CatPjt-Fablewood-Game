param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')

$engine = Resolve-FableEngine -EngineRoot $EngineRoot
Assert-FableReady
$uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat -PathType Leaf)) {
    throw "Unreal AutomationTool is missing: $uat"
}
$compilerConfiguration = Join-Path $script:FableProjectRoot 'Saved\UnrealBuildTool\BuildConfiguration.xml'
if (-not (Test-Path -LiteralPath $compilerConfiguration -PathType Leaf)) {
    throw 'The project-local compiler configuration is missing. Run Setup-Project.cmd before packaging.'
}

# Each run has its own archive and report. Earlier builds are never removed or replaced.
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + 'Z-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$archiveDirectory = Join-Path $script:FableProjectRoot ('Builds\Windows-' + $runId)
$reportDirectory = Join-Path $script:FableProjectRoot ('Saved\Packaging\' + $runId)
if (Test-Path -LiteralPath $archiveDirectory) {
    throw "The archive path already exists and was preserved: $archiveDirectory"
}
[System.IO.Directory]::CreateDirectory($archiveDirectory) | Out-Null
[System.IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
$logPath = Join-Path $reportDirectory 'AutomationTool.log'
$gameBuildLog = Join-Path $reportDirectory 'GameBuild.log'
$reportPath = Join-Path $reportDirectory 'result.json'
$report = [ordered]@{
    status = 'started'
    runId = $runId
    startedUtc = [DateTime]::UtcNow.ToString('o')
    project = $script:FableProjectFile
    engine = $engine
    platform = 'Win64'
    configuration = 'Development'
    map = '/Game/Maps/ForestClearing'
    archiveDirectory = $archiveDirectory
    log = $logPath
    gameBuildLog = $gameBuildLog
    exitCode = $null
    executables = @()
    completedUtc = $null
    error = $null
    playTested = $false
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8

$uatArguments = @(
    'BuildCookRun',
    "-project=$script:FableProjectFile",
    '-noP4',
    '-platform=Win64',
    '-clientconfig=Development',
    '-map=/Game/Maps/ForestClearing',
    '-build',
    '-UbtArgs=-NoUBA -NoPCH',
    '-cook',
    '-AdditionalCookerOptions=-culture=ko',
    '-stage',
    # Keep local build symbols; omit their large copies from the playable archive.
    '-nodebuginfo',
    '-pak',
    '-prereqs',
    '-archive',
    "-archivedirectory=$archiveDirectory",
    '-unattended',
    '-utf8output'
)

Write-Host 'Building and packaging the Windows Development prototype. The first cook may take several minutes.'
Write-Host "Archive destination: $archiveDirectory"
Write-Host "Packaging log: $logPath"
try {
    # UAT combines Editor and Game targets in one UBT process. Gather the Game
    # target independently so newly added sources are found on the external drive.
    $build = Join-Path $engine 'Engine\Build\BatchFiles\Build.bat'
    # Refresh project inputs before a build; external volumes may retain old
    # directory timestamps after new C++ files are added.
    $projectFilesLog = Join-Path $reportDirectory 'ProjectFiles.log'
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $build -ProjectFiles "-Project=$script:FableProjectFile" -Game -Engine -Progress 2>&1 | Tee-Object -FilePath $projectFilesLog
        $projectFilesExitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousErrorAction }
    if ($projectFilesExitCode -ne 0) { throw "Project input refresh failed: $projectFilesLog" }

    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $build Fablewood Win64 Development "-Project=$script:FableProjectFile" -WaitMutex -NoHotReloadFromIDE -NoPCH -NoUBA 2>&1 | Tee-Object -FilePath $gameBuildLog
        $gameBuildExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($gameBuildExitCode -ne 0) { throw "Game source gather/build failed with exit code $gameBuildExitCode. See $gameBuildLog" }
    # A native tool can emit diagnostic stderr on a successful run. Preserve the output
    # and judge success by UAT's actual exit code, then verify the archived executable.
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $uat @uatArguments 2>&1 | Tee-Object -FilePath $logPath
        $packagingExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    $report.exitCode = $packagingExitCode
    if ($packagingExitCode -ne 0) {
        throw "Unreal packaging failed with exit code $packagingExitCode. See $logPath"
    }
    $executables = @(Get-ChildItem -LiteralPath $archiveDirectory -Filter 'Fablewood.exe' -File -Recurse)
    if ($executables.Count -eq 0) {
        throw "UAT reported success but no Fablewood.exe exists in $archiveDirectory. See $logPath"
    }
    $fontLicense = Join-Path $script:FableProjectRoot 'ArtSource\Fonts\LICENSE.txt'
    if (-not (Test-Path -LiteralPath $fontLicense -PathType Leaf)) { throw 'Korean font license is missing.' }
    $notices = Join-Path $archiveDirectory 'ThirdPartyNotices'
    [IO.Directory]::CreateDirectory($notices) | Out-Null
    Copy-Item -LiteralPath $fontLicense -Destination (Join-Path $notices 'NanumGothic-LICENSE.txt')
    $report.executables = @($executables | ForEach-Object FullName)
    $report.status = 'packaged'
    Write-Host "Windows prototype packaged: $archiveDirectory"
    Write-Host 'Packaging succeeded. This script did not launch or play-test the build.'
} catch {
    $report.status = 'failed'
    $report.error = $_.Exception.Message
    throw
} finally {
    $report.completedUtc = [DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    Write-Host "Packaging report: $reportPath"
}
