#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$BackendRoot,
    [ValidateRange(1024,65535)][int]$DatabasePort=55438,
    [ValidateRange(1024,65535)][int]$ApiPort=5281,
    [ValidatePattern('^[a-z][a-z0-9_]{0,62}$')][string]$Schema='fablewood',
    [ValidatePattern('^[a-zA-Z0-9_-]{1,64}$')][string]$ServerId='local-dedicated',
    [ValidateRange(10,300)][int]$ReadyTimeoutSeconds=90,
    [switch]$Restart
)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Backend-Link.ps1')
$link=Get-FableBackendLink -BackendRoot $BackendRoot
$forwarded=@{}
foreach ($entry in $PSBoundParameters.GetEnumerator()) {
    if ($entry.Key -ne 'BackendRoot') { $forwarded[$entry.Key]=$entry.Value }
}
& $link.launcher @forwarded
