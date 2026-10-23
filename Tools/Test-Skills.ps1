param(
 [string]$EngineRoot,[string]$PackagedExecutable,
 [ValidateRange(1024,65535)][int]$Port=7796,
 [ValidateRange(180,1200)][int]$TimeoutSeconds=1100,
 [ValidateRange(900,1920)][int]$Width=900,[ValidateRange(600,1080)][int]$Height=600
)
# Advancement now requires real combat trials and the Moonwell quest.
# The previous immediate-advancement scenario is retained in the pre-change baseline.
Write-Host 'Skill progression is now validated through the full advancement-trial adventure.'
& (Join-Path $PSScriptRoot 'Test-Adventure.ps1') @PSBoundParameters
