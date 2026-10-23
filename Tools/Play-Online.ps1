#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$Address='127.0.0.1:7789',
    [string]$BackendUrl='http://127.0.0.1:5281',
    [string]$Username,
    [switch]$Register,
    [ValidateSet('Warrior','Mage','Archer')][string]$InitialClass='Warrior',
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [switch]$LatestPackaged,
    [ValidateRange(900,3840)][int]$Width=900,
    [ValidateRange(600,2160)][int]$Height=600,
    [ValidateRange(0,10000)][int]$WindowX=20
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $IsWindows) { throw 'This launcher requires Windows and PowerShell 7.4+.' }
function Quote-OnlineArgument([string]$Value) {
    if ($Value.Contains('"') -or $Value.Contains("`n") -or $Value.Contains("`r")) { throw 'Unsafe process argument.' }
    return '"'+$Value+'"'
}
function Resolve-OnlinePackage([string]$Requested,[bool]$Latest) {
    if ($Requested -and $Latest) { throw 'Choose either -PackagedExecutable or -LatestPackaged.' }
    if ($Latest) {
        $reports=Join-Path $script:FableProjectRoot 'Saved\Packaging'
        if (Test-Path -LiteralPath $reports) {
            foreach ($file in @(Get-ChildItem -LiteralPath $reports -Filter 'result.json' -Recurse -File | Sort-Object LastWriteTime -Descending)) {
                try { $report=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json } catch { continue }
                if ($report.status -ne 'packaged') { continue }
                $candidates=@($report.executables | Where-Object { $_ -match '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$' -and (Test-Path -LiteralPath $_ -PathType Leaf) })
                if ($candidates.Count -eq 1) { $Requested=[string]$candidates[0];break }
            }
        }
        if (-not $Requested) { throw 'No completed Windows package with one inner game executable was found.' }
    }
    if (-not $Requested) { return $null }
    $exe=(Resolve-Path -LiteralPath $Requested -ErrorAction Stop).Path
    if ($exe -notmatch '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
        $exe=Join-Path (Split-Path -Parent $exe) 'Fablewood\Binaries\Win64\Fablewood.exe'
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The packaged inner game executable is missing.' }
    return (Resolve-Path -LiteralPath $exe).Path
}
function Write-OnlineState([string]$Path,$Value) {
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 8)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
}

function Invoke-OnlineApi([string]$Method,[string]$Route,$Body=$null,[string]$Token='') {
    $parameters=@{Uri=($BackendUrl.TrimEnd('/')+$Route);Method=$Method;TimeoutSec=25;ErrorAction='Stop'}
    if ($null -ne $Body) { $parameters.Body=$Body | ConvertTo-Json -Compress;$parameters.ContentType='application/json' }
    if ($Token) { $parameters.Headers=@{Authorization=('Bearer '+$Token)} }
    try { return Invoke-RestMethod @parameters }
    catch {
        $code='unavailable'
        $responseProperty=$_.Exception.PSObject.Properties['Response']
        if ($null -ne $responseProperty -and $null -ne $responseProperty.Value) { $code=[string][int]$responseProperty.Value.StatusCode }
        throw "Account service request failed: $Route (HTTP $code). For an existing account, omit -Register. Credentials were omitted."
    }
}
if ($Address -notmatch '^([A-Za-z0-9][A-Za-z0-9.-]*):([0-9]{1,5})$' -or [int]$Matches[2] -lt 1024 -or [int]$Matches[2] -gt 65535) {
    throw 'Use an address such as 127.0.0.1:7789 (host and UDP port only).'
}
$backendUri=$null
if (-not [Uri]::TryCreate($BackendUrl,[UriKind]::Absolute,[ref]$backendUri) -or $backendUri.UserInfo -or $backendUri.Query -or $backendUri.Fragment -or $backendUri.AbsolutePath -ne '/' -or
    ($backendUri.Scheme -ne 'https' -and -not ($backendUri.Scheme -eq 'http' -and $backendUri.IsLoopback))) { throw 'Use a loopback HTTP or HTTPS backend base URL without credentials, query, or path.' }
