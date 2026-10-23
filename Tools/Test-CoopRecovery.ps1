param(
 [string]$EngineRoot,[string]$PackagedExecutable,
 [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$ContinueRunId,
 [ValidateSet('revive','timeout','forest-wipe','root-boss-wipe','moon-seal-wipe','solo','disconnect')]
 [string[]]$Scenario=@('revive','timeout','forest-wipe','root-boss-wipe','moon-seal-wipe','solo','disconnect'),
 [ValidateRange(1024,65535)][int]$Port=7798,
 [ValidateRange(180,1200)][int]$TimeoutSeconds=720,
 [ValidateRange(900,1920)][int]$Width=900,[ValidateRange(600,1080)][int]$Height=600
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackagedExecutable){$exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path;$projectArg='';$mode='packaged-development'}
else{$engine=Resolve-FableEngine -EngineRoot $EngineRoot;Assert-FableReady;$exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe';$projectArg='"'+$script:FableProjectFile+'" ';$mode='editor-game'}
$executableHashBefore=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "Port $Port is already in use."}
function Get-TestSaveDirectory([string]$Executable){
 if($Executable -match '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$'){
  $root=Split-Path (Split-Path (Split-Path $Executable -Parent) -Parent) -Parent
  return Join-Path $root 'Saved\SaveGames'
 }
 return Join-Path $script:FableProjectRoot 'Saved\SaveGames'
}
$targetSaves=Get-TestSaveDirectory $exe
[IO.Directory]::CreateDirectory($targetSaves)|Out-Null
$originals=[ordered]@{}
Get-ChildItem -LiteralPath $targetSaves -Filter '*.sav' -File|Where-Object {$_.Name -notmatch '_Test_|_FrontTest_|ProfileIndex_Test_'}|ForEach-Object {$originals[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
$fixtures=@{}
if(@($Scenario|Where-Object {$_ -in @('root-boss-wipe','moon-seal-wipe')}).Count){
 $completion=Get-Content -LiteralPath (Join-Path $script:FableProjectRoot 'Saved\AdventureValidation\completion.json') -Raw|ConvertFrom-Json
 $sourceSummary=[string]$completion.validation.mageArcher
 $sourceReport=Get-Content -LiteralPath $sourceSummary -Raw|ConvertFrom-Json
 if($sourceReport.status -ne 'passed'){throw 'Dungeon fixtures require a previously completed real adventure.'}
 $originals[$sourceSummary]=(Get-FileHash -LiteralPath $sourceSummary -Algorithm SHA256).Hash
 $sourceSaves=Get-TestSaveDirectory ([string]$sourceReport.executable)
 foreach($role in @('Host','Guest')){
  $source=Join-Path $sourceSaves ('Fablewood_RPG_Test_'+$sourceReport.runId+'_'+$role+'.sav')
  $originals[$source]=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $fixtures[$role]=$source
 }
}
$batch=if($ContinueRunId){$ContinueRunId}else{(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)}
$directory=Join-Path $script:FableProjectRoot ('Saved\CoopRecoveryValidation\'+$batch)
[IO.Directory]::CreateDirectory($directory)|Out-Null
$started=[DateTime]::UtcNow
$caseResults=[ordered]@{}
$failure=$null
$continuation=$null
$attemptDirectory=$null
$carriedFiles=[ordered]@{}
if($ContinueRunId){
 $previousPath=Join-Path $directory 'summary.json'
 $previous=Get-Content -LiteralPath $previousPath -Raw|ConvertFrom-Json
 if($previous.runId -ne $batch -or $previous.status -ne 'failed' -or $previous.executable -ne $exe -or $previous.mode -ne $mode -or -not $previous.executablePreserved -or $previous.executableSHA256Before -cne $executableHashBefore -or $previous.executableSHA256After -cne $executableHashBefore -or -not $previous.originalFilesPreserved){throw 'Only an interrupted suite of this exact preserved executable can continue.'}
 foreach($entry in $previous.originalFiles){
  $current=(Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash
  if(-not $entry.preserved -or $entry.beforeSHA256 -cne $entry.afterSHA256 -or $entry.beforeSHA256 -cne $current){throw 'An original file changed since the interrupted suite.'}
  $originals[$entry.path]=$current
 }
 $attemptDirectory=Join-Path $directory ('attempts/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
 [IO.Directory]::CreateDirectory($attemptDirectory)|Out-Null
 $archivedSummary=Join-Path $attemptDirectory 'summary.json'
 [IO.File]::Copy($previousPath,$archivedSummary,$false)
 $previousHash=(Get-FileHash -LiteralPath $previousPath -Algorithm SHA256).Hash
 if((Get-FileHash -LiteralPath $archivedSummary -Algorithm SHA256).Hash -cne $previousHash){throw 'Interrupted summary preservation failed.'}
 foreach($entry in $previous.cases.PSObject.Properties){
  if($entry.Value.status -ne 'passed'){continue}
  if($entry.Name -notin @('revive','timeout','forest-wipe','root-boss-wipe','moon-seal-wipe','solo','disconnect')){throw 'Unknown carried case.'}
  foreach($phaseName in @('fresh','resume')){
   foreach($roleEntry in $entry.Value.phases.$phaseName.roles.PSObject.Properties){
    $rawPath=Join-Path $directory ($entry.Name+'/'+$phaseName+'/'+$roleEntry.Name+'.json')
    $raw=Get-Content -LiteralPath $rawPath -Raw|ConvertFrom-Json
    if(($raw|ConvertTo-Json -Depth 30 -Compress) -cne ($roleEntry.Value|ConvertTo-Json -Depth 30 -Compress)){throw 'Carried role result differs from its original raw report.'}
    foreach($file in @($rawPath)+@($roleEntry.Value.screenshots)){$carriedFiles[$file]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
   }
  }
  $caseResults[$entry.Name]=$entry.Value
 }
 $started=[DateTime]::Parse($previous.startedUtc).ToUniversalTime()
 $continuation=[ordered]@{resumedUtc=[DateTime]::UtcNow.ToString('o');previousSummary=$archivedSummary;previousSummarySHA256=$previousHash;previousReason=$previous.reason;carriedPassedCases=@($caseResults.Keys);sameExecutableSHA256=$executableHashBefore}
}
function Invoke-RecoveryPhase([string]$Case,[string]$RunId,[string]$Phase,[string]$PhaseDirectory){
 [IO.Directory]::CreateDirectory($PhaseDirectory)|Out-Null
 $hostProcess=$null;$guestProcess=$null;$disconnected=$false;$reports=@{};$phaseValidated=$false
 $roles=@(if($Case -eq 'solo'){'host'}else{'host';'guest'})
 try{
  $common=' -game -windowed -ResX='+$Width+' -ResY='+$Height+' -nosplash -unattended -FableRPGValidationRun='+$RunId+' -FableCoopScenario='+$Case+' -FableCoopPhase='+$Phase+' -FableCoopValidationDirectory="'+$PhaseDirectory+'" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
  $hostArguments=$projectArg+'/Game/Maps/ForestClearing?listen -port='+$Port+' -MULTIHOME=127.0.0.1 -WinX=20 -WinY=40 -FableRPGValidationRole=host -FableCoopValidationRole=host -abslog="'+(Join-Path $PhaseDirectory 'host.log')+'"'+$common
  $hostProcess=Start-Process -FilePath $exe -ArgumentList $hostArguments -WindowStyle Hidden -PassThru
  $ready=$false;$deadline=[DateTime]::UtcNow.AddSeconds(240)
  while([DateTime]::UtcNow -lt $deadline){
   $hostProcess.Refresh();if($hostProcess.HasExited){throw 'Host exited before its loopback endpoint was ready.'}
   if(@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)|Where-Object OwningProcess -eq $hostProcess.Id){$ready=$true;break}
   Start-Sleep -Milliseconds 500
  }
  if(-not $ready){throw 'Recovery host did not start listening.'}
  if($Case -ne 'solo'){
   $guestArguments=$projectArg+'127.0.0.1:'+$Port+' -WinX=700 -WinY=80 -FableRPGValidationRole=guest -FableCoopValidationRole=guest -abslog="'+(Join-Path $PhaseDirectory 'guest.log')+'"'+$common
   $guestProcess=Start-Process -FilePath $exe -ArgumentList $guestArguments -WindowStyle Hidden -PassThru
  }
  Write-Host "Recovery $Case $Phase started: $PhaseDirectory"
  $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while([DateTime]::UtcNow -lt $deadline){
   foreach($role in $roles){
    if($disconnected -and $role -eq 'guest'){continue}
    $path=Join-Path $PhaseDirectory ($role+'.json')
    if(-not(Test-Path -LiteralPath $path)){continue}
    try{$r=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json}catch{continue}
    $sample=$r|Select-Object @{Name='utc';Expression={[DateTime]::UtcNow.ToString('o')}},role,step,substep,position,velocity,partner_position,partner_distance,health,life_state,downed_remaining,revive_progress
    [IO.File]::AppendAllText((Join-Path $PhaseDirectory 'movement-trace.jsonl'),(($sample|ConvertTo-Json -Compress)+[Environment]::NewLine))
    if($r.status -eq 'failed'){throw ($Case+'/'+$Phase+'/'+$role+': '+$r.reason)}
    if($r.status -eq 'passed'){$reports[$role]=$r}
    elseif($Case -eq 'disconnect' -and $Phase -eq 'fresh' -and $role -eq 'guest' -and $r.status -eq 'disconnect_ready'){
     if($r.run_id -ne $RunId -or $r.role -ne 'guest' -or $r.scenario -ne $Case -or $r.phase -ne $Phase -or $r.network_mode -ne 'client' -or $r.process_id -ne $guestProcess.Id -or $r.revive_progress -le .2 -or -not $r.has_revive_target -or -not $r.checks.ready_for_disconnect){throw 'Guest disconnect was not observed during an active authoritative revival.'}
     $reports[$role]=$r
     # Permit the owned guest to use the real menu action, which closes its
     # NetConnection normally instead of waiting for an abrupt UDP timeout.
     [IO.File]::WriteAllText((Join-Path $PhaseDirectory 'guest-disconnect-approved.txt'),$RunId)
     $disconnected=$true
     Write-Host 'Owned guest menu-disconnect authorized during revival; host cleanup remains under observation.'
    }
   }
   if($reports.Count -eq $roles.Count){break}
   $hostProcess.Refresh();if($hostProcess.HasExited -and -not $reports.ContainsKey('host')){throw 'Host exited before its recovery report was complete.'}
   if($guestProcess -and -not $disconnected){$guestProcess.Refresh();if($guestProcess.HasExited -and -not $reports.ContainsKey('guest')){throw 'Guest exited unexpectedly.'}}
   Start-Sleep -Milliseconds 100
  }
  if($reports.Count -ne $roles.Count){throw "Recovery $Case $Phase timed out."}
  if($Case -eq 'disconnect' -and $Phase -eq 'fresh' -and (-not $disconnected -or $reports['guest'].status -ne 'disconnect_ready' -or -not $reports['guest'].checks.ready_for_disconnect)){throw 'Required intentional disconnect was not performed.'}
  foreach($role in $roles){
   $r=$reports[$role];$process=if($role -eq 'host'){$hostProcess}else{$guestProcess}
   $expectedMode=if($role -eq 'host'){'listen_server'}else{'client'}
   if($r.run_id -ne $RunId -or $r.role -ne $role -or $r.scenario -ne $Case -or $r.phase -ne $Phase -or $r.process_id -ne $process.Id -or $r.network_mode -ne $expectedMode){throw 'Recovery report identity mismatch.'}
   $expectedPlayers=if($Case -eq 'solo' -or ($Case -eq 'disconnect' -and $Phase -eq 'fresh' -and $role -eq 'host')){1}else{2}
   if($r.player_count -ne $expectedPlayers -or -not $r.save_succeeded){throw 'Recovery party count or save status mismatch.'}
   if($Phase -eq 'fresh' -and -not ($Case -eq 'disconnect' -and $role -eq 'guest')){
    if(-not $r.checks.final_safe_checkpoint -or $r.enemy_count -ne 0 -or $r.enemy_bolt_count -ne 0 -or $r.life_state -ne 0){throw 'Final recovery checkpoint still has combat or inactive heroes.'}
   }
   if($Phase -eq 'resume'){
    if(-not $r.checks.runtime_not_persisted -or $r.life_state -ne 0){throw 'Runtime recovery state was persisted.'}
   }elseif($Case -eq 'revive'){
    $wanted=if($role -eq 'host'){@('downed_seen','downed_actions_blocked','revived_in_place','revive_no_reward','protection_seen')}else{@('downed_seen','range_rejected','damage_cancelled','self_rejected','release_cancelled','movement_cancelled','panel_cancelled','revive_completed','revive_no_reward')}
    foreach($flag in $wanted){if(-not $r.checks.$flag){throw "Missing revival checkpoint: $role/$flag"}}
   }elseif($Case -eq 'timeout'){
    foreach($flag in @('downed_seen','bleedout_seen','individual_rescue')){if(-not $r.checks.$flag){throw "Missing timeout checkpoint $flag"}}
   }elseif($Case -eq 'disconnect'){
    if($role -eq 'host' -and -not $r.checks.disconnect_recovered){throw 'Host did not recover after the reviving guest disconnected.'}
   }else{
    foreach($flag in @('downed_seen','wipe_countdown_seen','wipe_preserved_profile','runtime_reset','retry_fresh')){if(-not $r.checks.$flag){throw "Missing wipe/retry checkpoint: $role/$flag"}}
    if($Case -eq 'root-boss-wipe' -and (-not $r.checks.boss_damaged_before_wipe -or -not $r.checks.boss_reset_full)){throw 'Boss restart did not restore full health.'}
    if($Case -eq 'moon-seal-wipe' -and -not $r.checks.seal_completed_before_wipe){throw 'Moonwell retry did not test resetting an already-cleansed seal.'}
   }
   if($r.screenshots.Count -lt 1){throw 'Recovery report has no actual screenshots.'}
   foreach($shot in $r.screenshots){
    $full=[IO.Path]::GetFullPath($shot)
    if(-not $full.StartsWith([IO.Path]::GetFullPath($PhaseDirectory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or -not(Test-Path -LiteralPath $full -PathType Leaf) -or (Get-Item -LiteralPath $full).Length -le 0){throw "Missing or out-of-scope actual screenshot: $full"}
   }
  }
  $phaseValidated=$true
  return [ordered]@{roles=$reports;guestIntentionallyDisconnected=$disconnected}
 }finally{
  if($phaseValidated){
   # The final reports confirm synchronous profile saves. Stop the owned server first,
   # then its owned client, before waiting, so live AI cannot wear equipment after
   # the checked checkpoint during a 10-second window-close grace period.
   foreach($process in @($hostProcess,$guestProcess)){
    if($null -eq $process){continue};$process.Refresh()
    if(-not $process.HasExited){$process.Kill()}
   }
   foreach($process in @($hostProcess,$guestProcess)){
    if($null -ne $process){[void]$process.WaitForExit(5000)}
   }
  }else{
   foreach($process in @($guestProcess,$hostProcess)){
    if($null -eq $process){continue};$process.Refresh()
    if(-not $process.HasExited){[void]$process.CloseMainWindow();if(-not $process.WaitForExit(10000)){$process.Kill();[void]$process.WaitForExit(5000)}}
   }
  }
 }
}
try{
 foreach($case in $Scenario){
  if($caseResults.Contains($case)){Write-Host "Recovery $case preserved fresh/resume pass for this exact executable.";continue}
  $runId=$batch+'-'+$case.Replace('-','')
  if($ContinueRunId){$runId+='-r'+[Guid]::NewGuid().ToString('N').Substring(0,8)}
  $caseDirectory=Join-Path $directory $case
  if($ContinueRunId -and (Test-Path -LiteralPath $caseDirectory)){
   $from=[IO.Path]::GetFullPath($caseDirectory);$to=[IO.Path]::GetFullPath((Join-Path $attemptDirectory $case));$root=[IO.Path]::GetFullPath($directory).TrimEnd('\')+'\'
   if(-not $from.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or -not $to.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw 'Partial-case archive escaped its owned run.'}
   Move-Item -LiteralPath $from -Destination $to
  }
  $result=[ordered]@{status='running';runId=$runId;fixtureCopies=@();phases=[ordered]@{}}
  $caseResults[$case]=$result
  if($case -in @('root-boss-wipe','moon-seal-wipe')){
   foreach($role in @('Host','Guest')){
    $source=$fixtures[$role];$target=Join-Path $targetSaves ('Fablewood_RPG_Test_'+$runId+'_'+$role+'.sav')
    if(Test-Path -LiteralPath $target){throw 'Unique fixture target already exists and was preserved.'}
    [IO.File]::Copy($source,$target,$false)
    $hash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if($hash -cne $originals[$source]){throw 'Fixture copy hash mismatch.'}
    $result.fixtureCopies += [ordered]@{source=$source;target=$target;sourceSHA256=$hash}
   }
  }
  foreach($phase in @('fresh','resume')){
   $result.phases[$phase]=Invoke-RecoveryPhase -Case $case -RunId $runId -Phase $phase -PhaseDirectory (Join-Path $caseDirectory $phase)
   if($phase -eq 'resume'){
    foreach($role in $result.phases.resume.roles.Keys){
     if($result.phases.fresh.roles[$role].profile_signature -cne $result.phases.resume.roles[$role].profile_signature){throw "Persistent progress changed during $case process restart: $role"}
    }
   }
   Write-Host "Recovery $case $phase passed."
  }
  $result.status='passed'
 }
}catch{$failure=$_.Exception.Message}
$preservation=@();$allPreserved=$true
foreach($path in $originals.Keys){
 $after=$null;try{$after=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}catch{}
 $same=$after -ceq $originals[$path];$allPreserved=$allPreserved -and $same
 $preservation += [ordered]@{path=$path;beforeSHA256=$originals[$path];afterSHA256=$after;preserved=$same}
}
if(-not $allPreserved){$failure=([string]$failure+' One or more original profile/fixture files changed.').Trim()}
if($continuation){
 $carriedChecks=@()
 foreach($path in $carriedFiles.Keys){
  $after=$null;try{$after=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}catch{}
  $same=$after -ceq $carriedFiles[$path]
  if(-not $same){$failure=([string]$failure+' A carried raw report or screenshot changed.').Trim()}
  $carriedChecks += [ordered]@{path=$path;beforeSHA256=$carriedFiles[$path];afterSHA256=$after;preserved=$same}
 }
 $continuation.carriedFiles=$carriedChecks
}

$executableHashAfter=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$binaryPreserved=$executableHashBefore -ceq $executableHashAfter
if(-not $binaryPreserved){$failure=([string]$failure+' Game executable changed during validation.').Trim()}
$status=if($failure){'failed'}else{'passed'}
[ordered]@{status=$status;runId=$batch;reason=$failure;continuation=$continuation;mode=$mode;executable=$exe;executableSHA256Before=$executableHashBefore;executableSHA256After=$executableHashAfter;executablePreserved=$binaryPreserved;startedUtc=$started.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o');width=$Width;height=$Height;networkScope='Same PC, separate game processes via IPv4 loopback; solo case uses one listen-server process';damageScope='Real enemy attacks only; no HP/damage/reward/quest injection';cleanupScope='After a six-second enemy/projectile-free checkpoint and validated synchronous-save reports, stop only the owned test server/client immediately to preserve the final combat checkpoint; this is harness cleanup, not a forced-termination recovery scenario';originalFilesPreserved=$allPreserved;originalFiles=$preservation;cases=$caseResults}|ConvertTo-Json -Depth 24|Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "Co-op recovery report: $directory\summary.json"
if($failure){throw $failure}
Write-Host 'Co-op recovery, retry and persistent reload checks passed for the requested scenarios.'