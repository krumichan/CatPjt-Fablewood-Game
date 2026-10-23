#requires -Version 7.4
[CmdletBinding()]
param([string]$EngineRoot,[string]$BackendUrl='http://127.0.0.1:5292',[ValidateRange(900,1920)][int]$Width=1000,[ValidateRange(600,1080)][int]$Height=700,[int]$WindowX=40)
. (Join-Path $PSScriptRoot 'Common.ps1')
$uri=$null
if(-not [Uri]::TryCreate($BackendUrl,[UriKind]::Absolute,[ref]$uri) -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -ne '/' -or ($uri.Scheme -ne 'https' -and -not($uri.Scheme -eq 'http' -and $uri.IsLoopback))){throw 'Use an HTTPS API root, or loopback HTTP for development.'}
$engine=Resolve-FableEngine $EngineRoot;Assert-FableReady
$exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$arguments=@(('"'+$script:FableProjectFile+'"'),'/Game/Maps/ForestClearing','-game','-d3d11','-windowed','-nosplash',('-FableOnlineBackendUrl='+$uri.AbsoluteUri.TrimEnd('/')),('-ResX='+$Width),('-ResY='+$Height),('-WinX='+$WindowX),'-WinY=40','-ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0','-ini:Engine:[ConsoleVariables]:t.IdleWhenNotForeground=0','-LogCmds="LogInit off,LogNet off,LogLoad off"')
# Credentials are entered only in the game widget. Never accept them as launcher arguments.
$p=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $script:FableProjectRoot -WindowStyle Normal -PassThru
Write-Output ([pscustomobject]@{status='started';processId=$p.Id;screen='in-game account login';backendUrl=$uri.AbsoluteUri.TrimEnd('/')})