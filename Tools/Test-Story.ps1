param(
 [string]$EngineRoot,[string]$PackagedExecutable,
 [ValidateSet("Mage","Warrior")][string]$HostClass="Mage",
 [int]$Port=7800,[int]$TimeoutSeconds=1100,[int]$Width=900,[int]$Height=600
)
$PSBoundParameters['Port']=$Port
& (Join-Path $PSScriptRoot 'Test-Adventure.ps1') @PSBoundParameters -Story

