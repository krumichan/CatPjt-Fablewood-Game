#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$OutputDirectory,
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 300
)

. (Join-Path $PSScriptRoot 'Common.ps1')

# 검증 실패는 비공개 로그를 출력하지 않고 정해진 코드로 보고한다. / 非公開ログを出さず固定コードで失敗する。
function Stop-RouteUiFixture([string]$Code)
{
    throw [InvalidOperationException]::new('TownRouteUiRender:' + $Code)
}

# 재분석 지점으로 출력이나 실행 파일의 소유 경계를 우회하지 못하게 한다. / 再解析点による境界迂回を拒否する。
function Assert-RouteUiPlainPath([string]$Path)
{
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor)
    {
        if (Test-Path -LiteralPath $cursor)
        {
            $item = Get-Item -LiteralPath $cursor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)
            {
                Stop-RouteUiFixture 'reparse_path'
            }
        }

        $parent = [IO.Directory]::GetParent($cursor)
        if ($null -eq $parent)
        {
            break
        }

        $cursor = $parent.FullName
    }
}

# 정규화한 경로가 명시된 소유 루트 안에 있는지 확인한다. / 正規化パスの所有範囲を確認する。
function Test-RouteUiWithin([string]$Path, [string]$Root, [switch]$AllowRoot)
{
    $candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $boundary = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return (($AllowRoot -and $candidate.Equals($boundary, [StringComparison]::OrdinalIgnoreCase)) -or
            $candidate.StartsWith($boundary + '\', [StringComparison]::OrdinalIgnoreCase))
}

# 인자를 Windows 명령행에 안전하게 전달한다. / 引数をWindowsのコマンド行へ安全に渡す。
function ConvertTo-RouteUiArgument([string]$Value)
{
    if ($Value.Contains('"') -or $Value.Contains("`r") -or $Value.Contains("`n") -or $Value.EndsWith('\'))
    {
        Stop-RouteUiFixture 'unsafe_argument'
    }

    if ($Value.StartsWith('-') -and $Value.Contains('='))
    {
        $separator = $Value.IndexOf('=')
        return $Value.Substring(0, $separator + 1) + '"' + $Value.Substring($separator + 1) + '"'
    }

    return '"' + $Value + '"'
}

# 기존 결과를 덮어쓰지 않고 UTF-8 JSON을 저장한다. / 既存結果を上書きせずUTF-8 JSONを保存する。
function Write-RouteUiNewJson([string]$Path, $Value)
{
    Assert-RouteUiPlainPath $Path
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 18))
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try
    {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
    }
    finally
    {
        $stream.Dispose()
    }
}

# 선택적 JSON 필드는 엄격 모드에서도 명시적으로 읽는다. / 任意JSONフィールドを明示的に読む。
function Read-RouteUiProperty($Object, [string]$Name)
{
    if ($null -eq $Object)
    {
        return $null
    }

    $property = $Object.PSObject.Properties[$Name]
    if ($null -ne $property)
    {
        return $property.Value
    }

    return $null
}

# 시작한 프로세스의 PID·시작 시각·실행 이미지를 함께 고정한다. / PID・開始時刻・実行画像を固定する。
function Get-RouteUiIdentity([Diagnostics.Process]$Process, [string]$ExpectedExecutable)
{
    $null = $Process.Handle
    if ($Process.HasExited)
    {
        Stop-RouteUiFixture 'process_exited_before_identity'
    }

    $image = [IO.Path]::GetFullPath($Process.Path)
    if (-not $image.Equals($ExpectedExecutable, [StringComparison]::OrdinalIgnoreCase))
    {
        Stop-RouteUiFixture 'started_image_mismatch'
    }

    $started = $Process.StartTime.ToUniversalTime()
    return [ordered]@{
        identityEstablished = $true
        pid = $Process.Id
        executable = $image
        startTimeUtc = $started.ToString('o')
        startTimeUtcTicks = $started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
    }
}

# 完全な識別に失敗しても観測できた部分値だけを残す。 / 전체 식별 실패 때도 관측한 부분값만 보존한다.
function Get-RouteUiPartialIdentity([Diagnostics.Process]$Process, [string]$ExpectedExecutable)
{
    $partial = [ordered]@{
        identityEstablished = $false
        expectedExecutable = $ExpectedExecutable
        pid = $null
        observedExecutable = $null
        observedStartTimeUtc = $null
        observedStartTimeUtcTicks = $null
    }

    if ($null -ne $Process)
    {
        try
        {
            $partial.pid = $Process.Id
        }
        catch
        {
        }
        try
        {
            $partial.observedExecutable = $Process.Path
        }
        catch
        {
        }
        try
        {
            $started = $Process.StartTime.ToUniversalTime()
            $partial.observedStartTimeUtc = $started.ToString('o')
            $partial.observedStartTimeUtcTicks = $started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
        }
        catch
        {
        }
    }

    return $partial
}

# PID가 재사용돼도 원래 소유 프로세스에만 종료를 요청한다. / PID再利用時も元の所有プロセスだけを終了する。
function Stop-RouteUiOwnedProcess([Diagnostics.Process]$Process, $Identity)
{
    if ($null -eq $Process)
    {
        return [ordered]@{ processWasStarted = $false; cleanupComplete = $true; originalAlreadyExited = $false; normalCloseRequested = $false; forced = $false }
    }

    if ($Process.HasExited)
    {
        return [ordered]@{ processWasStarted = $true; cleanupComplete = $true; originalAlreadyExited = $true; normalCloseRequested = $false; forced = $false }
    }

    # 살아 있는 프로세스의 식별이 없으면 종료도 완료 주장도 하지 않는다. / 生存プロセスの識別がなければ終了も完了主張もしない。
    if ($null -eq $Identity)
    {
        Stop-RouteUiFixture 'owned_live_process_identity_unavailable'
    }

    $current = Get-Process -Id $Identity.pid -ErrorAction SilentlyContinue
    if ($null -eq $current)
    {
        Stop-RouteUiFixture 'owned_process_state_unavailable'
    }

    try
    {
        $null = $current.Handle
        $ticks = $current.StartTime.ToUniversalTime().Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
        $image = [IO.Path]::GetFullPath($current.Path)
        if ($ticks -cne $Identity.startTimeUtcTicks -or
            -not $image.Equals($Identity.executable, [StringComparison]::OrdinalIgnoreCase))
        {
            Stop-RouteUiFixture 'owned_process_identity_changed'
        }

        $closed = $current.CloseMainWindow()
        $forced = $false
        if (-not $current.WaitForExit(10000))
        {
            # 열린 원래 프로세스 핸들을 다시 확인하며 다른 PID나 앱에는 접근하지 않는다.
            # 元のプロセスハンドルを再確認し、別PIDやアプリには触れない。
            if ($current.HasExited -or
                $current.StartTime.ToUniversalTime().Ticks.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $Identity.startTimeUtcTicks)
            {
                Stop-RouteUiFixture 'owned_process_changed_before_stop'
            }

            $current.Kill()
            $forced = $true
            if (-not $current.WaitForExit(10000))
            {
                Stop-RouteUiFixture 'owned_process_stop_timeout'
            }
        }

        return [ordered]@{ processWasStarted = $true; cleanupComplete = $true; originalAlreadyExited = $false; normalCloseRequested = [bool]$closed; forced = $forced }
    }
    finally
    {
        $current.Dispose()
    }
}

# 실제 PNG의 서명·IHDR 크기·끝 청크와 원문 해시를 확인한다. / PNGの署名・寸法・終端・原文ハッシュを確認する。
function Read-RouteUiPng([string]$Path, [string]$CaseDirectory, [int]$Width, [int]$Height)
{
    $full = [IO.Path]::GetFullPath($Path)
    if (-not (Test-RouteUiWithin $full $CaseDirectory) -or [IO.Path]::GetExtension($full) -ine '.png')
    {
        Stop-RouteUiFixture 'png_outside_owned_phase'
    }

    Assert-RouteUiPlainPath $full
    $file = Get-Item -LiteralPath $full -Force
    if ($file.PSIsContainer -or $file.Length -lt 45 -or $file.Length -gt 33554432)
    {
        Stop-RouteUiFixture 'png_size_invalid'
    }

    $stream = [IO.File]::OpenRead($full)
    try
    {
        $head = New-Object byte[] 33
        if ($stream.Read($head, 0, $head.Length) -ne $head.Length)
        {
            Stop-RouteUiFixture 'png_header_incomplete'
        }

        $signature = @(137, 80, 78, 71, 13, 10, 26, 10)
        for ($index = 0; $index -lt $signature.Count; $index++)
        {
            if ($head[$index] -ne $signature[$index])
            {
                Stop-RouteUiFixture 'png_signature_invalid'
            }
        }

        if (($head[8..15] -join ',') -cne '0,0,0,13,73,72,68,82')
        {
            Stop-RouteUiFixture 'png_ihdr_invalid'
        }

        [long]$actualWidth = ([long]$head[16] * 16777216) + ([long]$head[17] * 65536) + ([long]$head[18] * 256) + $head[19]
        [long]$actualHeight = ([long]$head[20] * 16777216) + ([long]$head[21] * 65536) + ([long]$head[22] * 256) + $head[23]
        if ($actualWidth -ne $Width -or $actualHeight -ne $Height)
        {
            Stop-RouteUiFixture 'png_dimensions_mismatch'
        }

        $null = $stream.Seek(-12, [IO.SeekOrigin]::End)
        $tail = New-Object byte[] 12
        if ($stream.Read($tail, 0, $tail.Length) -ne $tail.Length -or
            ($tail -join ',') -cne '0,0,0,0,73,69,78,68,174,66,96,130')
        {
            Stop-RouteUiFixture 'png_iend_incomplete'
        }
    }
    finally
    {
        $stream.Dispose()
    }

    return [ordered]@{
        path = $full
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
        width = $actualWidth
        height = $actualHeight
        createdUtc = $file.CreationTimeUtc.ToString('o')
        lastWriteUtc = $file.LastWriteTimeUtc.ToString('o')
        headerAndEndChunkVerified = $true
        visualContentVerified = $false
    }
}

# 실행 파일과 출력 경계는 엔진을 시작하기 전에 고정한다. / 起動前に実行画像と出力境界を固定する。
if ($EngineRoot -and $PackagedExecutable)
{
    Stop-RouteUiFixture 'choose_one_executable_mode'
}

$projectRoot = [IO.Path]::GetFullPath($script:FableProjectRoot).TrimEnd('\', '/')
$projectFile = [IO.Path]::GetFullPath($script:FableProjectFile)
$mode = 'editor-game'
if ($PackagedExecutable)
{
    $executable = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $PackagedExecutable).Path)
    $binaryDirectory = [IO.Path]::GetDirectoryName($executable)
    $binaryParent = [IO.Path]::GetDirectoryName($binaryDirectory)
    if ([IO.Path]::GetFileName($binaryDirectory) -ine 'Win64' -or
        [IO.Path]::GetFileName($binaryParent) -ine 'Binaries' -or
        [IO.Path]::GetFileName($executable) -notmatch '^(?i:Fablewood(?:-Win64-Development)?\.exe)$')
    {
        Stop-RouteUiFixture 'packaged_inner_executable_required'
    }

    $packageProjectRoot = [IO.Path]::GetDirectoryName($binaryParent)
    if ([IO.Path]::GetFileName($packageProjectRoot) -ine 'Fablewood' -and
        -not $packageProjectRoot.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        Stop-RouteUiFixture 'packaged_project_layout_required'
    }

    $mode = 'packaged-development'
}
else
{
    $engine = Resolve-FableEngine -EngineRoot $EngineRoot
    Assert-FableReady
    $executable = [IO.Path]::GetFullPath((Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'))
}

Assert-RouteUiPlainPath $projectRoot
Assert-RouteUiPlainPath $executable
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
$savedBoundary = Join-Path $projectRoot 'Saved\TownRouteUiRender'
if ($OutputDirectory -and -not [IO.Path]::IsPathRooted($OutputDirectory))
{
    Stop-RouteUiFixture 'absolute_output_required'
}

$base = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/') } else { $savedBoundary }
$allowed = Test-RouteUiWithin $base $savedBoundary -AllowRoot
$outputScope = 'project-saved'

# 공용 작업의 결과 루트만 추가 허용하며 임의의 외부 디렉터리는 허용하지 않는다.
# 共通ジョブの結果ルートだけを追加で許可し、任意の外部出力を拒否する。
if (-not $allowed -and $env:CODEX_DESKTOP_JOB_ROOT -and $env:CODEX_DESKTOP_RESULTS_DIR)
{
    $jobRoot = [IO.Path]::GetFullPath($env:CODEX_DESKTOP_JOB_ROOT).TrimEnd('\', '/')
    $resultBoundary = [IO.Path]::GetFullPath($env:CODEX_DESKTOP_RESULTS_DIR).TrimEnd('\', '/')
    $expectedResults = [IO.Path]::GetFullPath((Join-Path $jobRoot 'results')).TrimEnd('\', '/')
    if ($resultBoundary.Equals($expectedResults, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-RouteUiWithin $projectRoot $jobRoot) -and
        ($mode -eq 'editor-game' -or (Test-RouteUiWithin $executable $jobRoot)) -and
        (Test-RouteUiWithin $base $resultBoundary -AllowRoot))
    {
        Assert-RouteUiPlainPath $jobRoot
        $allowed = $true
        $outputScope = 'owned-job-results'
    }
}

if (-not $allowed)
{
    Stop-RouteUiFixture 'output_outside_owned_scope'
}

if ($mode -eq 'packaged-development' -and $outputScope -eq 'project-saved')
{
    $packageProjectRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($executable)))
    if (-not $packageProjectRoot.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        Stop-RouteUiFixture 'packaged_mode_requires_owned_results_output'
    }
}

Assert-RouteUiPlainPath $base
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$directory = Join-Path $base $runId
if (Test-Path -LiteralPath $directory)
{
    Stop-RouteUiFixture 'fresh_run_directory_required'
}

$null = [IO.Directory]::CreateDirectory($directory)
Assert-RouteUiPlainPath $directory
# 원문 로그는 공개 결과의 재귀 수집 경계 밖에 둔다. / 生ログを公開結果の再帰収集境界の外へ置く。
$privateBase = Join-Path $projectRoot 'Saved\TownRouteUiRender-Private'
if ($env:CODEX_DESKTOP_JOB_ROOT -or $env:CODEX_DESKTOP_RESULTS_DIR)
{
    if (-not $env:CODEX_DESKTOP_JOB_ROOT -or -not $env:CODEX_DESKTOP_RESULTS_DIR)
    {
        Stop-RouteUiFixture 'incomplete_owned_job_environment'
    }

    $privateJobRoot = [IO.Path]::GetFullPath($env:CODEX_DESKTOP_JOB_ROOT).TrimEnd('\', '/')
    $privateResultsRoot = [IO.Path]::GetFullPath($env:CODEX_DESKTOP_RESULTS_DIR).TrimEnd('\', '/')
    $expectedPrivateResults = [IO.Path]::GetFullPath((Join-Path $privateJobRoot 'results')).TrimEnd('\', '/')
    if (-not $privateResultsRoot.Equals($expectedPrivateResults, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-RouteUiWithin $projectRoot $privateJobRoot) -or
        ($mode -eq 'packaged-development' -and -not (Test-RouteUiWithin $executable $privateJobRoot)))
    {
        Stop-RouteUiFixture 'private_log_job_scope_mismatch'
    }

    Assert-RouteUiPlainPath $privateJobRoot
    $privateBase = Join-Path $privateJobRoot 'private\route-ui-render'
    if (Test-RouteUiWithin $privateBase $privateResultsRoot -AllowRoot)
    {
        Stop-RouteUiFixture 'private_logs_inside_public_results'
    }
}

Assert-RouteUiPlainPath $privateBase
$privateDirectory = Join-Path $privateBase $runId
if (Test-Path -LiteralPath $privateDirectory)
{
    Stop-RouteUiFixture 'private_log_directory_not_fresh'
}
$null = [IO.Directory]::CreateDirectory($privateDirectory)
Assert-RouteUiPlainPath $privateDirectory
$summaryPath = Join-Path $directory 'result.json'
$summary = [ordered]@{
    kind = 'fablewood-town-route-ui-render-fixture'
    status = 'running'
    runId = $runId
    runDirectory = $directory
    startedUtc = [DateTime]::UtcNow.ToString('o')
    mode = $mode
    executable = $executable
    executableSha256 = $executableHash
    outputScope = $outputScope
    sourceNativeExpected = 91
    nativeAutomationExecutedByThisRunner = $false
    fixtureExecutionPassed = $false
    actualVisualReviewPassed = $false
    visualReviewPassed = $false
    networkScope = 'single-process-standalone-render-fixture'
    runtimeScope = 'Synthetic logical catalog and actual product HUD; no physical upper-floor or adopted-town proof.'
    persistenceScope = 'Existing FableValidationRole=host disables persistence; no backend or online flags.'
    rawLogsPrivate = $true
    publicRawLogsExported = $false
    rawLogsDirectory = $privateDirectory
    phases = @()
    failureCode = $null
}
Write-RouteUiNewJson (Join-Path $directory 'runner-owner.json') ([ordered]@{
    runId = $runId
    createdUtc = $summary.startedUtc
    executable = $executable
    executableSha256 = $executableHash
    launcherProcessId = $PID
    sourceScriptSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
})

try
{
    # 렌더링은 한 번에 한 프로세스만 실행한다. / 描画プロセスを一つずつ順番に実行する。
    foreach ($resolution in @(@{ width = 900; height = 600 }, @{ width = 1280; height = 800 }))
    {
        $name = [string]$resolution.width + 'x' + [string]$resolution.height
        $phaseDirectory = Join-Path $directory $name
        $null = [IO.Directory]::CreateDirectory($phaseDirectory)
        Assert-RouteUiPlainPath $phaseDirectory
        $fixturePath = Join-Path $phaseDirectory 'fixture-report.json'
        $phase = [ordered]@{
            name = $name
            width = $resolution.width
            height = $resolution.height
            runId = $runId
            outputDirectory = $phaseDirectory
            status = 'running'
            process = $null
            fixtureReport = $null
            screenshots = @()
            cleanup = $null
            rawLog = [ordered]@{ path = (Join-Path $privateDirectory ($name + '.log')); rawContentsIncluded = $false }
        }
        $summary.phases += $phase
        $process = $null
        $identity = $null

        try
        {
            $phaseExecutableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
            $phase['executableSha256'] = $phaseExecutableHash
            if ($phaseExecutableHash -cne $executableHash)
            {
                Stop-RouteUiFixture 'executable_changed_between_phases'
            }

            $arguments = @()
            if ($mode -eq 'editor-game')
            {
                $arguments += $projectFile
            }

            $arguments += @('/Game/Maps/ForestClearing', '-game', '-windowed',
                ('-ResX=' + $resolution.width), ('-ResY=' + $resolution.height), '-nosplash', '-unattended', '-NoSound',
                '-FableValidationRole=host', '-FableTownRouteUiFixture', ('-FableTownRouteUiFixtureRun=' + $runId),
                ('-FableTownRouteUiFixtureDirectory=' + $phaseDirectory),
                ('-abslog=' + (Join-Path $privateDirectory ($name + '.log'))),
                '-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0')
            $argumentLine = (@($arguments | ForEach-Object { ConvertTo-RouteUiArgument ([string]$_) }) -join ' ')
            $process = Start-Process -FilePath $executable -ArgumentList $argumentLine -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
            $phase.process = Get-RouteUiPartialIdentity $process $executable
            $identity = Get-RouteUiIdentity $process $executable
            $phase.process = $identity
            $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            $fixture = $null

            # 최종 JSON이 완성될 때까지 기다리며 엔진 원문 로그는 공개 출력에 넣지 않는다.
            # 最終JSONの完成を待ち、エンジンの生ログは公開出力へ出さない。
            while ([DateTime]::UtcNow -lt $deadline)
            {
                if (Test-Path -LiteralPath $fixturePath -PathType Leaf)
                {
                    Assert-RouteUiPlainPath $fixturePath
                    if ((Get-Item -LiteralPath $fixturePath).Length -gt 2097152)
                    {
                        Stop-RouteUiFixture 'fixture_report_bound_exceeded'
                    }

                    try
                    {
                        $candidate = Get-Content -LiteralPath $fixturePath -Raw -Encoding UTF8 | ConvertFrom-Json
                    }
                    catch
                    {
                        $candidate = $null
                    }

                    if ($null -ne $candidate -and (Read-RouteUiProperty $candidate 'status') -in @('passed', 'failed'))
                    {
                        $fixture = $candidate
                        break
                    }
                }

                if ($process.HasExited)
                {
                    Stop-RouteUiFixture 'fixture_process_exited_before_report'
                }

                Start-Sleep -Milliseconds 250
            }

            if ($null -eq $fixture)
            {
                Stop-RouteUiFixture 'fixture_timeout'
            }

            # 실패 JSON도 원문 경로와 해시를 먼저 보존한다. / 失敗JSONも元パスとハッシュを先に保存する。
            $phase.fixtureReport = [ordered]@{
                path = $fixturePath
                bytes = (Get-Item -LiteralPath $fixturePath).Length
                sha256 = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            if ((Read-RouteUiProperty $fixture 'status') -cne 'passed')
            {
                Stop-RouteUiFixture 'fixture_report_failed'
            }

            $reportedResolution = Read-RouteUiProperty $fixture 'resolution'
            if ((Read-RouteUiProperty $fixture 'runId') -cne $runId -or
                (Read-RouteUiProperty $fixture 'processId') -ne $identity.pid -or
                (Read-RouteUiProperty $fixture 'networkMode') -cne 'standalone' -or
                (Read-RouteUiProperty $fixture 'sourceNativeExpected') -ne 91 -or
                (Read-RouteUiProperty $fixture 'visualReviewPassed') -isnot [bool] -or
                (Read-RouteUiProperty $fixture 'visualReviewPassed') -ne $false -or
                (Read-RouteUiProperty $reportedResolution 'width') -ne $resolution.width -or
                (Read-RouteUiProperty $reportedResolution 'height') -ne $resolution.height)
            {
                Stop-RouteUiFixture 'fixture_identity_or_scope_mismatch'
            }

            $snapshots = @(Read-RouteUiProperty $fixture 'caseSnapshots')
            $expectedNames = @('direct', 'via-and-occlusion', 'no-route-cleared', 'direct-restored')
            if ($snapshots.Count -ne $expectedNames.Count)
            {
                Stop-RouteUiFixture 'fixture_case_count_mismatch'
            }

            $paths = @()
            for ($index = 0; $index -lt $expectedNames.Count; $index++)
            {
                $snapshot = $snapshots[$index]
                $caseName = Read-RouteUiProperty $snapshot 'name'
                $shotPath = Read-RouteUiProperty $snapshot 'screenshotPath'
                if ($caseName -cne $expectedNames[$index] -or $shotPath -isnot [string] -or [IO.Path]::IsPathRooted($shotPath) -ne $true)
                {
                    Stop-RouteUiFixture 'fixture_case_order_or_path_mismatch'
                }

                $png = Read-RouteUiPng $shotPath $phaseDirectory $resolution.width $resolution.height
                if ($paths -contains $png.path.ToLowerInvariant())
                {
                    Stop-RouteUiFixture 'fixture_reused_screenshot'
                }

                $paths += $png.path.ToLowerInvariant()
                $png['caseName'] = $caseName
                $phase.screenshots += $png
            }

            $phase.fixtureReport = [ordered]@{
                path = $fixturePath
                bytes = (Get-Item -LiteralPath $fixturePath).Length
                sha256 = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
            }

            if (-not $process.WaitForExit(30000))
            {
                Stop-RouteUiFixture 'fixture_did_not_exit_normally'
            }

            $exitObserved = [DateTime]::UtcNow
            $phase.process['waitCompleted'] = $true
            $phase.process['exitObservedUtc'] = $exitObserved.ToString('o')
            $phase.process['exitObservedUtcTicks'] = $exitObserved.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
            $phase.process['exitCode'] = $process.ExitCode
            if ($process.ExitCode -ne 0)
            {
                Stop-RouteUiFixture 'fixture_process_exit_nonzero'
            }

            $phase.status = 'passed'
        }
        catch
        {
            $phase.status = 'failed'
            $detail = [string]$_.Exception.Message
            $phase['failureCode'] = if ($detail -cmatch '^TownRouteUiRender:[a-z0-9_]+$') { $detail } else { 'TownRouteUiRender:io_or_process_observation_failed' }
            throw
        }
        finally
        {
            # 본 실패와 소유 종료 실패를 둘 다 남겨 원래 원인을 덮어쓰지 않는다.
            # 本体失敗と所有終了失敗を両方残し、元の原因を上書きしない。
            try
            {
                $phase.cleanup = Stop-RouteUiOwnedProcess $process $identity
            }
            catch
            {
                $detail = [string]$_.Exception.Message
                $phase['cleanupFailureCode'] = if ($detail -cmatch '^TownRouteUiRender:[a-z0-9_]+$') { $detail } else { 'TownRouteUiRender:owned_cleanup_observation_failed' }
                $phase.cleanup = [ordered]@{
                    processWasStarted = ($null -ne $process)
                    cleanupComplete = $false
                    originalExitObserved = $false
                    originalAlreadyExited = $false
                    identityAvailable = ($null -ne $identity)
                    normalCloseRequested = $false
                    forced = $false
                }
                if ($phase.status -eq 'passed')
                {
                    $phase.status = 'failed'
                    throw
                }
            }
            finally
            {
                # 原文を読まず存在・長さ・ハッシュだけを参照する。 / 원문 없이 존재·길이·해시만 참조한다.
                try
                {
                    if (Test-Path -LiteralPath $phase.rawLog.path -PathType Leaf)
                    {
                        $phase.rawLog['exists'] = $true
                        $phase.rawLog['bytes'] = (Get-Item -LiteralPath $phase.rawLog.path).Length
                        $phase.rawLog['observedUtc'] = [DateTime]::UtcNow.ToString('o')
                        $exitObserved = $null -ne $process -and $process.HasExited
                        $phase.rawLog['processExitObserved'] = $exitObserved
                        if ($exitObserved)
                        {
                            $phase.rawLog['sha256'] = (Get-FileHash -LiteralPath $phase.rawLog.path -Algorithm SHA256).Hash.ToLowerInvariant()
                        }
                        else
                        {
                            $phase.rawLog['hashUnavailableReason'] = 'owned_process_exit_not_observed'
                        }
                    }
                    else
                    {
                        $phase.rawLog['exists'] = $false
                    }
                }
                catch
                {
                    $phase.rawLog['observationUnavailable'] = $true
                }
                if ($null -ne $process)
                {
                    $process.Dispose()
                }
            }
        }
    }

    $summary.fixtureExecutionPassed = $true
    $summary.status = 'passed'
}
catch
{
    $message = [string]$_.Exception.Message
    $summary.failureCode = if ($message -cmatch '^TownRouteUiRender:[a-z0-9_]+$') { $message } else { 'TownRouteUiRender:io_or_process_observation_failed' }
    $summary.status = 'failed'
    $summary.fixtureExecutionPassed = $false
}
finally
{
    # 시각 검수는 원본 PNG를 별도로 확인한 뒤 기록한다. 파일 성공만으로 픽셀 성공을 만들지 않는다.
    # 目視検査は元PNGを確認してから別記し、ファイル成功だけで描画成功にしない。
    $summary['finishedUtc'] = [DateTime]::UtcNow.ToString('o')
    Write-RouteUiNewJson $summaryPath $summary
}

[ordered]@{ status = $summary.status; runId = $runId; runDirectory = $directory; summaryPath = $summaryPath } | ConvertTo-Json -Compress
if ($summary.status -ne 'passed')
{
    exit 1
}

exit 0
