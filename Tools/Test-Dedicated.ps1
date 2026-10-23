param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$BackendUrl='http://127.0.0.1:5281',
    [string]$BackendConfig,
    [string]$BackendRoot,
    [ValidateRange(1024,65535)][int]$Port=7802,
    [ValidateRange(180,1800)][int]$TimeoutSeconds=650,
    [ValidateRange(900,1920)][int]$Width=900,
    [ValidateRange(600,1080)][int]$Height=600
)
# New accounts only. Never reads server keys/passwords or imports local .sav profiles.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Backend-Link.ps1')
$backendLink=Get-FableBackendLink -BackendRoot $BackendRoot
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run this harness with PowerShell 7.' }
function Read-DedicatedText([string]$Path) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try { $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true);try { return $reader.ReadToEnd() } finally { $reader.Dispose() } } finally { $stream.Dispose() }
}
function Read-DedicatedJson([string]$Path) { return (Read-DedicatedText $Path | ConvertFrom-Json) }
function Save-DedicatedJson([string]$Path,$Value) { [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 70),[Text.UTF8Encoding]::new($false)) }
function Hash-Dedicated([string]$Path) {
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read,262144,[IO.FileOptions]::SequentialScan)
    try {
        $algorithm=[Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToHexString($algorithm.ComputeHash($stream)).ToLowerInvariant() } finally { $algorithm.Dispose() }
    } finally { $stream.Dispose() }
}
function Assert-Dedicated($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Equal-DedicatedJson($A,$B) { return (ConvertTo-Json -InputObject $A -Depth 70 -Compress) -ceq (ConvertTo-Json -InputObject $B -Depth 70 -Compress) }
function Quote-Dedicated([string]$Value) {
    if ($Value.Contains('"') -or $Value.Contains("`n") -or $Value.Contains("`r")) { throw 'Unsafe process argument.' }
    return '"'+$Value+'"'
}
function Api-Dedicated([string]$Method,[string]$Route,$Body=$null,[string]$Token='') {
    $parameters=@{Uri=($BackendUrl.TrimEnd('/')+$Route);Method=$Method;TimeoutSec=25;ErrorAction='Stop'}
    if ($Token) { $parameters.Headers=@{Authorization=('Bearer '+$Token)} }
    if ($null -ne $Body) { $parameters.Body=($Body|ConvertTo-Json -Compress);$parameters.ContentType='application/json' }
    try { return Invoke-RestMethod @parameters } catch { throw "Backend request failed: $Method $Route (credentials omitted)." }
}
function Start-DedicatedProcess([string]$Role,[string]$Phase,[string]$Ticket='') {
    $phaseFolder=Join-Path $runFolder $Phase
    [void][IO.Directory]::CreateDirectory($phaseFolder)
    $exe=if($Role -eq 'server'){$serverExecutable}else{$clientExecutable}
    $arguments=[Collections.Generic.List[string]]::new()
    if ($Role -eq 'server' -or -not $PackagedExecutable) { $arguments.Add((Quote-Dedicated $script:FableProjectFile)) }
    if ($Role -eq 'server') {
        $arguments.Add('/Game/Maps/ForestClearing?FablePlay=1')
        foreach ($a in @('-server','-NullRHI','-nosound','-unattended',('-port='+$Port),'-MULTIHOME=127.0.0.1','-FableDedicatedValidation')) { $arguments.Add($a) }
        $arguments.Add('-FableBackendConfig='+(Quote-Dedicated $BackendConfig))
    } else {
        Assert-Dedicated ($Ticket -match '^[A-Za-z0-9_-]{40,100}$') 'Unexpected one-time join ticket format.'
        $secretTickets.Add($Ticket)
        $arguments.Add(('127.0.0.1:{0}?FableTicket={1}' -f $Port,$Ticket))
        foreach ($a in @('-game','-d3d11','-windowed','-nosplash','-nosound','-unattended','-ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0',('-ResX='+$Width),('-ResY='+$Height),'-ForceRes','-WinY=40')) { $arguments.Add($a) }
        $windowX=if($Role -eq 'client1'){20}else{940};$arguments.Add('-WinX='+$windowX)
        $arguments.Add('-FableDedicatedValidationRole='+$Role)
    }
    $arguments.Add('-FableDedicatedValidationRun='+$runId)
    $arguments.Add('-FableDedicatedValidationDirectory='+(Quote-Dedicated $runFolder))
    $arguments.Add('-FableDedicatedValidationPhase='+$Phase)
    $log=Join-Path $phaseFolder ($Role+'.log')
    $arguments.Add('-abslog='+(Quote-Dedicated $log))
    # The URL contains a short-lived ticket. Disable categories that echo URLs/command lines.
    $arguments.Add('-LogCmds="LogInit off,LogNet off,LogLoad off"')
    $arguments.Add('-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"')
    try { $process=Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory $script:FableProjectRoot -WindowStyle Hidden -PassThru }
    catch { throw "Could not start owned $Role process; process arguments omitted." }
    $ownedProcesses.Add($process)
    $processEvidence.Add([pscustomobject]@{role=$Role;phase=$Phase;pid=$process.Id;executable=$exe;log=$log;startedUtc=$process.StartTime.ToUniversalTime().ToString('o')})
    return $process
}
function Wait-DedicatedServer($Process,[string]$Phase) {
    $deadline=[DateTime]::UtcNow.AddSeconds(90)
    do {
        $Process.Refresh();if($Process.HasExited){throw 'Dedicated server exited before it opened the requested UDP port.'}
        $endpoints=@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $Process.Id)
        if($endpoints.Count -gt 0) { return }
        Start-Sleep -Milliseconds 300
    } while([DateTime]::UtcNow -lt $deadline)
    throw 'Dedicated server did not bind the requested port in 90 seconds.'
}
function Assert-DedicatedProfile($P,[string]$PlayerId,[bool]$IsServer=$false) {
    Assert-Dedicated ($P.player_id -ceq $PlayerId) 'Backend player identity mismatch.'
    Assert-Dedicated ($P.backend_ready -eq $true -and $P.save_pending -eq $false -and $P.save_succeeded -eq $true) 'Profile lacks a durable backend acknowledgement.'
    Assert-Dedicated ($P.revision -ge 1 -and $P.revision -eq $P.acknowledged_revision -and $P.profile_hash -match '^[a-f0-9]{64}$') 'Backend revision/hash mismatch.'
    $expectedSlot=if($IsServer){'backend:'+$PlayerId}else{''}
    Assert-Dedicated ([string]$P.local_profile_slot -ceq $expectedSlot) 'Backend profile slot does not match the diagnostic identity (no local SaveGame slot is allowed).'
    Assert-Dedicated ($P.life_state -eq 0 -and $P.health -gt 0) 'Terminal profile is not active.'
    Assert-Dedicated (@($P.inventory_signatures).Count -eq $P.inventory_count) 'Inventory report count mismatch.'
}
function Assert-DedicatedShots($Report,[string]$Folder,[int]$Minimum) {
    $shots=@($Report.screenshots)
    Assert-Dedicated (@($shots|Select-Object -Unique).Count -ge $Minimum) 'Missing dedicated validation screenshots.'
    $prefix=[IO.Path]::GetFullPath($Folder).TrimEnd('\')+'\'
    foreach($shot in $shots) {
        $full=[IO.Path]::GetFullPath([string]$shot)
        Assert-Dedicated ($full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($full) -ieq '.png' -and (Test-Path -LiteralPath $full -PathType Leaf)) 'Missing or out-of-phase screenshot.'
        $bytes=[IO.File]::ReadAllBytes($full)
        Assert-Dedicated ($bytes.Length -gt 8 -and ($bytes[0..7] -join ',') -ceq '137,80,78,71,13,10,26,10') 'Invalid PNG evidence.'
    }
}
function Wait-DedicatedPhase([string]$Phase,$Server,$Clients) {
    $folder=Join-Path $runFolder $Phase;$processes=@{server=$Server;client1=$Clients.client1;client2=$Clients.client2}
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$reports=@{}
    $lastServerLogCheck=[DateTime]::MinValue
    $serverLog=if($Phase -eq 'reconnect'){Join-Path $runFolder 'fresh\server.log'}else{Join-Path $folder 'server.log'}
    do {
        if(([DateTime]::UtcNow-$lastServerLogCheck).TotalSeconds -ge 1) {
            $lastServerLogCheck=[DateTime]::UtcNow;$loginRejected=$false
            try { $loginRejected=(Read-DedicatedText $serverLog).Contains('FABLE_BACKEND_LOGIN_REJECTED') } catch { }
            if($loginRejected){throw "Owned dedicated server rejected backend login during $Phase; preserved server log contains the diagnostic marker."}
        }
        foreach($role in @('server','client1','client2')) {
            if($reports.ContainsKey($role)){continue}
            $processes[$role].Refresh();if($processes[$role].HasExited){throw "Owned $role exited before terminal report for $Phase."}
            $path=Join-Path $folder ($role+'.json')
            if(-not(Test-Path -LiteralPath $path)){continue}
            try{$report=Read-DedicatedJson $path}catch{continue}
            if($report.status -eq 'failed'){throw "Native $role/$Phase failed: $($report.reason)"}
            if($report.status -ne 'passed'){continue}
            Assert-Dedicated ($report.run_id -ceq $runId -and $report.phase -ceq $Phase -and $report.role -ceq $role -and $report.process_id -eq $processes[$role].Id) 'Report run/phase/role/PID mismatch.'
            Assert-Dedicated ($report.report_snapshot_frozen -eq $true -and $report.player_count -eq 2 -and $report.enemy_count -eq 0 -and $report.projectile_count -eq 0) 'Terminal snapshot is not frozen and safe.'
            $log=if($role -eq 'server' -and $Phase -eq 'reconnect'){Join-Path $runFolder 'fresh\server.log'}else{Join-Path $folder ($role+'.log')}
            try {$logText=Read-DedicatedText $log}catch{continue}
            if($logText -notmatch ('FABLE_DEDICATED_VALIDATION_RESULT role='+$role+' phase='+$Phase+' status=passed attempts=\d+')){continue}
            $reports[$role]=$report
        }
        if($reports.Count -eq 3){break}
        Start-Sleep -Milliseconds 200
    } while([DateTime]::UtcNow -lt $deadline)
    Assert-Dedicated ($reports.Count -eq 3) "Three-process phase timed out: $Phase"
    $serverReport=$reports.server
    Assert-Dedicated ($serverReport.network_mode -ceq 'dedicated_server' -and $serverReport.controller_count -eq 2 -and $serverReport.local_controller_count -eq 0) 'Actual server is not NM_DedicatedServer with two remote controllers.'
    foreach($key in @('dedicated_no_local_player','distinct_profiles','durable_acknowledged')) { Assert-Dedicated ($serverReport.checks.$key -eq $true) "Missing server observation $key." }
    Assert-Dedicated (@($serverReport.players).Count -eq 2 -and @($serverReport.players.player_id|Select-Object -Unique).Count -eq 2) 'Server did not report two distinct identities.'
    $db=@{}
    foreach($role in @('client1','client2')) {
        $r=$reports[$role];$account=$accounts[$role]
        Assert-Dedicated ($r.network_mode -ceq 'client' -and $r.controller_count -eq 1 -and $r.local_controller_count -eq 1) 'A client unexpectedly owns server authority.'
        Assert-DedicatedProfile $r.profile $account.playerId
        Assert-Dedicated ($r.profile.class -eq $account.initialClass) 'Backend initial class changed.'
        $authoritative=@($serverReport.players|Where-Object player_id -CEQ $account.playerId)
        Assert-Dedicated ($authoritative.Count -eq 1) 'Server profile mapping is not unique.'
        $sp=$authoritative[0];Assert-DedicatedProfile $sp $account.playerId $true
        foreach($field in @('profile_hash','profile_signature','equipped_weapon','inventory_count')) { Assert-Dedicated ($r.profile.$field -ceq $sp.$field) "Client/server $field mismatch." }
        Assert-Dedicated (Equal-DedicatedJson @($r.profile.inventory_signatures) @($sp.inventory_signatures)) 'Client/server item metadata differs.'
        foreach($field in @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions')) { Assert-Dedicated ($r.profile.wallet.$field -eq $sp.wallet.$field) "Replicated wallet mismatch: $field" }
        $me=Api-Dedicated GET '/v1/me' $null $account.accessToken
        Assert-Dedicated ($me.playerId -ceq $account.playerId -and $me.profileHash -ceq $sp.profile_hash -and $me.revision -eq $sp.acknowledged_revision -and $me.leaseActive -eq $true) 'DB acknowledgement/hash/lease does not match the actual server.'
        foreach($field in @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions')) { Assert-Dedicated ($me.wallet.$field -eq $sp.wallet.$field) "DB wallet mismatch: $field" }
        $db[$role]=$me
        if($Phase -eq 'fresh') {
            foreach($key in @('authenticated_profile','remote_shop_rejected','invalid_item_rejected','health_potion_used','real_combat','quest_gold_exact','weapon_purchase_exact','potion_purchase_exact','owned_weapon_equipped','movement_replicated','durable_acknowledged','safe_checkpoint')) { Assert-Dedicated ($r.checks.$key -eq $true) "Missing fresh observation: $role/$key" }
            Assert-Dedicated ($r.movement_cm -gt 200 -and $r.remote_movement_cm -gt 100 -and $r.attack_inputs -gt 0 -and $r.skill_inputs -gt 0 -and $r.profile.skill_casts -gt 0 -and $r.quiet_seconds -ge 6) 'Client normal-input movement/combat or safe checkpoint was not observed.'
            Assert-Dedicated ($sp.movement_cm -gt 200 -and $sp.incoming_damage_seen -eq $true -and $sp.potion_consumed_seen -eq $true -and $serverReport.enemy_damage_seen -eq $true) 'Server did not observe actual movement, damage and item consumption.'
            Assert-Dedicated ($sp.forest_quest -eq 3 -and $sp.quest_kills -eq 3 -and $sp.inventory_count -eq 6 -and $sp.equipped_weapon -gt 4 -and $sp.wallet.gold -eq 20 -and $sp.health_potions -eq 3) 'Fresh reward/purchase/consumption totals differ from the normal quest/shop flow.'
            Assert-DedicatedShots $r $folder 4
        } else {
            Assert-Dedicated ($r.checks.runtime_restored -eq $true) 'Restored runtime observation missing.'
            $fresh=$summary.phases.fresh.$role.profile
            foreach($field in @('profile_hash','profile_signature','equipped_weapon','inventory_count')) { Assert-Dedicated ($r.profile.$field -ceq $fresh.$field) "Reconnect/restart changed $role/$field." }
            Assert-Dedicated (Equal-DedicatedJson @($r.profile.inventory_signatures) @($fresh.inventory_signatures)) 'Item roll, enhancement or durability changed after restore.'
            foreach($field in @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions')) { Assert-Dedicated ($r.profile.wallet.$field -eq $fresh.wallet.$field) 'Wallet changed after restore.' }
            Assert-DedicatedShots $r $folder 1
        }
    }
    return [ordered]@{server=$reports.server;client1=$reports.client1;client2=$reports.client2;dbProfiles=$db}
}
function Quit-DedicatedClients([string]$Phase,$Server,$Clients) {
    Save-DedicatedJson (Join-Path $runFolder "$Phase\quit-request.json") @{run_id=$runId;phase=$Phase}
    $deadline=[DateTime]::UtcNow.AddSeconds(70)
    do {
        $closed=$true;foreach($role in @('client1','client2')){$Clients[$role].Refresh();$closed=$closed -and $Clients[$role].HasExited}
        $zero=$false;try{$state=Read-DedicatedJson (Join-Path $runFolder 'connection-state.json');$zero=$state.run_id -ceq $runId -and $state.phase -ceq $Phase -and $state.process_id -eq $Server.Id -and $state.controller_count -eq 0 -and $state.local_controller_count -eq 0}catch{}
        $released=$true;$releasedProfiles=@{}
        foreach($role in @('client1','client2')) { $me=Api-Dedicated GET '/v1/me' $null $accounts[$role].accessToken;$releasedProfiles[$role]=$me;$released=$released -and ($me.leaseActive -eq $false) }
        if($closed -and $zero -and $released) {
            foreach($role in @('client1','client2')) { Assert-Dedicated ($releasedProfiles[$role].profileHash -ceq $summary.phases[$Phase].$role.profile.profile_hash) 'Logout changed the acknowledged safe profile.' }
            return [ordered]@{clientProcessesExited=$true;serverControllerCount=0;leasesReleased=$true;dbProfiles=$releasedProfiles}
        }
        Start-Sleep -Milliseconds 500
    } while([DateTime]::UtcNow -lt $deadline)
    throw 'Graceful client disconnect did not release both server controllers and backend leases.'
}
function Quit-DedicatedServer([string]$Phase,$Server) {
    Save-DedicatedJson (Join-Path $runFolder "$Phase\quit-server-request.json") @{run_id=$runId;phase=$Phase}
    if(-not $Server.WaitForExit(30000)){throw 'Owned dedicated server did not exit gracefully.'}
}
function New-DedicatedClients([string]$Phase) {
    $clients=@{}
    foreach($role in @('client1','client2')) {
        $ticket=Api-Dedicated POST '/v1/join-tickets' @{} $accounts[$role].accessToken
        $clients[$role]=Start-DedicatedProcess $role $Phase ([string]$ticket.ticket)
    }
    return $clients
}
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$runFolder=Join-Path $script:FableProjectRoot ('Saved\DedicatedValidation\'+$runId)
[void][IO.Directory]::CreateDirectory($runFolder)
if(-not $BackendConfig){$BackendConfig=$backendLink.serverConfig}
$BackendConfig=[IO.Path]::GetFullPath($BackendConfig)
$ownedProcesses=[Collections.Generic.List[System.Diagnostics.Process]]::new()
$processEvidence=[Collections.Generic.List[object]]::new()
$secretTickets=[Collections.Generic.List[string]]::new()
$accounts=@{};$originalFiles=@();$serverExecutable='';$clientExecutable='';$stage='preflight'
$summary=[ordered]@{runId=$runId;status='running';mode='';phases=[ordered]@{};disconnects=[ordered]@{};backendUrl=$BackendUrl;serverConfig=$BackendConfig;processes=@();originalFilesPreserved=@();startedUtc=[DateTime]::UtcNow.ToString('o')}
try {
    Assert-Dedicated (Test-Path -LiteralPath $BackendConfig -PathType Leaf) 'Private backend server configuration is missing.'
    $uri=[Uri]$BackendUrl
    Assert-Dedicated ($uri.IsLoopback -and $uri.Scheme -eq 'http' -and $uri.AbsolutePath -eq '/') 'This harness is restricted to an existing local backend.'
    Assert-Dedicated (@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue).Count -eq 0) 'The requested UE UDP port is already in use.'
    if($PackagedExecutable) {
        $inner=(Resolve-Path -LiteralPath $PackagedExecutable).Path
        if ($inner -notmatch '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
            $inner=Join-Path (Split-Path -Parent $inner) 'Fablewood\Binaries\Win64\Fablewood.exe'
            Assert-Dedicated (Test-Path -LiteralPath $inner -PathType Leaf) 'Supply the packaged inner game executable or archive bootstrap.'
            $inner=(Resolve-Path -LiteralPath $inner).Path
        }
        $engine=Resolve-FableEngine $EngineRoot;Assert-FableReady
        $serverExecutable=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
        $clientExecutable=$inner
        $summary.mode='editor-dedicated-packaged-clients'
    } else {
        $engine=Resolve-FableEngine $EngineRoot;Assert-FableReady
        $serverExecutable=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$clientExecutable=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
        $summary.mode='editor-dedicated'
    }
    $summary.serverExecutable=$serverExecutable;$summary.clientExecutable=$clientExecutable
    $summary.serverSHA256Before=Hash-Dedicated $serverExecutable
    $summary.clientSHA256Before=if([string]::Equals($serverExecutable,$clientExecutable,[StringComparison]::OrdinalIgnoreCase)){$summary.serverSHA256Before}else{Hash-Dedicated $clientExecutable}
    $files=@(Get-ChildItem -LiteralPath (Join-Path $script:FableProjectRoot 'Saved') -Recurse -File | Where-Object { $_.Extension -ieq '.sav' -or $_.Name -ceq 'completion.json' })
    $buildFolder=Join-Path $script:FableProjectRoot 'Builds'
    if(Test-Path -LiteralPath $buildFolder){$files+=@(Get-ChildItem -LiteralPath $buildFolder -Recurse -File -Filter '*.sav')}
    $originalFiles=@($files|Sort-Object FullName -Unique|ForEach-Object{[pscustomobject]@{path=$_.FullName;before=Hash-Dedicated $_.FullName}})
    Assert-Dedicated ((Api-Dedicated GET '/health').status -eq 'ok') 'Backend health is not ready.'
    $stage='create-accounts'
    foreach($pair in @(@('client1',1),@('client2',2))) {
        $role=[string]$pair[0];$initialClass=[int]$pair[1]
        $name='d'+[Guid]::NewGuid().ToString('N').Substring(0,24)
        $password=[Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
        $account=Api-Dedicated POST '/v1/accounts' @{name=$name;password=$password;initialClass=$initialClass}
        $session=Api-Dedicated POST '/v1/sessions' @{name=$name;password=$password}
        $accounts[$role]=@{playerId=[string]$account.playerId;initialClass=$initialClass;accessToken=[string]$session.accessToken}
        $password=$null
    }
    $summary.accounts=@($accounts.GetEnumerator()|Sort-Object Name|ForEach-Object{[pscustomobject]@{role=$_.Name;playerId=$_.Value.playerId;initialClass=$_.Value.initialClass}})
    $stage='fresh';Write-Host "Dedicated validation $runId fresh: one server and two remote clients."
    $server=Start-DedicatedProcess server fresh;Wait-DedicatedServer $server fresh
    $clients=New-DedicatedClients fresh
    $summary.phases.fresh=Wait-DedicatedPhase fresh $server $clients
    $summary.disconnects.fresh=Quit-DedicatedClients fresh $server $clients
    $stage='reconnect';Write-Host 'Dedicated reconnect: same server, released leases, new one-time tickets.'
    Save-DedicatedJson (Join-Path $runFolder 'phase-request.json') @{run_id=$runId;phase='reconnect'}
    $clients=New-DedicatedClients reconnect
    $summary.phases.reconnect=Wait-DedicatedPhase reconnect $server $clients
    $summary.disconnects.reconnect=Quit-DedicatedClients reconnect $server $clients
    Quit-DedicatedServer reconnect $server
    $stage='backend-restart';Write-Host 'Restarting the owned backend API; PostgreSQL data remains in its existing volume.'
    $backendStatePath=$backendLink.stateFile
    $beforeBackend=Read-DedicatedJson $backendStatePath
    Assert-Dedicated ($beforeBackend.baseUrl.TrimEnd('/') -ceq $BackendUrl.TrimEnd('/') -and [IO.Path]::GetFullPath($beforeBackend.serverConfig) -ieq $BackendConfig) 'Backend restart would target a different service.'
    $restart=& (Join-Path $PSScriptRoot 'Start-Backend.ps1') -BackendRoot $backendLink.root -Restart -DatabasePort ([int]$beforeBackend.databasePort) -ApiPort $uri.Port -Schema ([string]$beforeBackend.schema) -ServerId ([string]$beforeBackend.serverId)
    $afterBackend=Read-DedicatedJson $backendStatePath
    Assert-Dedicated ($afterBackend.backendPid -ne $beforeBackend.backendPid -and $afterBackend.containerName -ceq $beforeBackend.containerName -and $afterBackend.volume -ceq $beforeBackend.volume) 'Backend restart did not preserve its database container/volume.'
    $summary.backendRestart=[ordered]@{beforePid=$beforeBackend.backendPid;afterPid=$afterBackend.backendPid;containerName=$afterBackend.containerName;volume=$afterBackend.volume;databasePreserved=$true;apiRestarted=$true}
    $stage='resume';Write-Host 'Dedicated resume: restarted API and new dedicated server, existing account profiles.'
    Save-DedicatedJson (Join-Path $runFolder 'phase-request.json') @{run_id=$runId;phase='resume'}
    $server=Start-DedicatedProcess server resume;Wait-DedicatedServer $server resume
    $clients=New-DedicatedClients resume
    $summary.phases.resume=Wait-DedicatedPhase resume $server $clients
    $summary.disconnects.resume=Quit-DedicatedClients resume $server $clients
    Quit-DedicatedServer resume $server
    $summary.status='passed'
} catch {
    $summary.status='failed';$summary.failedStage=$stage;$summary.error=$_.Exception.Message
    Write-Warning ("Dedicated validation failed at $stage. Evidence is retained in $runFolder.")
} finally {
    # Cleanup only handles started by this invocation. Failure cleanup is not evidence of graceful logout.
    foreach($process in $ownedProcesses) { try{$process.Refresh();if(-not $process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction Stop;$process.WaitForExit()}}catch{} }
    $summary.processes=$processEvidence.ToArray()
    $redactions=0
    foreach($log in @(Get-ChildItem -LiteralPath $runFolder -Recurse -File -Filter '*.log')) {
        $text=Read-DedicatedText $log.FullName;$changed=$false
        foreach($ticket in $secretTickets) { if($text.Contains($ticket)){$text=$text.Replace($ticket,'[REDACTED_ONE_TIME_TICKET]');$redactions++;$changed=$true} }
        if($changed){[IO.File]::WriteAllText($log.FullName,$text,[Text.UTF8Encoding]::new($false))}
    }
    $summary.oneTimeTicketLogRedactions=$redactions
    $preserved=[Collections.Generic.List[object]]::new()
    foreach($file in $originalFiles) {
        $after=if(Test-Path -LiteralPath $file.path){Hash-Dedicated $file.path}else{'missing'}
        $preserved.Add([pscustomobject]@{path=$file.path;before=$file.before;after=$after})
        if($after -cne $file.before){$summary.status='failed';$summary.error='An original save or completion marker changed.'}
    }
    $summary.originalFilesPreserved=$preserved.ToArray()
    $currentSaves=@(Get-ChildItem -LiteralPath (Join-Path $script:FableProjectRoot 'Saved') -Recurse -File -Filter '*.sav')
    $buildFolder=Join-Path $script:FableProjectRoot 'Builds'
    if(Test-Path -LiteralPath $buildFolder){$currentSaves+=@(Get-ChildItem -LiteralPath $buildFolder -Recurse -File -Filter '*.sav')}
    $knownSavePaths=@($originalFiles|Where-Object {$_.path -like '*.sav'}|ForEach-Object {$_.path})
    $unexpectedSaves=@($currentSaves|Where-Object {$_.FullName -notin $knownSavePaths}|ForEach-Object {$_.FullName})
    $summary.unexpectedLocalSaves=$unexpectedSaves
    if($unexpectedSaves.Count -gt 0){$summary.status='failed';$summary.error='Dedicated backend validation unexpectedly created local .sav files.'}
    if($serverExecutable -and (Test-Path -LiteralPath $serverExecutable)){$summary.serverSHA256After=Hash-Dedicated $serverExecutable;if($summary.serverSHA256After -cne $summary.serverSHA256Before){$summary.status='failed';$summary.error='Server executable changed.'}}
    if($clientExecutable -and (Test-Path -LiteralPath $clientExecutable)){
        $summary.clientSHA256After=if([string]::Equals($serverExecutable,$clientExecutable,[StringComparison]::OrdinalIgnoreCase)){$summary.serverSHA256After}else{Hash-Dedicated $clientExecutable}
        if($summary.clientSHA256After -cne $summary.clientSHA256Before){$summary.status='failed';$summary.error='Client executable changed.'}
    }
    $summary.finishedUtc=[DateTime]::UtcNow.ToString('o')
    Save-DedicatedJson (Join-Path $runFolder 'summary.json') $summary
}
Write-Host (Join-Path $runFolder 'summary.json')
if($summary.status -ne 'passed'){throw 'Dedicated validation failed; see its preserved summary.'}
