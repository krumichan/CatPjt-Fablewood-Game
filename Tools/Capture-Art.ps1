param([string]$EngineRoot,[string]$PackagedExecutable)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($PackagedExecutable) { $exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path; $projectArgument='' }
else { $engine=Resolve-FableEngine -EngineRoot $EngineRoot; $exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'; $projectArgument='"'+$script:FableProjectFile+'" ' }
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$directory=Join-Path $script:FableProjectRoot ('Saved\ArtReview\'+$runId)
[IO.Directory]::CreateDirectory($directory) | Out-Null
$argsText=$projectArgument+'/Game/Maps/ForestClearing -game -windowed -ResX=1600 -ResY=900 -nosplash -unattended -NoSound -FableArtReview -FableArtReviewDirectory="'+$directory+'" -abslog="'+(Join-Path $directory 'game.log')+'" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
$gameProcess=Start-Process -FilePath $exe -ArgumentList $argsText -PassThru
Write-Host "Art review game process $($gameProcess.Id); captures: $directory"
if (-not $gameProcess.WaitForExit(180000)) { Stop-Process -Id $gameProcess.Id -Force; throw 'Art review timed out.' }
$gameProcess.Refresh()
if ($gameProcess.ExitCode -ne 0) { throw "Art review exited with code $($gameProcess.ExitCode)." }
$shots=@('hero.png','forest.png','bridge.png')
foreach ($shot in $shots) { if (-not (Test-Path -LiteralPath (Join-Path $directory $shot))) { throw "Missing art capture: $shot" } }
[ordered]@{status='captured';runId=$runId;processId=$gameProcess.Id;executable=$exe;screenshots=$shots;note='Art review uses fixed cameras and stationary enemies; independent co-op test exercises gameplay.'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'result.json') -Encoding UTF8
Write-Host "Art review captured: $directory"
