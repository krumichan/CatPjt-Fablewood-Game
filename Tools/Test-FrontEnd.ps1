param([string]$EngineRoot,[string]$PackagedExecutable,[ValidateRange(1024,65535)][int]$Port=7788,[ValidateRange(900,1920)][int]$Width=1280,[ValidateRange(600,1080)][int]$Height=800)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackagedExecutable){$exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path;$projectArg='';$mode='packaged-development'}
else{$engine=Resolve-FableEngine -EngineRoot $EngineRoot;Assert-FableReady;$exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe';$projectArg='"'+$script:FableProjectFile+'" ';$mode='editor-game'}
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "Port $Port is already in use."}
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$directory=Join-Path $script:FableProjectRoot ('Saved\FrontEndValidation\'+$runId)
[IO.Directory]::CreateDirectory($directory)|Out-Null
$saveDirs=@((Join-Path $script:FableProjectRoot 'Saved\SaveGames'))
if($PackagedExecutable){$saveDirs+=Join-Path (Split-Path (Split-Path (Split-Path $exe))) 'Saved\SaveGames'}
function Get-RealProfileHashes {
 $hashes=@{}
 foreach($dir in $saveDirs){
  if(Test-Path -LiteralPath $dir){
   foreach($file in Get-ChildItem -LiteralPath $dir -Filter '*.sav' -File){
    if($file.Name -notmatch 'Test'){$hashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
   }
  }
 }
 return $hashes
}
$before=Get-RealProfileHashes
$processes=@{};$reports=@{};$failure=$null
try{
 foreach($role in @('host','guest')){
  $log=Join-Path $directory ($role+'.log')
  $arguments=$projectArg+'/Game/Maps/ForestClearing -game -port='+$Port+' -windowed -ResX='+$Width+' -ResY='+$Height+' -unattended -NoSound -nosplash -FableFrontEndValidationRole='+$role+' -FableFrontEndValidationRun='+$runId+' -FableFrontEndValidationAddress=127.0.0.1:'+$Port+' -FableFrontEndValidationDirectory="'+$directory+'" -abslog="'+$log+'" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
  $processes[$role]=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
 }
 Write-Host "Frontend processes started: $directory"
 $deadline=[DateTime]::UtcNow.AddSeconds(300)
 while([DateTime]::UtcNow -lt $deadline){
  foreach($role in @('host','guest')){
   $file=Join-Path $directory ($role+'.json')
   if(Test-Path -LiteralPath $file){
    try{$r=Get-Content -LiteralPath $file -Raw | ConvertFrom-Json}catch{continue}
    if($r.status -eq 'failed'){throw "$role failed: $($r.reason)"}
    if($r.status -eq 'passed'){$reports[$role]=$r}
   }
   $processes[$role].Refresh()
   if($processes[$role].HasExited){throw "$role process exited before completion."}
  }
  if($reports.Count -eq 2){break}
  Start-Sleep -Milliseconds 1000
 }
 if($reports.Count -ne 2){throw 'Frontend scenario timed out.'}
 foreach($role in @('host','guest')){
  $r=$reports[$role]
  if($r.run_id -ne $runId -or $r.process_id -ne $processes[$role].Id -or $r.player_count -ne 2){throw 'Frontend identity or player count mismatch.'}
  foreach($shot in $r.screenshots){
   if(-not ([IO.Path]::GetFullPath($shot).StartsWith($directory+'\',[StringComparison]::OrdinalIgnoreCase)) -or -not (Test-Path -LiteralPath $shot -PathType Leaf)){throw 'Capture missing or outside run directory.'}
  }
 }
 if(-not $reports.host.new_game_checked -or -not $reports.host.continue_checked -or -not $reports.host.previous_save_preserved -or $reports.host.network_mode -ne 'listen_server'){throw 'Host menu checkpoints missing.'}
 if(-not $reports.guest.invalid_address_rejected -or $reports.guest.network_mode -ne 'client'){throw 'Guest join checkpoints missing.'}
} catch{$failure=$_.Exception.Message}
finally{
 foreach($process in $processes.Values){$process.Refresh();if(-not $process.HasExited){[void]$process.CloseMainWindow();if(-not $process.WaitForExit(10000)){$process.Kill();[void]$process.WaitForExit(5000)}}}
}
$after=Get-RealProfileHashes
foreach($path in $before.Keys){if(-not $after.ContainsKey($path) -or $before[$path] -ne $after[$path]){$failure='Existing player profile changed during isolated validation.'}}
foreach($path in $after.Keys){if(-not $before.ContainsKey($path)){$failure='Unexpected production profile created by test.'}}
$status=if($failure){'failed'}else{'passed'}
[ordered]@{status=$status;runId=$runId;mode=$mode;width=$Width;height=$Height;executable=$exe;reason=$failure;networkScope='Same PC, two processes, menu join to IPv4 loopback';existingProfilesPreserved=($null -eq $failure);roles=$reports}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "Frontend report: $directory\summary.json"
if($failure){throw $failure}