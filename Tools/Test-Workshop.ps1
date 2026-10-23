param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$SourceSummary,
    [ValidateRange(1024,65535)][int]$Port = 7799,
    [ValidateRange(180,3600)][int]$TimeoutSeconds = 2400,
    [ValidateRange(900,1920)][int]$Width = 900,
    [ValidateRange(600,1080)][int]$Height = 600
)
$ErrorActionPreference='Stop'
foreach($name in @('Port','TimeoutSeconds','Width','Height')) { $PSBoundParameters[$name]=Get-Variable -Name $name -ValueOnly }
& (Join-Path $PSScriptRoot 'Test-Loot.ps1') -Workshop @PSBoundParameters
