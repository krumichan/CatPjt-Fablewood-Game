#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$BackendRoot,
    [ValidateRange(1024,65535)][int]$Port=7789,
    [ValidateRange(15,300)][int]$ReadyTimeoutSeconds=120
)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Backend-Link.ps1')
$backendLink=Get-FableBackendLink -BackendRoot $BackendRoot
if (-not $IsWindows) { throw 'This launcher requires Windows and PowerShell 7.4+.' }
function Quote-OnlineArgument([string]$Value) {
    if ($Value.Contains('"') -or $Value.Contains("`n") -or $Value.Contains("`r")) { throw 'Unsafe process argument.' }
    return '"'+$Value+'"'
}
function Write-OnlineState([string]$Path,$Value) {
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 8)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
}

# Launcher-engine Game targets do not provide a dedicated server executable.
# Use the verified Editor-Cmd server mode; packaged clients are launched separately.
$engine=Resolve-FableEngine $EngineRoot;Assert-FableReady
$serverExe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $serverExe -PathType Leaf)) { throw 'UnrealEditor-Cmd.exe is missing.' }
$launchPrefix=(Quote-OnlineArgument $script:FableProjectFile)+' '
$mode='editor-dedicated'
$sessionRoot=Join-Path $script:FableProjectRoot 'Saved\OnlineSessions'
$statePath=Join-Path $sessionRoot ('server-'+$Port+'.json')
$serverConfig=$backendLink.serverConfig
$owned=$null
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    $previous=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    foreach ($name in @('projectRoot','executable','pid','processStartedUtc','port','serverConfig')) {
        if ($null -eq $previous.PSObject.Properties[$name]) { throw 'Online server ownership state is incomplete. No process was changed.' }
    }
    if ([IO.Path]::GetFullPath([string]$previous.projectRoot) -ne [IO.Path]::GetFullPath($script:FableProjectRoot) -or
        [int]$previous.port -ne $Port -or [IO.Path]::GetFullPath([string]$previous.serverConfig) -ne $serverConfig) {
        throw 'Online server state belongs to a different project or endpoint. No process was changed.'
    }
    $candidate=Get-Process -Id ([int]$previous.pid) -ErrorAction SilentlyContinue
    if ($candidate) {
        # Preserve fractional seconds when ConvertFrom-Json supplies a DateTime/DateTimeOffset value.
        $recordedStart=$previous.processStartedUtc
        if ($recordedStart -is [DateTimeOffset]) { $startedUtc=$recordedStart.UtcDateTime }
        elseif ($recordedStart -is [DateTime]) { $startedUtc=$recordedStart.ToUniversalTime() }
        else { $startedUtc=[DateTime]::Parse([string]$recordedStart,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime() }
        if ([IO.Path]::GetFullPath($candidate.Path) -ne [IO.Path]::GetFullPath([string]$previous.executable) -or
            $candidate.StartTime.ToUniversalTime().Ticks -ne $startedUtc.Ticks) {
            throw 'The recorded server PID now belongs to a different process. Nothing was stopped.'
        }
        if ([IO.Path]::GetFullPath($candidate.Path) -ne [IO.Path]::GetFullPath($serverExe)) { throw 'An owned server is already running with a different executable. Keep both clients on that same build.' }
        $owned=$candidate
    }
}
$endpoints=@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
if ($owned) {
    if (@($endpoints | Where-Object OwningProcess -EQ $owned.Id).Count -eq 0 -or @($endpoints | Where-Object OwningProcess -NE $owned.Id).Count -gt 0) {
        throw 'The owned server does not exclusively own the requested UDP port. No process was changed.'
    }
} elseif ($endpoints.Count -gt 0) { throw "UDP port $Port is occupied. No unrelated process was stopped." }
# Reuse existing private configuration; only Start-Backend handles credentials.
$backendArgs=@{}
$backendStatePath=$backendLink.stateFile
if (Test-Path -LiteralPath $backendStatePath -PathType Leaf) {
    $backendState=Get-Content -LiteralPath $backendStatePath -Raw | ConvertFrom-Json
    $backendUri=[Uri]$backendState.baseUrl
    if (-not $backendUri.IsLoopback -or $backendUri.Scheme -ne 'http') { throw 'Stored backend endpoint is not a local HTTP service.' }
    $backendArgs=@{DatabasePort=[int]$backendState.databasePort;ApiPort=$backendUri.Port;Schema=[string]$backendState.schema;ServerId=[string]$backendState.serverId}
}
$backend=& (Join-Path $PSScriptRoot 'Start-Backend.ps1') -BackendRoot $backendLink.root @backendArgs
if ($backend.status -notin @('ready','already-running') -or -not (Test-Path -LiteralPath $serverConfig -PathType Leaf)) { throw 'The local backend is not ready.' }
if ($owned) {
    Write-Output ([pscustomobject]@{status='already-running';pid=$owned.Id;address=('127.0.0.1:'+$Port);backendUrl=$backend.baseUrl;stateFile=$statePath;executable=$serverExe})
    return
}
[void][IO.Directory]::CreateDirectory($sessionRoot)
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$runRoot=Join-Path $sessionRoot $runId;[void][IO.Directory]::CreateDirectory($runRoot)
$log=Join-Path $runRoot 'server.log'
$arguments=$launchPrefix+'/Game/Maps/ForestClearing?FablePlay=1 -server -NullRHI -nosound -unattended -MULTIHOME=127.0.0.1 -port='+$Port+
    ' -FableBackendConfig='+(Quote-OnlineArgument $serverConfig)+' -abslog='+(Quote-OnlineArgument $log)+
    ' -LogCmds="LogInit off,LogNet off,LogLoad off" -ExecCmds="t.MaxFPS 30"'
