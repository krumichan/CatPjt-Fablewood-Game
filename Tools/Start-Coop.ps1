param([string]$EngineRoot,[ValidateRange(1024,65535)][int]$Port=7788,[switch]$LatestPackaged)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($LatestPackaged) {
    $packageReports=Join-Path $script:FableProjectRoot 'Saved\Packaging'
    $editor=$null
    if (Test-Path -LiteralPath $packageReports) {
        foreach($resultFile in @(Get-ChildItem -LiteralPath $packageReports -Filter 'result.json' -File -Recurse | Sort-Object LastWriteTime -Descending)) {
            $package=Get-Content -LiteralPath $resultFile.FullName -Raw | ConvertFrom-Json
            if($package.status -ne 'packaged'){continue}
            $gameExe=@($package.executables | Where-Object {$_ -match '[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$' -and (Test-Path -LiteralPath $_ -PathType Leaf)})
            if($gameExe.Count){$editor=$gameExe[0];break}
        }
    }
    if(-not $editor){throw 'No completed Windows package was found. Run Package-Prototype.cmd first.'}
    $launchPrefix=''
} else {
    $engine = Resolve-FableEngine -EngineRoot $EngineRoot
    Assert-FableReady
    $editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
    $launchPrefix='"' + $script:FableProjectFile + '" '
}
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "UDP port $Port is already in use. No existing process was changed." }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logDirectory = Join-Path $script:FableProjectRoot 'Saved\Logs'
[System.IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$hostLog = Join-Path $logDirectory "coop-$stamp-host.log"
$guestLog = Join-Path $logDirectory "coop-$stamp-guest.log"
# These are the interactive game windows the user will play in.
# This PC passed the packaged co-op test on driver 528.97. Skip only the advisory
# driver-version modal for these launches; driver checks/logs and global settings remain active.
$hostArguments = $launchPrefix + '/Game/Maps/ForestClearing?FablePlay=1?listen -game -port=' + $Port + ' -MULTIHOME=127.0.0.1 -windowed -ResX=900 -ResY=600 -WinX=20 -WinY=40 -nosplash -ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0 -ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0" -abslog="' + $hostLog + '"'
$hostProcess = Start-Process -FilePath $editor -ArgumentList $hostArguments -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(120)
$ready = $false
while ([DateTime]::UtcNow -lt $deadline) {
    $hostProcess.Refresh()
    if ($hostProcess.HasExited) { throw "Host exited before opening port $Port. See $hostLog" }
    $endpoints = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
    if ($endpoints | Where-Object OwningProcess -eq $hostProcess.Id) { $ready=$true; break }
    Start-Sleep -Milliseconds 500
}
if (-not $ready) { throw "Host did not open port $Port within 120 seconds. Its window was left open. See $hostLog" }
$guestArguments = $launchPrefix + '127.0.0.1:' + $Port + ' -game -windowed -ResX=900 -ResY=600 -WinX=950 -WinY=40 -nosplash -ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0 -ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0" -abslog="' + $guestLog + '"'
$guestProcess = Start-Process -FilePath $editor -ArgumentList $guestArguments -PassThru
$joinDeadline=[DateTime]::UtcNow.AddSeconds(120)
$joined=$false
while([DateTime]::UtcNow -lt $joinDeadline) {
    $hostProcess.Refresh(); $guestProcess.Refresh()
    if($hostProcess.HasExited -or $guestProcess.HasExited){throw "A game window exited before joining. See $hostLog and $guestLog"}
    if((Test-Path -LiteralPath $hostLog) -and (Select-String -LiteralPath $hostLog -Pattern 'LogNet: Join succeeded:' -Quiet)){$joined=$true;break}
    Start-Sleep -Milliseconds 500
}
if(-not $joined){throw "Guest connection was not confirmed within 120 seconds. Game windows were left open. See $guestLog"}
$sessionDirectory = Join-Path $script:FableProjectRoot 'Saved\LocalSessions'
[System.IO.Directory]::CreateDirectory($sessionDirectory) | Out-Null
[ordered]@{startedUtc=[DateTime]::UtcNow.ToString('o');hostPid=$hostProcess.Id;guestPid=$guestProcess.Id;port=$Port;hostLog=$hostLog;guestLog=$guestLog;connected=$joined;mode='loopback listen server; manual play; not an automated pass'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sessionDirectory "$stamp.json") -Encoding UTF8
Write-Host 'Two local game windows connected. Close both game windows when finished.'
Write-Host 'WASD move | Mouse look | Space jump | Shift dodge | J/Left mouse sword | K leaf arc | R next round'
