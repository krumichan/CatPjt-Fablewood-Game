param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$BackendUrl='http://127.0.0.1:5281',
    [string]$BackendConfig,
    [string]$BackendRoot,
    [switch]$InventoryScenario,
    [string]$RestoreAccountsPath,
    [ValidateRange(1024,65535)][int]$Port=7802,
    [ValidateRange(180,1800)][int]$TimeoutSeconds=650,
    [ValidateRange(900,1920)][int]$Width=900,
    [ValidateRange(600,1080)][int]$Height=600
)
# Fresh accounts by default; optional private restoration accounts are prepared separately.
# Private files are read only for identity checks; credentials are never logged. Local .sav profiles are never imported.
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

function Equal-DedicatedValue($A,$B) {
    if($null -eq $A -or $null -eq $B){return $null -eq $A -and $null -eq $B}
    if($A -is [string] -or $B -is [string]){return ($A -is [string] -and $B -is [string] -and $A -ceq $B)}
    if($A -is [bool] -or $B -is [bool]){return ($A -is [bool] -and $B -is [bool] -and $A -eq $B)}
    $numeric=@([byte],[short],[int],[long],[float],[double],[decimal])
    if($A.GetType() -in $numeric -or $B.GetType() -in $numeric){return ($A.GetType() -in $numeric -and $B.GetType() -in $numeric -and [decimal]$A -eq [decimal]$B)}
    if($A -is [array] -or $B -is [array]){
        if($A -isnot [array] -or $B -isnot [array] -or $A.Count -ne $B.Count){return $false}
        for($i=0;$i -lt $A.Count;$i++){if(-not(Equal-DedicatedValue $A[$i] $B[$i])){return $false}}
        return $true
    }
    $aNames=@($A.PSObject.Properties.Name|Sort-Object -CaseSensitive)
    $bNames=@($B.PSObject.Properties.Name|Sort-Object -CaseSensitive)
    if(-not(Equal-DedicatedJson $aNames $bNames)){return $false}
    foreach($name in $aNames){if(-not(Equal-DedicatedValue $A.$name $B.$name)){return $false}}
    return $true
}
function Assert-DedicatedInventory($Profile,$Snapshot) {
    Assert-Dedicated ($Snapshot.version -eq 1 -and $Snapshot.heroClass -eq $Profile.class) 'Central inventory contract/class mismatch.'
    Assert-Dedicated (Equal-DedicatedValue @($Profile.central_items) @($Snapshot.items)) 'Replicated item fields/identities/locations differ from the central item rows.'
    Assert-Dedicated ($Profile.equipped_weapon_instance_id -ceq $Snapshot.equippedWeaponInstanceId -and $Profile.equipped_armor_instance_id -ceq $Snapshot.equippedArmorInstanceId) 'Central equipped UUID references mismatch.'
    Assert-Dedicated (@($Snapshot.items).Count -eq $Profile.inventory_count+$Profile.stash_count) 'Central item count does not equal bag plus storage.'
    $ids=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $maxLocal=0
    foreach($item in $Snapshot.items){
        Assert-Dedicated ($item.instanceId -cmatch '^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$' -and $ids.Add($item.instanceId)) 'Item UUID is absent or duplicated.'
        $maxLocal=[Math]::Max($maxLocal,[int]$item.localId)
    }
    Assert-Dedicated ($Snapshot.nextLocalId -gt $maxLocal) 'Canonical next item ID was not preserved.'
}
function Assert-DedicatedInventoryEvents($Report) {
    Assert-Dedicated ($Report.inventory_validation -eq $true) 'Native inventory scenario was not enabled.'
    foreach($key in @('inventory_sale_exact','inventory_repair_exact','inventory_enhance_exact','inventory_deposit_preserved','inventory_withdraw_preserved','inventory_final_stash')){Assert-Dedicated ($Report.checks.$key -eq $true) "Inventory action observation missing: $key"}
    $events=@($Report.inventory_events)
    Assert-Dedicated (Equal-DedicatedJson @($events.action) @('sale','repair','enhance','deposit','withdraw','deposit')) 'Inventory action sequence mismatch.'
    Assert-Dedicated ($events[0].before_wallet.gold -eq 20 -and $events[0].before_wallet.ore -eq 7) 'Base quest/shop totals were not retained before inventory actions.'
    $gold=20;$ore=7;$previousWallet=$null
    foreach($event in $events){
        if($null -ne $previousWallet){Assert-Dedicated (Equal-DedicatedValue $previousWallet $event.before_wallet) 'Unexpected economy change between inventory actions.'}
        foreach($field in @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions')){
            $expectedDelta=if($field -eq 'gold'){[int]$event.gold_delta}elseif($field -eq 'ore'){[int]$event.ore_delta}else{0}
            Assert-Dedicated (($event.after_wallet.$field-$event.before_wallet.$field) -eq $expectedDelta) "Inventory wallet delta mismatch: $field"
        }
        Assert-Dedicated ($event.revision -gt 0) 'Action was recorded before durable acknowledgement.'
        $gold+=[int]$event.gold_delta;$ore+=[int]$event.ore_delta
        if($event.action -eq 'sale'){
            Assert-Dedicated ($null -eq $event.item_after -and $event.gold_delta -gt 0 -and $event.ore_delta -eq 0) 'Sale did not remove one owned item for a positive exact price.'
        }else{
            $before=$event.item_before|ConvertTo-Json -Depth 20|ConvertFrom-Json
            $after=$event.item_after
            Assert-Dedicated ($before.instanceId -ceq $after.instanceId -and $before.localId -eq $after.localId) 'Item identity changed during an inventory action.'
            if($event.action -eq 'repair'){
                Assert-Dedicated ($before.durability -lt $before.maxDurability -and $after.durability -eq $after.maxDurability -and $event.gold_delta -lt 0) 'Repair did not restore actual combat wear.'
                $before.durability=$after.durability
            }elseif($event.action -eq 'enhance'){
                Assert-Dedicated ($after.upgrade -eq $before.upgrade+1 -and $event.gold_delta -eq -30 -and $event.ore_delta -eq -2) 'First enhancement result or cost mismatch.'
                $before.upgrade=$after.upgrade
            }else{
                $before.location=$after.location;$before.position=$after.position
                Assert-Dedicated ($event.gold_delta -eq 0 -and $event.ore_delta -eq 0) 'Storage move changed currency.'
            }
            Assert-Dedicated (Equal-DedicatedValue $before $after) 'Unrelated item metadata changed during an inventory action.'
        }
        $previousWallet=$event.after_wallet
    }
    Assert-Dedicated ($Report.profile.wallet.gold -eq $gold -and $Report.profile.wallet.ore -eq $ore -and $Report.profile.inventory_count -eq 4 -and $Report.profile.stash_count -eq 1) 'Final item/economy totals differ from the observed actions.'
    $stored=@($Report.profile.central_items|Where-Object location -EQ 1)
    Assert-Dedicated ($stored.Count -eq 1 -and $stored[0].upgrade -eq 1) 'Enhanced item was not stored with its UUID and enhancement.'
}

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
    if($InventoryScenario){$arguments.Add('-FableInventoryValidation')}
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
    $serverLog=if($Phase -eq 'reconnect'){Join-Path $runFolder ($baselinePhase+'\server.log')}else{Join-Path $folder 'server.log'}
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
            $log=if($role -eq 'server' -and $Phase -eq 'reconnect'){Join-Path $runFolder ($baselinePhase+'\server.log')}else{Join-Path $folder ($role+'.log')}
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
    $db=@{};$allItemIds=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
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
        $items=Api-Dedicated GET '/v1/me/inventory' $null $account.accessToken
        Assert-Dedicated ($items.playerId -ceq $account.playerId -and $items.revision -eq $sp.acknowledged_revision -and $items.profileHash -ceq $sp.profile_hash -and $items.inventoryVersion -eq 1) 'Central inventory ownership/revision/profile acknowledgement mismatch.'
        Assert-DedicatedInventory $r.profile $items.inventory
        Assert-DedicatedInventory $sp $items.inventory
        foreach($item in $items.inventory.items){Assert-Dedicated ($allItemIds.Add($item.instanceId)) 'Two players share the same item UUID.'}
        $me|Add-Member -NotePropertyName centralInventory -NotePropertyValue $items
        $db[$role]=$me
        if($Phase -eq 'fresh') {
            foreach($key in @('authenticated_profile','remote_shop_rejected','invalid_item_rejected','health_potion_used','real_combat','quest_gold_exact','weapon_purchase_exact','potion_purchase_exact','owned_weapon_equipped','movement_replicated','durable_acknowledged','safe_checkpoint')) { Assert-Dedicated ($r.checks.$key -eq $true) "Missing fresh observation: $role/$key" }
            Assert-Dedicated ($r.movement_cm -gt 200 -and $r.remote_movement_cm -gt 100 -and $r.attack_inputs -gt 0 -and $r.skill_inputs -gt 0 -and $r.profile.skill_casts -gt 0 -and $r.quiet_seconds -ge 6) 'Client normal-input movement/combat or safe checkpoint was not observed.'
            Assert-Dedicated ($sp.movement_cm -gt 200 -and $sp.incoming_damage_seen -eq $true -and $sp.potion_consumed_seen -eq $true -and $serverReport.enemy_damage_seen -eq $true) 'Server did not observe actual movement, damage and item consumption.'
            Assert-Dedicated ($sp.forest_quest -eq 3 -and $sp.quest_kills -eq 3 -and $sp.health_potions -eq 3) 'Fresh quest/consumption totals differ from the normal flow.'
            if($InventoryScenario){Assert-DedicatedInventoryEvents $r}
            else{Assert-Dedicated ($sp.inventory_count -eq 6 -and $sp.equipped_weapon -gt 4 -and $sp.wallet.gold -eq 20) 'Fresh reward/purchase totals differ from the normal quest/shop flow.'}
            Assert-DedicatedShots $r $folder 4
        } else {
            Assert-Dedicated ($r.checks.runtime_restored -eq $true) 'Restored runtime observation missing.'
            if($Phase -eq $baselinePhase){
                $expected=$accounts[$role].expectedProfile
                Assert-Dedicated ($r.profile.profile_signature -ceq $expected.profile_signature) 'Legacy gameplay state changed during v8-to-v9 migration.'
                Assert-Dedicated (Equal-DedicatedJson @($r.profile.inventory_signatures) @($expected.inventory_signatures)) 'Legacy item performance/rolls/enhancement/durability changed during migration.'
                Assert-Dedicated (Equal-DedicatedValue $r.profile.wallet $expected.wallet) 'Legacy wallet changed during migration.'
                Assert-DedicatedShots $r $folder 1
                continue
            }
            $fresh=$summary.phases[$baselinePhase].$role.profile
            Assert-Dedicated (Equal-DedicatedValue @($r.profile.central_items) @($fresh.central_items)) 'Item UUID/metadata/location changed after reconnect or API/server restart.'
            Assert-Dedicated ($r.profile.equipped_weapon_instance_id -ceq $fresh.equipped_weapon_instance_id -and $r.profile.equipped_armor_instance_id -ceq $fresh.equipped_armor_instance_id) 'Equipped UUID changed after restore.'
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
$baselinePhase=if($RestoreAccountsPath){'legacy'}else{'fresh'}
$summary=[ordered]@{runId=$runId;status='running';mode='';phases=[ordered]@{};disconnects=[ordered]@{};backendUrl=$BackendUrl;serverConfig=$BackendConfig;processes=@();originalFilesPreserved=@();startedUtc=[DateTime]::UtcNow.ToString('o')}
try {
    Assert-Dedicated (Test-Path -LiteralPath $BackendConfig -PathType Leaf) 'Private backend server configuration is missing.'
    $uri=[Uri]$BackendUrl
    Assert-Dedicated ($uri.IsLoopback -and $uri.Scheme -eq 'http' -and $uri.AbsolutePath -eq '/') 'This harness is restricted to an existing local backend.'
    # Verify the existing API/DB identity before creating accounts or starting UE.
    # Never let a late restart discover that gameplay used a different backend.
    $backendStatePath=$backendLink.stateFile
    $credentialsPath=Join-Path $backendLink.root 'Saved\Backend\backend-credentials.json'
    Assert-Dedicated ((Test-Path -LiteralPath $backendStatePath -PathType Leaf) -and (Test-Path -LiteralPath $credentialsPath -PathType Leaf)) 'Existing backend ownership state or private settings are missing.'
    try {
        $backendPreflightState=Read-DedicatedJson $backendStatePath
        $privateConfig=Read-DedicatedJson $BackendConfig
        $privateSettings=Read-DedicatedJson $credentialsPath
    } catch { throw 'Backend identity metadata could not be read (private contents omitted).' }
    foreach($name in @('status','projectRoot','baseUrl','serverConfig','serverId','databasePort','schema','containerName','volume')) {
        Assert-Dedicated ($null -ne $backendPreflightState.PSObject.Properties[$name]) 'Backend ownership state is incomplete.'
    }
    foreach($name in @('projectRoot','databasePort','apiPort','schema','serverId')) {
        Assert-Dedicated ($null -ne $privateSettings.PSObject.Properties[$name]) 'Private backend identity settings are incomplete.'
    }
    foreach($name in @('url','serverId')) { Assert-Dedicated ($null -ne $privateConfig.PSObject.Properties[$name]) 'Private UE server configuration is incomplete.' }
    Assert-Dedicated ($backendPreflightState.status -ceq 'ready' -and $backendPreflightState.baseUrl.TrimEnd('/') -ceq $BackendUrl.TrimEnd('/') -and $privateConfig.url.TrimEnd('/') -ceq $BackendUrl.TrimEnd('/')) 'API URL differs between the requested endpoint, backend state, or private UE configuration.'
    Assert-Dedicated ([IO.Path]::GetFullPath([string]$backendPreflightState.projectRoot) -ieq $backendLink.root -and [IO.Path]::GetFullPath([string]$privateSettings.projectRoot) -ieq $backendLink.root -and [IO.Path]::GetFullPath([string]$backendPreflightState.serverConfig) -ieq $BackendConfig) 'Backend root or private server configuration belongs to a different service.'
    Assert-Dedicated ([int]$privateSettings.apiPort -eq $uri.Port -and [int]$privateSettings.databasePort -eq [int]$backendPreflightState.databasePort -and [int]$backendPreflightState.databasePort -ge 1024 -and [int]$backendPreflightState.databasePort -le 65535 -and [string]$privateSettings.schema -ceq [string]$backendPreflightState.schema) 'Backend API port, database port, or schema differs from the private settings.'
    Assert-Dedicated ([string]$privateConfig.serverId -ceq [string]$backendPreflightState.serverId -and [string]$privateSettings.serverId -ceq [string]$backendPreflightState.serverId) 'Backend server identity differs from the private settings.'
    foreach($binding in @(@{name='containerName';state='containerName';legacy='fablewood-postgres'},@{name='volumeName';state='volume';legacy='fablewood-postgres-data'},@{name='composeProject';state='composeProject';legacy='fablewood-backend'})) {
        $setting=$privateSettings.PSObject.Properties[$binding.name]
        $stateSetting=$backendPreflightState.PSObject.Properties[$binding.state]
        $expected=if($null -eq $setting){$binding.legacy}else{[string]$setting.Value}
        $actual=if($null -eq $stateSetting){$binding.legacy}else{[string]$stateSetting.Value}
        Assert-Dedicated ($expected -ceq $actual) 'Database container, volume, or compose ownership differs from the private settings.'
    }
    $privateConfig=$null;$privateSettings=$null
    $summary.backendPreflight=[ordered]@{status='passed';checkedUtc=[DateTime]::UtcNow.ToString('o');stateFile=$backendStatePath;baseUrl=$BackendUrl;databasePort=[int]$backendPreflightState.databasePort;schema=[string]$backendPreflightState.schema;serverId=[string]$backendPreflightState.serverId;containerName=[string]$backendPreflightState.containerName;volume=[string]$backendPreflightState.volume;configurationMatched=$true}
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
    if($RestoreAccountsPath){
        Assert-Dedicated (-not $InventoryScenario) 'Legacy restoration and fresh inventory actions are separate validation cohorts.'
        $prepared=Read-DedicatedJson ([IO.Path]::GetFullPath($RestoreAccountsPath))
        Assert-Dedicated ($prepared.status -ceq 'seeded-legacy-v8' -and @($prepared.accounts).Count -eq 2) 'Expected a separately prepared private legacy migration cohort.'
        $stage='legacy-preflight'
        $summary.legacyPreflight=[ordered]@{status='running';checkedUtc=[DateTime]::UtcNow.ToString('o');accounts=@()}
        $legacyOwners=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach($entry in $prepared.accounts){
            Assert-Dedicated ($entry.role -in @('client1','client2') -and -not $accounts.ContainsKey($entry.role) -and $entry.accessToken -cmatch '^[A-Za-z0-9_-]{43}$') 'Invalid restoration account context (credentials suppressed).'
            $playerGuid=[Guid]::Parse($entry.playerId)
            Assert-Dedicated ($playerGuid -ne [Guid]::Empty -and [string]$entry.playerId -ceq $playerGuid.ToString('D') -and $legacyOwners.Add([string]$entry.playerId) -and [int]$entry.initialClass -in @(0,1,2)) 'Restoration accounts must have two distinct canonical owners and valid classes.'
            Assert-Dedicated ($null -ne $entry.expectedProfile -and $null -ne $entry.expectedProfile.PSObject.Properties['profile_hash'] -and [string]$entry.expectedProfile.profile_hash -cmatch '^[a-f0-9]{64}$') 'Legacy restoration expectation lacks a valid original profile hash.'
            $beforeInventory=Api-Dedicated GET '/v1/me/inventory' $null ([string]$entry.accessToken)
            foreach($name in @('playerId','revision','profileHash','inventoryVersion','inventory')) { Assert-Dedicated ($null -ne $beforeInventory.PSObject.Properties[$name]) 'Legacy preflight response is incomplete.' }
            Assert-Dedicated ([string]$beforeInventory.playerId -ceq [string]$entry.playerId) 'Legacy access token belongs to a different player.'
            Assert-Dedicated ($beforeInventory.inventoryVersion -eq 0 -and $null -eq $beforeInventory.inventory) 'Legacy restoration requires an unconverted inventoryVersion 0 account with null inventory; already migrated accounts cannot be reused.'
            Assert-Dedicated ([string]$beforeInventory.profileHash -ceq [string]$entry.expectedProfile.profile_hash) 'Legacy database profile hash differs from the original v8 expectation before server startup.'
            $summary.legacyPreflight.accounts+=@([pscustomobject]@{role=[string]$entry.role;playerId=[string]$beforeInventory.playerId;revision=$beforeInventory.revision;profileHash=[string]$beforeInventory.profileHash;expectedProfileHash=[string]$entry.expectedProfile.profile_hash;inventoryVersion=$beforeInventory.inventoryVersion;inventoryIsNull=($null -eq $beforeInventory.inventory);ownerMatched=$true;originalHashMatched=$true})
            $accounts[$entry.role]=@{playerId=[string]$entry.playerId;initialClass=[int]$entry.initialClass;accessToken=[string]$entry.accessToken;expectedProfile=$entry.expectedProfile}
        }
        $summary.legacyPreflight.status='passed'
    }else{
    foreach($pair in @(@('client1',1),@('client2',2))) {
        $role=[string]$pair[0];$initialClass=[int]$pair[1]
        $name='d'+[Guid]::NewGuid().ToString('N').Substring(0,24)
        $password=[Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
        $account=Api-Dedicated POST '/v1/accounts' @{name=$name;password=$password;initialClass=$initialClass}
        $session=Api-Dedicated POST '/v1/sessions' @{name=$name;password=$password}
        $accounts[$role]=@{playerId=[string]$account.playerId;initialClass=$initialClass;accessToken=[string]$session.accessToken}
        $password=$null
    }
    }
    $summary.accounts=@($accounts.GetEnumerator()|Sort-Object Name|ForEach-Object{[pscustomobject]@{role=$_.Name;playerId=$_.Value.playerId;initialClass=$_.Value.initialClass}})
    $stage=$baselinePhase;Write-Host "Dedicated validation $runId ${baselinePhase}: one server and two remote clients."
    $server=Start-DedicatedProcess server $baselinePhase;Wait-DedicatedServer $server $baselinePhase
    $clients=New-DedicatedClients $baselinePhase
    $summary.phases[$baselinePhase]=Wait-DedicatedPhase $baselinePhase $server $clients
    $summary.disconnects[$baselinePhase]=Quit-DedicatedClients $baselinePhase $server $clients
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
    foreach($name in @('projectRoot','databasePort','schema','serverId','containerName','volume')) { Assert-Dedicated ([string]$beforeBackend.$name -ceq [string]$backendPreflightState.$name) 'Backend identity changed after preflight; restart was refused.' }
    $beforeCompose=$beforeBackend.PSObject.Properties['composeProject'];$checkedCompose=$backendPreflightState.PSObject.Properties['composeProject']
    Assert-Dedicated (($null -eq $beforeCompose -and $null -eq $checkedCompose) -or ($null -ne $beforeCompose -and $null -ne $checkedCompose -and [string]$beforeCompose.Value -ceq [string]$checkedCompose.Value)) 'Backend compose ownership changed after preflight; restart was refused.'
    $restartArgs=@{ContainerName=[string]$beforeBackend.containerName;VolumeName=[string]$beforeBackend.volume}
    if ($null -ne $beforeBackend.PSObject.Properties['composeProject']) { $restartArgs.ComposeProject=[string]$beforeBackend.composeProject }
    $restart=& (Join-Path $PSScriptRoot 'Start-Backend.ps1') -BackendRoot $backendLink.root -Restart -DatabasePort ([int]$beforeBackend.databasePort) -ApiPort $uri.Port -Schema ([string]$beforeBackend.schema) -ServerId ([string]$beforeBackend.serverId) @restartArgs
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