try { $process=Start-Process -FilePath $serverExe -ArgumentList $arguments -WorkingDirectory $script:FableProjectRoot -WindowStyle Hidden -PassThru }
catch { throw 'Could not start the online server. Private configuration and process arguments were omitted.' }
$state=[ordered]@{status='starting';runId=$runId;projectRoot=[IO.Path]::GetFullPath($script:FableProjectRoot);mode=$mode;pid=$process.Id;executable=$serverExe;processStartedUtc=$process.StartTime.ToUniversalTime().ToString('o');port=$Port;address=('127.0.0.1:'+$Port);backendUrl=$backend.baseUrl;serverConfig=$serverConfig;log=$log;expectedLocalPlayerCount=0;scope='manual server launch; not an automated gameplay pass'}
Write-OnlineState $statePath $state;Write-OnlineState (Join-Path $runRoot 'server-state.json') $state
$deadline=[DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
try {
    do {
        $process.Refresh();if ($process.HasExited) { throw "The online server exited before binding UDP $Port. See $log." }
        if (@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object OwningProcess -EQ $process.Id).Count -gt 0) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "The owned online server did not bind UDP $Port in time. See $log." }
        Start-Sleep -Milliseconds 400
    } while ($true)
    $state.status='listening';$state['readyUtc']=[DateTime]::UtcNow.ToString('o')
} catch {
    $state.status='failed'
    $process.Refresh();if (-not $process.HasExited) { Stop-Process -InputObject $process -ErrorAction SilentlyContinue }
    Write-OnlineState $statePath $state;Write-OnlineState (Join-Path $runRoot 'server-state.json') $state
    throw
}
Write-OnlineState $statePath $state;Write-OnlineState (Join-Path $runRoot 'server-state.json') $state
Write-Host 'Server is listening. Open Play-Online.cmd twice with two different accounts; the server has no player window.'
Write-Output ([pscustomobject]@{status='listening';pid=$process.Id;address=$state.address;backendUrl=$state.backendUrl;stateFile=$statePath;executable=$serverExe;log=$log})

