param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [ValidateRange(1024,65535)][int]$Port=7791,
    [ValidateRange(120,900)][int]$TimeoutSeconds=360
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($PackagedExecutable) {
    if (-not (Test-Path -LiteralPath $PackagedExecutable -PathType Leaf)) { throw 'Packaged executable was not found.' }
    $executable = (Resolve-Path -LiteralPath $PackagedExecutable).Path
    $projectArgument = ''
    $mode = 'packaged-development'
} else {
    $engine = Resolve-FableEngine -EngineRoot $EngineRoot
    Assert-FableReady
    $executable = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
    $projectArgument = '"' + $script:FableProjectFile + '" '
    $mode = 'editor-game'
}
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "UDP port $Port is already in use." }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$reportDirectory = Join-Path $script:FableProjectRoot ('Saved\CoopValidation\' + $runId)
[IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
$hostReportPath = Join-Path $reportDirectory 'host.json'
$guestReportPath = Join-Path $reportDirectory 'guest.json'
$hostLog = Join-Path $reportDirectory 'host.log'
$guestLog = Join-Path $reportDirectory 'guest.log'
$hostProcess = $null
$guestProcess = $null
$startedUtc = [DateTime]::UtcNow
$failure = $null
$hostReport = $null
$guestReport = $null
$summaryPath = Join-Path $reportDirectory 'summary.json'
try {
    $common = ' -game -windowed -ResX=960 -ResY=640 -nosplash -unattended -NoSound -FableValidationDirectory="' + $reportDirectory + '" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
    $hostArguments = $projectArgument + '/Game/Maps/ForestClearing?listen -port=' + $Port + ' -MULTIHOME=127.0.0.1 -WinX=20 -WinY=40 -FableValidationRole=host -abslog="' + $hostLog + '"' + $common
    # Rendered game windows are intentionally visible for visual validation.
    $hostProcess = Start-Process -FilePath $executable -ArgumentList $hostArguments -PassThru
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(180)
    $ready = $false
    while ([DateTime]::UtcNow -lt $readyDeadline) {
        $hostProcess.Refresh()
        if ($hostProcess.HasExited) { throw "Host exited before listening; see $hostLog" }
        if (@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) | Where-Object OwningProcess -eq $hostProcess.Id) { $ready=$true; break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) { throw 'Host did not open its loopback UDP endpoint within 180 seconds.' }
    $guestArguments = $projectArgument + '127.0.0.1:' + $Port + ' -WinX=1000 -WinY=40 -FableValidationRole=guest -abslog="' + $guestLog + '"' + $common
    $guestProcess = Start-Process -FilePath $executable -ArgumentList $guestArguments -PassThru
    Write-Host "Two real game processes connected through UDP $Port. Reports: $reportDirectory"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($role in @('host','guest')) {
            $path = Join-Path $reportDirectory ($role + '.json')
            if (Test-Path -LiteralPath $path) {
                try { $report = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { continue }
                if ($report.status -in @('passed','failed')) {
                    if ($role -eq 'host') { $hostReport=$report } else { $guestReport=$report }
                }
            }
        }
        if ($hostReport -and $guestReport) { break }
        $hostProcess.Refresh(); $guestProcess.Refresh()
        if ($hostProcess.HasExited -and -not $hostReport) { throw 'Host exited without a final test report.' }
        if ($guestProcess.HasExited -and -not $guestReport) { throw 'Guest exited without a final test report.' }
        Start-Sleep -Milliseconds 1000
    }
    if (-not $hostReport -or -not $guestReport) { throw "Timed out after $TimeoutSeconds seconds waiting for both test reports." }
    if ($hostReport.status -ne 'passed' -or $guestReport.status -ne 'passed') { throw 'At least one game process reported failure. See host.json and guest.json.' }
    foreach ($pair in @(@{role='host';report=$hostReport;process=$hostProcess},@{role='guest';report=$guestReport;process=$guestProcess})) {
        $report=$pair.report
        if ($report.role -ne $pair.role -or $report.run_id -ne $runId -or $report.process_id -ne $pair.process.Id) { throw 'Report identity did not match the process and test run.' }
        $expectedMode=if($pair.role -eq 'host'){'listen_server'}else{'client'}
        if ($report.network_mode -ne $expectedMode -or -not $report.is_local_controller) { throw 'Network role did not match the real host/client process.' }
        if (-not $report.quorum_observed -or $report.maximum_player_count -ne 2) { throw 'Two-player quorum was not observed in both processes.' }
        if ($report.local_movement_cm -lt 250 -or $report.observed_remote_movement_cm -lt 150) { throw 'Local or replicated movement was insufficient.' }
        foreach ($checkpoint in @(@{name='round_one';round=1},@{name='round_two';round=2})) {
            $point=$report.($checkpoint.name)
            if ($point.round -ne $checkpoint.round -or $point.defeated -ne 3 -or $point.required -ne 3 -or -not $point.complete) { throw 'Shared objective checkpoints did not match the expected completed rounds.' }
        }
        $reset=$report.round_two_reset
        if ($reset.round -ne 2 -or $reset.defeated -ne 0 -or $reset.required -ne 3 -or $reset.complete) { throw 'The next-round reset was not observed.' }
        if ($report.screenshots.Count -lt 5) { throw 'A process did not save all five required gameplay screenshots.' }
        foreach ($path in $report.screenshots) {
            $resolvedScreenshot=[IO.Path]::GetFullPath($path)
            $allowedDirectory=[IO.Path]::GetFullPath($reportDirectory).TrimEnd('\')+'\'
            if (-not $resolvedScreenshot.StartsWith($allowedDirectory,[StringComparison]::OrdinalIgnoreCase)) { throw 'Screenshot path was outside this validation run.' }
            if (-not (Test-Path -LiteralPath $resolvedScreenshot -PathType Leaf)) { throw 'A reported screenshot does not exist.' }
        }
    }
    if (-not $hostReport.server_cooldown_checks_passed -or $hostReport.restart_input_count -lt 1) { throw 'Server cooldown or normal restart-input validation failed.' }
} catch {
    $failure = $_.Exception.Message
} finally {
    # Only stop the exact processes launched by this run. Never touch other game/editor sessions.
    foreach ($process in @($guestProcess,$hostProcess)) {
        if ($null -eq $process) { continue }
        $process.Refresh()
        if (-not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(10000)) { $process.Kill(); [void]$process.WaitForExit(5000) }
        }
    }
    $status = if ($failure) { 'failed' } else { 'passed' }
    [ordered]@{
        runId=$runId;status=$status;reason=$failure;mode=$mode
        startedUtc=$startedUtc.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o')
        engineExecutable=$executable;project=$script:FableProjectFile;port=$Port
        networkScope='Two separate processes on one PC, IPv4 loopback listen server'
        host=$hostReport;guest=$guestReport
        logs=@($hostLog,$guestLog)
        screenshots=@(Get-ChildItem -LiteralPath $reportDirectory -Filter '*.png' -File | ForEach-Object FullName)
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
}
Write-Host "Validation report: $summaryPath"
if ($failure) { throw $failure }
Write-Host 'Co-op input, replication, objective and round validation passed. Review screenshots separately for visual quality.'