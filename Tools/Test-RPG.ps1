param(
 [string]$EngineRoot,[string]$PackagedExecutable,
 [ValidateRange(1024,65535)][int]$Port=7793,
 [ValidateRange(180,1200)][int]$TimeoutSeconds=650,
 [ValidateRange(900,1920)][int]$Width=1280,[ValidateRange(600,1080)][int]$Height=800
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackagedExecutable) { $exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path; $projectArg=''; $mode='packaged-development' }
else { $engine=Resolve-FableEngine -EngineRoot $EngineRoot; Assert-FableReady; $exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'; $projectArg='"'+$script:FableProjectFile+'" '; $mode='editor-game' }
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is already in use." }
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$directory=Join-Path $script:FableProjectRoot ('Saved\RPGValidation\'+$runId)
[IO.Directory]::CreateDirectory($directory) | Out-Null
$phaseResults=@{}
$failure=$null
$started=[DateTime]::UtcNow
try {
 foreach($phase in @('fresh','resume')) {
  $phaseDirectory=Join-Path $directory $phase
  [IO.Directory]::CreateDirectory($phaseDirectory) | Out-Null
  $hostProcess=$null; $guestProcess=$null
  $reports=@{}
  try {
   $common=' -game -windowed -ResX='+$Width+' -ResY='+$Height+' -nosplash -unattended -NoSound -FableRPGValidationRun='+$runId+' -FableRPGValidationPhase='+$phase+' -FableRPGValidationDirectory="'+$phaseDirectory+'" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
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
   Write-Host "RPG $phase phase started: $phaseDirectory"
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
   if($reports.Count -ne 2) { throw 'RPG validation timed out.' }
   foreach($role in @('host','guest')) {
    $r=$reports[$role]; $process=if($role -eq 'host'){$hostProcess}else{$guestProcess}
    $expectedMode=if($role -eq 'host'){'listen_server'}else{'client'}
    if($r.status -ne 'passed' -or $r.run_id -ne $runId -or $r.process_id -ne $process.Id -or $r.network_mode -ne $expectedMode -or $r.role -ne $role) { throw 'Report identity/role/status mismatch.' }
    if($r.player_count -ne 2 -or -not $r.save_succeeded -or $r.forest_quest -ne 3 -or $r.dungeon_quest -ne 3) { throw 'Quest/save checkpoints incomplete.' }
    if($r.health_potions -ne 4 -or $r.mana_potions -ne 3){throw 'Non-default potion stock did not survive gameplay/save reload.'}
    if($phase -eq 'fresh') {
     if(-not $r.saw_wave_two -or -not $r.saw_boss -or -not $r.saw_enrage -or -not $r.remote_upgrade_rejected -or -not $r.invalid_item_rejected -or -not $r.warrior_selection_checked -or -not $r.wrong_class_rejected -or -not $r.insufficient_gold_rejected -or -not $r.armor_equipped) { throw 'RPG checkpoints missing.' }
     foreach($check in @('npc_dialogue_checked','health_potion_checked','mana_potion_checked','potion_cooldown_checked','full_potion_rejected','shop_bought','shop_sold','remote_shop_rejected','protected_sale_rejected','poor_shop_rejected')) { if(-not $r.$check) { throw "Village checkpoint missing: $check" } }
     if($r.local_movement_cm -lt 1500 -or $r.remote_movement_cm -lt 1000 -or $r.attack_inputs -lt 5 -or $r.skill_inputs -lt 1) { throw 'Insufficient real gameplay input or replicated movement.' }
     if($r.screenshots.Count -lt 6) { throw 'Missing gameplay captures.' }
    } else {
     if($r.profile_signature -ne $phaseResults['fresh'][$role].profile_signature) { throw 'Persistent profile differs after process restart.' }
    }
    foreach($shot in $r.screenshots) {
     $full=[IO.Path]::GetFullPath($shot)
     if(-not $full.StartsWith([IO.Path]::GetFullPath($phaseDirectory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Screenshot outside run directory.' }
     if(-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Missing screenshot $full" }
    }
   }
   $phaseResults[$phase]=$reports
   Write-Host "RPG $phase phase passed."
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
 executable=$exe; networkScope='Two separate game processes on one PC; IPv4 loopback listen server'
 saveScope='Unique test save slots on the host PC; real player profiles remain separate'
 phases=$phaseResults
} | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "RPG report: $directory\summary.json"
if($failure) { throw $failure }
Write-Host 'RPG progression and persistent reload passed. Review captures separately.'
