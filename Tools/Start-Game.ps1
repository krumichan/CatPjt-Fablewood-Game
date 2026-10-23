param(
    [switch]$Editor,
    [string]$EngineRoot,
    [ValidateRange(1024,65535)][int]$Port = 7788
)
. (Join-Path $PSScriptRoot 'Common.ps1')

if ($Editor) {
    $engine = Resolve-FableEngine -EngineRoot $EngineRoot
    Assert-FableReady
    $executable = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
    $launchPrefix = '"' + $script:FableProjectFile + '" '
    $launchMode = 'editor game frontend'
} else {
    $packageReports = Join-Path $script:FableProjectRoot 'Saved\Packaging'
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $script:FableProjectRoot 'Builds')) + [IO.Path]::DirectorySeparatorChar
    $executable = $null
    if (Test-Path -LiteralPath $packageReports -PathType Container) {
        $reports = @(Get-ChildItem -LiteralPath $packageReports -Filter 'result.json' -File -Recurse | Sort-Object LastWriteTimeUtc -Descending)
        foreach ($report in $reports) {
            try {
                $package = Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json
                if ($package.status -ne 'packaged') { continue }
                foreach ($candidate in @($package.executables)) {
                    if ($candidate -notmatch '[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') { continue }
                    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
                    $resolved = (Resolve-Path -LiteralPath $candidate).Path
                    if (-not $resolved.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { continue }
                    $executable = $resolved
                    break
                }
            } catch {
                Write-Warning "Skipped unreadable package report: $($report.FullName)"
                continue
            }
            if ($executable) { break }
        }
    }
    if (-not $executable) { throw 'No completed Windows package was found. Run Package-Prototype.cmd first, or use Play.cmd -Editor for the built editor version.' }
    $launchPrefix = ''
    $launchMode = 'packaged game frontend'
}

$stamp = (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0,8))
$logDirectory = Join-Path $script:FableProjectRoot 'Saved\Logs'
[IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$gameLog = Join-Path $logDirectory "frontend-$stamp.log"
# This is the interactive game window requested by the player.
# This PC passed game tests on driver 528.97. Match the existing play launcher by
# skipping its advisory modal only; driver logs and global settings stay active.
# Opening this URL without FablePlay or listen shows the start screen first.
$arguments = $launchPrefix + '/Game/Maps/ForestClearing -game -port=' + $Port + ' -windowed -ResX=1280 -ResY=800 -nosplash -ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0 -ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0" -abslog="' + $gameLog + '"'
$gameProcess = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru

$sessionDirectory = Join-Path $script:FableProjectRoot 'Saved\LocalSessions'
[IO.Directory]::CreateDirectory($sessionDirectory) | Out-Null
[ordered]@{
    startedUtc = [DateTime]::UtcNow.ToString('o')
    pid = $gameProcess.Id
    executable = $executable
    port = $Port
    log = $gameLog
    mode = $launchMode
    automatedPass = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sessionDirectory "frontend-$stamp.json") -Encoding UTF8
Write-Host 'One game window opened at the start screen.'
Write-Host 'Choose New Game, Continue, Host Co-op, or Join inside the game. Close the game window when finished.'
Write-Host "Log: $gameLog"