$BackendUrl=$backendUri.GetLeftPart([UriPartial]::Authority)
$package=Resolve-OnlinePackage $PackagedExecutable ([bool]$LatestPackaged)
$launchPrefix=''
if ($package) { $clientExe=$package;$mode='packaged-game-client' }
else {
    $engine=Resolve-FableEngine $EngineRoot;Assert-FableReady
    $clientExe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
    $launchPrefix=(Quote-OnlineArgument $script:FableProjectFile)+' '
    $mode='editor-game-client'
}
if ((Invoke-OnlineApi GET '/health').status -ne 'ok') { throw 'The account service is not ready. Start-OnlineServer.cmd first.' }
if (-not $Username) { $Username=Read-Host 'Account name (3-32 letters, digits, underscore)' }
if ($Username -notmatch '^[A-Za-z0-9_]{3,32}$') { throw 'Account name must contain 3-32 letters, digits, or underscores.' }
$securePassword=Read-Host 'Password (10-128 characters; input is hidden)' -AsSecureString
$pointer=[IntPtr]::Zero;$plainPassword=$null;$session=$null;$ticketResponse=$null;$ticket=$null
try {
    $pointer=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
    $plainPassword=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    if ($plainPassword.Length -lt 10 -or $plainPassword.Length -gt 128) { throw 'Password must contain 10-128 characters.' }
    if ($Register) {
        $classIndex=@{Warrior=0;Mage=1;Archer=2}[$InitialClass]
        $null=Invoke-OnlineApi POST '/v1/accounts' @{name=$Username;password=$plainPassword;initialClass=$classIndex}
    }
    $session=Invoke-OnlineApi POST '/v1/sessions' @{name=$Username;password=$plainPassword}
    $plainPassword=$null
    $ticketResponse=Invoke-OnlineApi POST '/v1/join-tickets' @{} ([string]$session.accessToken)
    $session=$null
    $ticket=[string]$ticketResponse.ticket;$ticketResponse=$null
    if ($ticket -notmatch '^[A-Za-z0-9_-]{43}$') { throw 'The account service returned an invalid one-time ticket.' }
    $runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $runRoot=Join-Path $script:FableProjectRoot ('Saved\OnlineSessions\'+$runId)
    [void][IO.Directory]::CreateDirectory($runRoot)
    $log=Join-Path $runRoot 'client.log'
    # Only the single-use ticket is present in the URL. Password/session bearer never enter argv or state.
    $arguments=$launchPrefix+$Address+'?FableTicket='+$ticket+' -game -d3d11 -windowed -nosplash -ResX='+$Width+' -ResY='+$Height+' -ForceRes -WinX='+$WindowX+' -WinY=40'+
        ' -abslog='+(Quote-OnlineArgument $log)+' -LogCmds="LogInit off,LogNet off,LogLoad off" -ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0 -ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"'
    try { $process=Start-Process -FilePath $clientExe -ArgumentList $arguments -WorkingDirectory $script:FableProjectRoot -WindowStyle Normal -PassThru }
    catch { throw 'Could not start the game client. Private account data and process arguments were omitted.' }
    $arguments=$null;$ticket=$null
    $state=[ordered]@{status='launched';runId=$runId;projectRoot=$script:FableProjectRoot;mode=$mode;pid=$process.Id;executable=$clientExe;processStartedUtc=$process.StartTime.ToUniversalTime().ToString('o');address=$Address;backendUrl=$BackendUrl;registered=[bool]$Register;log=$log;scope='manual client launch; connection and persistence are not asserted by this launcher'}
    $statePath=Join-Path $runRoot 'client-state.json';Write-OnlineState $statePath $state
    Write-Host 'Game window opened. Use another account for the second player. Reconnect by running this launcher again without -Register.'
    Write-Output ([pscustomobject]@{status='launched';pid=$process.Id;address=$Address;stateFile=$statePath;executable=$clientExe;log=$log})
} finally {
    if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
    $securePassword.Dispose();$plainPassword=$null;$session=$null;$ticketResponse=$null;$ticket=$null
}
