param(
 [string]$EngineRoot,[string]$PackagedExecutable,
 [switch]$Story,
 [ValidateSet("Mage","Warrior")][string]$HostClass="Mage",
 [ValidateRange(1024,65535)][int]$Port=7797,
 [ValidateRange(180,1200)][int]$TimeoutSeconds=1100,
 [ValidateRange(900,1920)][int]$Width=900,[ValidateRange(600,1080)][int]$Height=600
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackagedExecutable) { $exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path; $projectArg=''; $mode='packaged-development' }
else { $engine=Resolve-FableEngine -EngineRoot $EngineRoot; Assert-FableReady; $exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'; $projectArg='"'+$script:FableProjectFile+'" '; $mode='editor-game' }
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is already in use." }
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$validationFolder=if($Story){'Saved\StoryValidation\'}else{'Saved\AdventureValidation\'}
$directory=Join-Path $script:FableProjectRoot ($validationFolder+$runId)
[IO.Directory]::CreateDirectory($directory) | Out-Null
$phaseResults=@{}
$failure=$null
$started=[DateTime]::UtcNow
try {
 $phases=if($Story){@('prologue','fresh','resume')}else{@('fresh','resume')}
 foreach($phase in $phases) {
  $phaseDirectory=Join-Path $directory $phase
  [IO.Directory]::CreateDirectory($phaseDirectory) | Out-Null
  $hostProcess=$null; $guestProcess=$null
  $reports=@{}
  try {
   $common=' -game -FableAdventure -windowed -ResX='+$Width+' -ResY='+$Height+' -nosplash -unattended -FableRPGValidationRun='+$runId+' -FableRPGValidationPhase='+$phase+' -FableRPGValidationDirectory="'+$phaseDirectory+'" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
   if($HostClass -eq 'Warrior'){$common+=' -FableAdventureWarrior'}
   if($Story){$common+=' -FableStory'}
   $hostLog=Join-Path $phaseDirectory 'host.log'
   $guestLog=Join-Path $phaseDirectory 'guest.log'
   $hostArguments=$projectArg+'/Game/Maps/ForestClearing?listen -port='+$Port+' -MULTIHOME=127.0.0.1 -WinX=20 -WinY=40 -FableRPGValidationRole=host -abslog="'+$hostLog+'"'+$common
   $hostProcess=Start-Process -FilePath $exe -ArgumentList $hostArguments -WindowStyle Hidden -PassThru
   $ready=$false; $deadline=[DateTime]::UtcNow.AddSeconds(240)
   while([DateTime]::UtcNow -lt $deadline) {
    $hostProcess.Refresh()
    if($hostProcess.HasExited) { throw "Host exited before listening: $hostLog" }
    if(@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) | Where-Object OwningProcess -eq $hostProcess.Id) { $ready=$true; break }
    Start-Sleep -Milliseconds 500
   }
   if(-not $ready) { throw 'Host did not open its loopback endpoint.' }
   $guestArguments=$projectArg+'127.0.0.1:'+$Port+' -WinX=700 -WinY=80 -FableRPGValidationRole=guest -abslog="'+$guestLog+'"'+$common
   $guestProcess=Start-Process -FilePath $exe -ArgumentList $guestArguments -WindowStyle Hidden -PassThru
   Write-Host "Adventure $phase phase started: $phaseDirectory"
   $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
   while([DateTime]::UtcNow -lt $deadline) {
    foreach($role in @('host','guest')) {
     $path=Join-Path $phaseDirectory ($role+'.json')
     if(Test-Path -LiteralPath $path) {
      try { $report=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { continue }
      if($report.status -in @('passed','failed')) { $reports[$role]=$report }
     }
    }
    if(@($reports.Values | Where-Object status -eq 'failed').Count -gt 0) { throw ('Scenario failure: '+(($reports.Values | ForEach-Object reason)-join '; ')) }
    if($reports.Count -eq 2) { break }
    $hostProcess.Refresh(); $guestProcess.Refresh()
    if($hostProcess.HasExited -or $guestProcess.HasExited) { throw 'Game process exited before both reports were complete.' }
    Start-Sleep -Milliseconds 1000
   }
   if($reports.Count -ne 2) { throw 'Adventure validation timed out.' }
   foreach($role in @('host','guest')) {
    $r=$reports[$role]; $process=if($role -eq 'host'){$hostProcess}else{$guestProcess}
    $expectedMode=if($role -eq 'host'){'listen_server'}else{'client'}
    if($r.status -ne 'passed' -or $r.run_id -ne $runId -or $r.process_id -ne $process.Id -or $r.network_mode -ne $expectedMode -or $r.role -ne $role) { throw 'Report identity/role/status mismatch.' }
    if(-not $r.adventure_enabled -or $r.class_progress.Count -ne 3) { throw 'Adventure mode or progression missing.' }
    $wanted=if($role -eq 'guest'){2}elseif($HostClass -eq 'Warrior'){0}else{1}
    if($Story) {
     if(-not $r.story_enabled -or $r.saved_schema_version -ne $(if($role -eq 'host'){8}else{0}) -or
        -not $r.save_succeeded -or $r.player_count -ne 2 -or $r.class -ne $wanted) {throw 'Story/schema/class/save observations are incomplete.'}
     if($phase -eq 'prologue') {
      if($r.story_chapter -ne 1 -or $r.story_object_mask -ne 1 -or $r.story_seal_mask -ne 0 -or
         -not $r.quest_journal_open -or -not $r.story_checks.partial_checkpoint_saved -or
         -not $r.story_checks.chapter_0_exact_reward -or -not $r.story_checks.object_0_observed -or
         -not $r.story_checks.object_0_duplicate_no_reward -or $r.screenshots.Count -lt 3) {throw 'Partial collection/reward checkpoint is incomplete.'}
     } else {
      if($r.story_chapter -ne 5 -or $r.story_object_mask -ne 31 -or $r.story_seal_mask -ne 7){throw 'Main story objectives are incomplete.'}
      if($phase -eq 'fresh') {
       if($r.story_reload_signature -cne $phaseResults['prologue'][$role].adventure_signature){throw 'The partially collected story profile differs after process restart.'}
       foreach($check in @('partial_checkpoint_restored','chapter_1_exact_reward','chapter_2_exact_reward','chapter_3_exact_reward','chapter_4_exact_reward',
         'object_1_observed','object_2_observed','object_3_observed','object_4_observed','object_1_duplicate_no_reward','object_2_duplicate_no_reward',
         'object_3_duplicate_no_reward','all_three_seals_personally_recorded','completed_report_no_reward','workshop_error_visible','workshop_stale_notice_hidden','workshop_repeated_error_visible','workshop_notice_hidden_after_close')) {
        if(-not $r.story_checks.$check){throw "Missing observed story checkpoint: $check"}
       }
       $rewardXP=@(20,60,60,80,100);$rewardGold=@(20,40,50,70,100);$rewardOre=@(0,3,3,4,5)
       $events=@($phaseResults['prologue'][$role].story_events)+@($r.story_events)
       $rewards=@($events|Where-Object action -eq 'chapter-reported')
       if($rewards.Count -ne 5){throw 'Exactly five distinct story reward events were not observed.'}
       $seen=@{}
       foreach($event in $rewards) {
        $c=[int]$event.reward_chapter
        if($c -lt 0 -or $c -gt 4 -or $seen.ContainsKey($c) -or $event.chapter -ne ($c+1) -or
           $event.total_xp -ne ($event.xp_before+$rewardXP[$c]) -or $event.gold -ne ($event.gold_before+$rewardGold[$c]) -or
           $event.ore -ne ($event.ore_before+$rewardOre[$c])) {throw 'Raw main story reward event delta is incorrect or duplicated.'}
        $seen[$c]=$true
       }
      }
     }
    }
    if($phase -ne 'prologue') {
    if($r.player_count -ne 2 -or -not $r.save_succeeded -or $r.class -ne $wanted -or $r.advancement_tier -ne 3 -or
       $r.forest_quest -ne 3 -or $r.dungeon_quest -ne 3 -or $r.moonwell_quest -ne 3 -or
       $r.dungeon_completions -ne 1 -or $r.moonwell_completions -ne 1) { throw 'Quest/class/save checkpoints incomplete.' }
    $progress=$r.class_progress[$wanted]
    if($progress.second_trial_stage -ne 3 -or $progress.third_trial_stage -ne 3 -or
       $progress.second_trial_casts -ne 3 -or $progress.third_trial_casts -ne 3 -or
       $progress.second_trial_legacy -or $progress.third_trial_legacy) {throw 'Real, non-legacy trial completion missing.'}
    if($phase -eq 'fresh') {
     foreach($check in @('trial_started','premature_trial_rejected','moon_locked_before_root_reward','seal_locked_while_enemies_alive',
       'second_advancement','third_advancement','moon_ripple_observed','moon_rain_observed','moon_ranged_observed','moon_cleared',
       'root_rewards_isolated','trial_class_switch_preserved','trial_duplicate_reward_rejected','quest_journal_open')) {
       if(-not $r.$check){throw "Adventure checkpoint missing: $check"}
     }
     if($r.observed_seal_mask -ne 7 -or $r.local_movement_cm -lt 3000 -or $r.remote_movement_cm -lt 2500 -or $r.skill_inputs -lt 6){throw 'Insufficient observed seals, movement or actual skill inputs.'}
     if($r.screenshots.Count -lt 6){throw 'Missing gameplay captures.'}
    } else {
     if(-not $r.skills_panel_open){throw 'Restored skill panel did not open.'}
     if($r.adventure_signature -cne $phaseResults['fresh'][$role].adventure_signature){throw 'Persistent v4 profile differs after process restart.'}
    }
    }
    foreach($shot in $r.screenshots) {
     $full=[IO.Path]::GetFullPath($shot)
     if(-not $full.StartsWith([IO.Path]::GetFullPath($phaseDirectory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Screenshot outside run directory.' }
     if(-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Missing screenshot $full" }
    }
   }
   $phaseResults[$phase]=$reports
   Write-Host "Adventure $phase phase passed."
  } finally {
   foreach($process in @($guestProcess,$hostProcess)) {
    if($null -eq $process) { continue }
    $process.Refresh()
    if(-not $process.HasExited) {
     [void]$process.CloseMainWindow()
     if(-not $process.WaitForExit(10000)) { $process.Kill(); [void]$process.WaitForExit(5000) }
    }
   }
  }
 }
} catch { $failure=$_.Exception.Message }
$status=if($failure){'failed'}else{'passed'}
[ordered]@{
 runId=$runId; status=$status; reason=$failure; mode=$mode; width=$Width; height=$Height
 startedUtc=$started.ToString('o'); endedUtc=[DateTime]::UtcNow.ToString('o')
 executable=$exe; hostClass=$HostClass;story=[bool]$Story; networkScope='Two separate game processes on one PC; IPv4 loopback listen server'
 saveScope='Unique test save slots on the host PC; real player profiles remain separate'
 phases=$phaseResults
} | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "Adventure report: $directory\summary.json"
if($failure) { throw $failure }
Write-Host 'Adventure quests, real class trials, both dungeons and persistent reload passed. This is not a human playtime measurement.'
