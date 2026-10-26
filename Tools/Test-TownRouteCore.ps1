#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$CompilerPath,
    [string]$WindowsSdkRoot,
    [string]$WindowsSdkVersion,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-CoreRouteCompiler
{
    # 실제 컴파일러·헤더·동적 런타임 라이브러리가 있는 도구만 고른다. / 実在するコンパイラ・ヘッダ・ライブラリを選ぶ。
    if ($CompilerPath)
    {
        return [IO.Path]::GetFullPath($CompilerPath)
    }

    $candidates = @()
    $visualStudioRoot = Join-Path $env:ProgramFiles 'Microsoft Visual Studio'
    foreach ($version in @(Get-ChildItem -LiteralPath $visualStudioRoot -Directory -ErrorAction SilentlyContinue))
    {
        foreach ($edition in @('BuildTools', 'Community', 'Professional', 'Enterprise'))
        {
            $tools = Join-Path $version.FullName ($edition + '\VC\Tools\MSVC')
            foreach ($tool in @(Get-ChildItem -LiteralPath $tools -Directory -ErrorAction SilentlyContinue))
            {
                $path = Join-Path $tool.FullName 'bin\Hostx64\x64\cl.exe'
                if ((Test-Path -LiteralPath $path) -and (Test-Path -LiteralPath (Join-Path $tool.FullName 'include\vector')) -and
                    (Test-Path -LiteralPath (Join-Path $tool.FullName 'lib\x64\msvcprt.lib')))
                {
                    $candidates += [pscustomobject]@{Path = $path; Version = [version]$tool.Name}
                }
            }
        }
    }

    $selected = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected)
    {
        throw 'An installed x64 MSVC C++ compiler is required. Specify -CompilerPath if it is installed elsewhere.'
    }
    return $selected.Path
}

function Invoke-CoreRouteProcess([string]$Executable, [string[]]$Arguments, [string]$Name)
{
    # 프로세스별 환경만 사용하며 전역 PATH·컴파일 설정을 바꾸지 않는다. / 子プロセス環境だけを使用する。
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.WorkingDirectory = $OutputDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments)
    {
        $info.ArgumentList.Add($argument)
    }
    $info.Environment['PATH'] = (Split-Path -Parent $CompilerPath) + ';' + [Environment]::GetEnvironmentVariable('PATH')
    foreach ($key in @('CL', '_CL_', 'INCLUDE', 'LIB', 'LIBPATH'))
    {
        $info.Environment.Remove($key) | Out-Null
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try
    {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $OutputDirectory ($Name + '.stdout.log')), $output, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $OutputDirectory ($Name + '.stderr.log')), $errors, [Text.UTF8Encoding]::new($false))
        if ($process.ExitCode -ne 0)
        {
            throw ($Name + ' failed; see ' + $OutputDirectory)
        }
        return $output
    }
    finally
    {
        $process.Dispose()
    }
}

# 검사는 새 Saved 하위 폴더에서만 산출물을 만든다. / 出力は新規Saved配下フォルダに限定する。
if (-not $IsWindows)
{
    throw 'This maintained runner requires Windows/MSVC; the core source itself is portable C++17.'
}
$CompilerPath = Resolve-CoreRouteCompiler
$toolRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $CompilerPath) '..\..\..'))
if (-not $WindowsSdkRoot)
{
    $WindowsSdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
}
if (-not $WindowsSdkVersion)
{
    $WindowsSdkVersion = @(Get-ChildItem -LiteralPath (Join-Path $WindowsSdkRoot 'Include') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdio.h') } |
        Select-Object -First 1)[0].Name
}
if (-not $OutputDirectory)
{
    $OutputDirectory = Join-Path $projectRoot ('Saved\TownRouteCoreTests\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$allowedOutputRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\TownRouteCoreTests'))
if (-not $OutputDirectory.StartsWith($allowedOutputRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $OutputDirectory))
{
    throw 'A fresh directory under this project Saved/TownRouteCoreTests is required.'
}
[void][IO.Directory]::CreateDirectory($OutputDirectory)

$includes = @((Join-Path $toolRoot 'include'), (Join-Path $WindowsSdkRoot ('Include\' + $WindowsSdkVersion + '\ucrt')),
    (Join-Path $WindowsSdkRoot ('Include\' + $WindowsSdkVersion + '\um')),
    (Join-Path $WindowsSdkRoot ('Include\' + $WindowsSdkVersion + '\shared')))
$libraries = @((Join-Path $toolRoot 'lib\x64'), (Join-Path $WindowsSdkRoot ('Lib\' + $WindowsSdkVersion + '\ucrt\x64')),
    (Join-Path $WindowsSdkRoot ('Lib\' + $WindowsSdkVersion + '\um\x64')))
foreach ($path in @($CompilerPath) + $includes + $libraries)
{
    if (-not (Test-Path -LiteralPath $path))
    {
        throw ('Missing compiler input: ' + $path)
    }
}

# 같은 제품 cpp를 직접 컴파일하며 UE 흉내·별도 알고리즘 사본을 사용하지 않는다. / 製品cppそのものをコンパイルする。
$sourceRoot = Join-Path $projectRoot 'Source\Fablewood'
$common = @('/nologo', '/c', '/std:c++17', '/EHsc', '/MD', '/utf-8', '/W4', '/WX', '/permissive-') +
    @($includes | ForEach-Object { '/I' + $_ }) + @('/I' + $sourceRoot)
$coreObject = Join-Path $OutputDirectory 'TownRouteCore.obj'
$testObject = Join-Path $OutputDirectory 'TownRouteCoreTests.obj'
$null = Invoke-CoreRouteProcess $CompilerPath ($common + @(('/Fo' + $coreObject), (Join-Path $sourceRoot 'FableTownRouteCore.cpp'))) 'compile-core'
$null = Invoke-CoreRouteProcess $CompilerPath ($common + @(('/Fo' + $testObject), (Join-Path $projectRoot 'Tools\Tests\TownRouteCoreTests.cpp'))) 'compile-tests'

$executable = Join-Path $OutputDirectory 'TownRouteCoreTests.exe'
$linker = Join-Path (Split-Path -Parent $CompilerPath) 'link.exe'
$null = Invoke-CoreRouteProcess $linker (@('/nologo', ('/OUT:' + $executable), $coreObject, $testObject) + @($libraries | ForEach-Object { '/LIBPATH:' + $_ })) 'link-tests'
$testOutput = Invoke-CoreRouteProcess $executable @() 'run-tests'
$outcome = $testOutput.Trim() | ConvertFrom-Json
if ($outcome.status -ne 'passed' -or $outcome.cases -ne 9 -or $outcome.failures -ne 0)
{
    throw 'The actual portable core tests did not pass.'
}

$result = [ordered]@{status = 'passed'; scope = 'portable-product-core-only'; actualUnrealBuild = $false; actualNetworkTest = $false;
    cases = $outcome.cases; assertions = $outcome.assertions; failures = $outcome.failures;
    compiler = $CompilerPath; windowsSdkVersion = $WindowsSdkVersion; outputDirectory = $OutputDirectory;
    coreSha256 = (Get-FileHash -LiteralPath (Join-Path $sourceRoot 'FableTownRouteCore.cpp') -Algorithm SHA256).Hash.ToLowerInvariant();
    testSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Tools\Tests\TownRouteCoreTests.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'result.json'), ($result | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 5
