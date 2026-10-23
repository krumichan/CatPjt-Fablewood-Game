param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$SourceSummary,
    [ValidateSet('Mage','Warrior')][string]$HostClass='Mage',
    [ValidateRange(1024,65535)][int]$Port=7801,
    [ValidateRange(180,3600)][int]$TimeoutSeconds=1500,
    [ValidateRange(900,1920)][int]$Width=900,
    [ValidateRange(600,1080)][int]$Height=600
)
. (Join-Path $PSScriptRoot 'Common.ps1')
function Read-TalentText([string]$Path) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Read-TalentJson([string]$Path) { return (Read-TalentText $Path | ConvertFrom-Json) }
function Get-TalentHash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Get-TalentSaveDirectory([string]$Executable) {
    if ($Executable -match '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
        return Join-Path (Split-Path (Split-Path (Split-Path $Executable -Parent) -Parent) -Parent) 'Saved\SaveGames'
    }
    return Join-Path $script:FableProjectRoot 'Saved\SaveGames'
}
function Assert-TalentTrue($Value,[string]$Description) {
    if ($Value -isnot [bool] -or -not $Value) { throw $Description }
}
function Assert-TalentScreenshots($Report,[string]$Folder,[int]$Minimum) {
    $shots=@($Report.screenshots)
    if ($shots.Count -lt $Minimum -or @($shots | Select-Object -Unique).Count -lt $Minimum) { throw 'Too few distinct talent screenshots.' }
    $prefix=[IO.Path]::GetFullPath($Folder).TrimEnd('\')+'\'
    foreach ($shot in $shots) {
        $full=[IO.Path]::GetFullPath([string]$shot)
        if (-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetExtension($full) -ine '.png' -or -not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Missing or out-of-phase screenshot: $full" }
        $stream=[IO.File]::OpenRead($full)
        try { $header=New-Object byte[] 8;[void]$stream.Read($header,0,8)
            if (($header -join ',') -cne '137,80,78,71,13,10,26,10') { throw "Invalid PNG: $full" }
        } finally { $stream.Dispose() }
    }
}
function Assert-TalentReport($Report,[string]$Role,[string]$Phase,[int]$ProcessId,[string]$Folder) {
    $expectedMode=if($Role -eq 'host'){'listen_server'}else{'client'}
    $expectedClass=if($Role -eq 'guest'){2}elseif($HostClass -eq 'Warrior'){0}else{1}
    if ($Report.status -cne 'passed' -or $Report.run_id -cne $runId -or $Report.role -cne $Role -or
        $Report.phase -cne $Phase -or $Report.process_id -ne $ProcessId -or $Report.network_mode -cne $expectedMode -or
        $Report.player_count -ne 2 -or $Report.class -ne $expectedClass) { throw "Talent identity/PID/network/class mismatch: $Role/$Phase" }
    Assert-TalentTrue $Report.talents_enabled 'Native talent flag was not enabled.'
    Assert-TalentTrue $Report.save_succeeded 'Authoritative profile save did not succeed.'
    Assert-TalentTrue $Report.report_snapshot_frozen 'Terminal report was not a frozen snapshot.'
    $expectedSchema=if($Role -eq 'host'){8}else{0}
    if ($Report.saved_schema_version -ne $expectedSchema) { throw 'Talent saved schema observation is wrong.' }
    if ($Role -eq 'host' -and $Report.profile_slot -cne ('Fablewood_RPG_Test_'+$runId+'_Host')) { throw 'Host saved an unexpected slot.' }
    if ($Role -eq 'guest' -and -not [string]::IsNullOrEmpty([string]$Report.profile_slot)) { throw 'Guest unexpectedly owns an authoritative save slot.' }
    if ($Report.advancement_tier -ne 3 -or $Report.forest_quest -ne 3 -or $Report.dungeon_quest -ne 3 -or $Report.moonwell_quest -ne 3 -or
        $Report.active_region -ne 0 -or $Report.life_state -ne 0 -or $Report.live_enemy_count -ne 0 -or $Report.projectile_count -ne 0) { throw 'Final talent checkpoint is not safe, completed and in the village.' }
    if ([string]::IsNullOrWhiteSpace([string]$Report.talent_signature) -or $Report.talent_signature -notmatch '\|v8:talents=') { throw 'Full v8 profile signature is missing.' }
    $progress=@($Report.class_progress)
    if ($progress.Count -ne 3 -or $progress[$expectedClass].second_talent -ne 2 -or $progress[$expectedClass].third_talent -ne 2) { throw 'The final current-class build is not Second/Second.' }
    $old=$sourceReport.phases.resume.$Role
    foreach ($index in @(0,1,2)) {
        if ($index -eq $expectedClass) { continue }
        if ($progress[$index].second_talent -ne 0 -or $progress[$index].third_talent -ne 0 -or
            $progress[$index].tier -ne $old.class_progress[$index].tier -or
            (ConvertTo-Json -InputObject @($progress[$index].ranks) -Compress) -cne (ConvertTo-Json -InputObject @($old.class_progress[$index].ranks) -Compress)) {
            throw 'An unrelated class build was changed.'
        }
    }
    if ($Phase -eq 'fresh') {
        foreach ($check in @('legacy_migrated','first_selected','duplicate_selection_preserved','first_effect',
            'reset_confirmation','cancel_preserved','reset_cost_refund','other_classes_preserved','skill_reinvested',
            'second_selected','second_effect','peer_replicated','peer_final_ready_seen','final_safe_checkpoint')) {
            Assert-TalentTrue $Report.talent_checks.$check ("Missing talent checkpoint: $Role/$check")
        }
        if ($Report.talent_cycle -ne 1 -or $Report.talent_probe_done_cycle -ne 1 -or $Report.talent_peer_done_cycle_seen -ne 1 -or $Report.quiet_seconds -lt 6 -or
            $Report.initial_signature -cne $old.adventure_signature -or
            $Report.initial_root_completions -ne $old.dungeon_completions -or
            $Report.dungeon_completions -ne ([int]$old.dungeon_completions+2) -or
            $Report.moonwell_completions -ne $old.moonwell_completions) { throw 'Legacy migration, two real expeditions or safe final checkpoint differed.' }
        $events=@($Report.talent_events)
        $first=@($events | Where-Object {$_.action -eq 'selected' -and $_.cycle -eq 0})
        $second=@($events | Where-Object {$_.action -eq 'selected' -and $_.cycle -eq 1})
        $effects=@($events | Where-Object action -eq 'combat_effect')
        $resets=@($events | Where-Object action -eq 'reset')
        if ($first.Count -ne 1 -or $second.Count -ne 1 -or $resets.Count -ne 1 -or $effects.Count -ne 2 -or
            $first[0].first_rank -ne 2 -or $first[0].second_rank -ne 0 -or $second[0].first_rank -ne 0 -or $second[0].second_rank -ne 2) {
            throw 'Actual UI selections or both combat observations are incomplete.'
        }
        foreach ($selection in @($first[0],$second[0])) {
            if ($selection.gold -ne $selection.before_gold -or $selection.points -ne $selection.before_points) { throw 'Free talent selection spent resources.' }
        }
        $refund=0;$ranks=@($old.class_progress[$expectedClass].ranks)
        for ($i=0;$i -lt 4;$i++) { $refund += [int]$ranks[$i] - $(if($i -eq 3){0}else{1}) }
        $reset=$resets[0]
        if ($reset.gold -ne ([int]$reset.before_gold-50) -or $reset.points -ne ([int]$reset.before_points+$refund) -or $reset.first_rank -ne 0 -or $reset.second_rank -ne 0) { throw 'Reset did not charge 50 gold and refund only the current class.' }
        $effectA=@($effects | Where-Object cycle -eq 0)[0];$effectB=@($effects | Where-Object cycle -eq 1)[0]
        Assert-TalentTrue $effectA.clean_damage_window 'First-path damage was not measured in an isolated stable window.'
        if ($effectA.enemy_health_after -ge $effectA.enemy_health_before -or $effectA.observed_health_delta -le 0 -or
            [Math]::Abs($effectA.observed_health_delta-($effectA.enemy_health_before-$effectA.enemy_health_after)) -gt 0.01) { throw 'First-path effect lacks a consistent isolated enemy health delta.' }
        if ($expectedClass -eq 1) {
            Assert-TalentTrue $Report.talent_checks.frost_actual_slow 'Mage frost slow was not actually applied.'
            if ($effectA.burn_damage -le 0 -or $effectB.shield -le 0 -or $effectB.shield_capacity -ne 90 -or $effectB.shield_duration -ne 8) { throw 'Mage burn or enhanced frost shield was not observed.' }
        } elseif ($expectedClass -eq 0) {
            if ($effectB.ally_shields -le 0 -or $effectB.shield -le 0 -or $effectB.shield_capacity -ne 85 -or $effectB.shield_duration -ne 8) { throw 'Warrior guardian self/ally shield was not observed.' }
        } else {
            Assert-TalentTrue $Report.talent_checks.ranger_actual_dodge 'Archer actual reduced-cooldown dodge was not observed.'
            if ($effectB.maximum_observed_speed -le 435 -or [Math]::Abs($effectB.movement_limit-462) -gt 0.1 -or [Math]::Abs($effectB.dodge_duration-1.05) -gt 0.01) { throw 'Archer actual mobility effect was not observed.' }
        }
        Assert-TalentScreenshots $Report $Folder 7
    } else {
        Assert-TalentTrue $Report.talent_checks.runtime_not_persisted 'Combat runtime survived a new process.'
        $fresh=$phaseResults.fresh[$Role]
        if ($Report.talent_signature -cne $fresh.talent_signature -or $Report.talent_choices -cne $fresh.talent_choices -or
            (ConvertTo-Json -InputObject @($Report.inventory_signatures) -Depth 10 -Compress) -cne (ConvertTo-Json -InputObject @($fresh.inventory_signatures) -Depth 10 -Compress) -or
            (ConvertTo-Json -InputObject @($Report.stash_signatures) -Depth 10 -Compress) -cne (ConvertTo-Json -InputObject @($fresh.stash_signatures) -Depth 10 -Compress) -or
            (ConvertTo-Json -InputObject @($Report.class_progress) -Depth 15 -Compress) -cne (ConvertTo-Json -InputObject @($fresh.class_progress) -Depth 15 -Compress)) { throw 'Full class/profile/gear/stash signature changed after restart.' }
        Assert-TalentScreenshots $Report $Folder 1
    }
}
function Invoke-TalentPhase([string]$Phase,[string]$Folder) {
    [IO.Directory]::CreateDirectory($Folder) | Out-Null
    $hostProcess=$null;$guestProcess=$null;$reports=@{};$roles=@('host','guest')
    try {
        if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is already in use." }
        $common=' -game -FableTalents -windowed -ResX='+$Width+' -ResY='+$Height+' -nosplash -unattended'+
            ' -FableRPGValidationRun='+$runId+' -FableRPGValidationPhase='+$Phase+' -FableRPGValidationDirectory="'+$Folder+'"'+
            ' -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
        $hostLog=Join-Path $Folder 'host.log';$guestLog=Join-Path $Folder 'guest.log'
        $hostArgs=$projectArg+'/Game/Maps/ForestClearing?listen -port='+$Port+' -MULTIHOME=127.0.0.1 -WinX=20 -WinY=40'+
            ' -FableRPGValidationRole=host -abslog="'+$hostLog+'"'+$common
        $hostProcess=Start-Process -FilePath $exe -ArgumentList $hostArgs -WindowStyle Hidden -PassThru
        $ready=$false;$deadline=[DateTime]::UtcNow.AddSeconds(240)
        while ([DateTime]::UtcNow -lt $deadline) {
            $hostProcess.Refresh();if ($hostProcess.HasExited) { throw "Host exited before listening: $hostLog" }
            if (@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $hostProcess.Id).Count -gt 0) {$ready=$true;break}
            Start-Sleep -Milliseconds 500
        }
        if (-not $ready) { throw 'Talent host did not open its loopback endpoint.' }
        $guestArgs=$projectArg+'127.0.0.1:'+$Port+' -WinX=700 -WinY=80 -FableRPGValidationRole=guest -abslog="'+$guestLog+'"'+$common
        $guestProcess=Start-Process -FilePath $exe -ArgumentList $guestArgs -WindowStyle Hidden -PassThru
        Write-Host "Talents $HostClass/Archer $Phase started: $Folder"
        $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            foreach ($role in $roles) {
                $path=Join-Path $Folder ($role+'.json');if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {continue}
                try {
                    $r=Read-TalentJson $path
                    if ($r.status -in @('passed','failed')) {
                        # Native success is logged only after the frozen JSON publication succeeds.
                        $log=Read-TalentText (Join-Path $Folder ($role+'.log'))
                        if ($log -match ('FABLE_RPG_VALIDATION role='+$role+' status='+$r.status+' attempts=\d+')) {$reports[$role]=$r}
                    }
                } catch { continue }
            }
            if (@($reports.Values | Where-Object status -eq 'failed').Count -gt 0) {throw ('Talent native failure: '+(($reports.Values | ForEach-Object reason)-join '; '))}
            if ($reports.Count -eq 2) {break}
            $hostProcess.Refresh();$guestProcess.Refresh()
            if ($hostProcess.HasExited -or $guestProcess.HasExited) {throw 'A game process exited before both terminal talent reports.'}
            Start-Sleep -Milliseconds 200
        }
        $phaseResults[$Phase]=$reports
        if ($reports.Count -ne 2) { throw 'Talent validation timed out; running/partial reports were retained.' }
        foreach ($role in $roles) {
            $process=if($role -eq 'host'){$hostProcess}else{$guestProcess}
            Assert-TalentReport $reports[$role] $role $Phase $process.Id $Folder
        }
        foreach ($role in @('Host','Guest')) {
            $save=Join-Path $targetSaves ('Fablewood_RPG_Test_'+$runId+'_'+$role+'.sav')
            if (-not (Test-Path -LiteralPath $save -PathType Leaf) -or (Get-Item -LiteralPath $save).Length -le 0) {throw "Saved isolated profile missing: $save"}
        }
        Write-Host "Talents $Phase passed."
    } finally {
        $phaseResults[$Phase]=$reports
        # Only this invocation's two process handles are eligible for cleanup.
        foreach ($process in @($guestProcess,$hostProcess)) {
            if ($null -eq $process) {continue}
            $process.Refresh();if (-not $process.HasExited) {$process.Kill();[void]$process.WaitForExit(5000)}
        }
    }
}
if ($PackagedExecutable) {
    $exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path
    if ($exe -notmatch '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
        $inner=Join-Path (Split-Path -Parent $exe) 'Fablewood\Binaries\Win64\Fablewood.exe'
        if (-not (Test-Path -LiteralPath $inner -PathType Leaf)) {throw 'Supply the packaged inner game executable or archive bootstrap.'}
        $exe=(Resolve-Path -LiteralPath $inner).Path
    }
    $projectArg='';$mode='packaged-development'
} else {
    $engine=Resolve-FableEngine -EngineRoot $EngineRoot;Assert-FableReady
    $exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe';$projectArg='"'+$script:FableProjectFile+'" ';$mode='editor-game'
}
$hashBefore=Get-TalentHash $exe
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$directory=Join-Path $script:FableProjectRoot ('Saved\TalentValidation\'+$runId)
if (Test-Path -LiteralPath $directory) {throw 'Unique talent evidence directory exists and was preserved.'}
[IO.Directory]::CreateDirectory($directory) | Out-Null
$targetSaves=Get-TalentSaveDirectory $exe;$phaseResults=[ordered]@{};$originals=[ordered]@{};$copies=@()
$sourceReport=$null;$sourceSaves=$null;$snapshotComplete=$false;$failure=$null;$started=[DateTime]::UtcNow
try {
    $completionPath=Join-Path $script:FableProjectRoot 'Saved\StoryValidation\completion.json'
    if (-not $SourceSummary) {
        $completion=Read-TalentJson $completionPath
        if ($completion.status -ne 'complete' -or $completion.stage -ne 'main-story-progression-v7') {throw 'A completed v7 Story baseline is required.'}
        $SourceSummary=if($HostClass -eq 'Warrior'){[string]$completion.validation.storyWarriorArcher}else{[string]$completion.validation.storyMageArcher}
    }
    foreach ($marker in @(Get-ChildItem -LiteralPath (Join-Path $script:FableProjectRoot 'Saved') -Directory | ForEach-Object {Join-Path $_.FullName 'completion.json'} | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf})) {$originals[$marker]=Get-TalentHash $marker}
    $SourceSummary=(Resolve-Path -LiteralPath $SourceSummary).Path;$sourceReport=Read-TalentJson $SourceSummary
    if ($sourceReport.status -ne 'passed' -or $sourceReport.runId -notmatch '^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$') {throw 'Source summary is not a completed identifiable real Story run.'}
    $originals[$SourceSummary]=Get-TalentHash $SourceSummary
    $property=$sourceReport.PSObject.Properties['profileSaveFolder']
    $sourceSaves=if($null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)){[IO.Path]::GetFullPath([string]$property.Value)}else{Get-TalentSaveDirectory ([string]$sourceReport.executable)}
    [IO.Directory]::CreateDirectory($targetSaves) | Out-Null
    foreach ($folder in @($sourceSaves,$targetSaves,(Join-Path $script:FableProjectRoot 'Saved\SaveGames') | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $folder -PathType Container)) {continue}
        foreach ($file in @(Get-ChildItem -LiteralPath $folder -Filter '*.sav' -File | Where-Object {$_.Name -notmatch '_Test_|_FrontTest_|ProfileIndex_Test_'})) {$originals[$file.FullName]=Get-TalentHash $file.FullName}
    }
    $sources=@{}
    foreach ($role in @('host','guest')) {
        $old=$sourceReport.phases.resume.$role;$expected=if($role -eq 'guest'){2}elseif($HostClass -eq 'Warrior'){0}else{1}
        if ($old.status -ne 'passed' -or $old.role -cne $role -or $old.run_id -cne $sourceReport.runId -or $old.class -ne $expected -or
            $old.player_count -ne 2 -or $old.advancement_tier -ne 3 -or $old.dungeon_quest -ne 3 -or $old.moonwell_quest -ne 3 -or
            [string]::IsNullOrWhiteSpace([string]$old.adventure_signature) -or $old.adventure_signature -match '\|v8:talents=') {throw 'Expected original v7 advanced class fixture is missing.'}
        $suffix=if($role -eq 'host'){'Host'}else{'Guest'}
        $source=Join-Path $sourceSaves ('Fablewood_RPG_Test_'+$sourceReport.runId+'_'+$suffix+'.sav')
        $originals[$source]=Get-TalentHash $source;$sources[$role]=$source
    }
    $snapshotComplete=$true
    foreach ($role in @('host','guest')) {
        $suffix=if($role -eq 'host'){'Host'}else{'Guest'};$source=$sources[$role]
        $target=Join-Path $targetSaves ('Fablewood_RPG_Test_'+$runId+'_'+$suffix+'.sav')
        if (Test-Path -LiteralPath $target) {throw 'Unique copied profile slot already exists and was preserved.'}
        [IO.File]::Copy($source,$target,$false);$copyHash=Get-TalentHash $target
        if ($copyHash -cne $originals[$source]) {throw 'Source fixture byte-copy SHA256 mismatch.'}
        $copies += [ordered]@{role=$role;source=$source;target=$target;sourceSHA256=$originals[$source];targetSHA256Before=$copyHash;byteExact=$true}
    }
    foreach ($phase in @('fresh','resume')) {Invoke-TalentPhase $phase (Join-Path $directory $phase)}
} catch {$failure=$_.Exception.Message}
$preservation=@();$allPreserved=$snapshotComplete
foreach ($entry in $originals.GetEnumerator()) {
    $after=$null;try {if(Test-Path -LiteralPath $entry.Key -PathType Leaf){$after=Get-TalentHash $entry.Key}}catch{}
    $same=$null -ne $after -and $after -ceq $entry.Value;$allPreserved=$allPreserved -and $same
    $preservation += [ordered]@{path=$entry.Key;beforeSHA256=$entry.Value;afterSHA256=$after;preserved=$same}
}
if (-not $allPreserved) {$failure=(@($failure,'Source fixture, completion marker or non-test profile preservation failed.') | Where-Object {$_}) -join ' '}
$hashAfter=$null;try {$hashAfter=Get-TalentHash $exe}catch{}
$binaryPreserved=$null -ne $hashAfter -and $hashAfter -ceq $hashBefore
if (-not $binaryPreserved) {$failure=(@($failure,'Executable SHA256 preservation failed.') | Where-Object {$_}) -join ' '}
$status=if($failure){'failed'}else{'passed'}
[ordered]@{
    runId=$runId;status=$status;reason=$failure;mode=$mode;hostClass=$HostClass;width=$Width;height=$Height;port=$Port
    startedUtc=$started.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o');executable=$exe
    executableSHA256Before=$hashBefore;executableSHA256After=$hashAfter;executablePreserved=$binaryPreserved
    sourceSummary=$SourceSummary;sourceRunId=$(if($sourceReport){$sourceReport.runId}else{$null})
    sourceProfileSaveFolder=$sourceSaves;profileSaveFolder=$targetSaves;fixtureCopies=$copies
    originalFilesPreserved=$allPreserved;originalFiles=$preservation;phases=$phaseResults
    networkScope='Two owned processes on IPv4 loopback; no physical two-PC/WAN claim'
    gameplayScope='Real UI selections and reset, two normal Root Ruins expeditions, actual class effects and safe village save; no HP, quest, reward or teleport injection'
    advancementScope='Copied real tier-three v7 profiles; tier-two/three unlock guards and mixed paths are native-test coverage'
    cleanupScope='Only the two process handles started by this phase; exact terminal snapshots and all evidence retained'
} | ConvertTo-Json -Depth 35 | Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "Talent report: $directory\summary.json"
if ($failure) {throw $failure}
Write-Host 'Talent selection, current-class reset, class combat effects and exact v8 restart checks passed.'
