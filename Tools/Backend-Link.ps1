# Shared source repository selection; private files stay in the backend's ignored Saved folder.
function Get-FableBackendLink {
    [CmdletBinding()]
    param([string]$BackendRoot)
    if (-not $BackendRoot) { $BackendRoot=$env:FABLE_BACKEND_ROOT }
    if (-not $BackendRoot) {
        $BackendRoot=Join-Path (Split-Path -Parent $script:FableProjectRoot) 'CatPjt-Fablewood-Backend'
    }
    $resolved=[IO.Path]::GetFullPath($BackendRoot)
    $launcher=Join-Path $resolved 'Tools\Start-Backend.ps1'
    $api=Join-Path $resolved 'Fablewood.Backend\Fablewood.Backend.csproj'
    $legacyApi=Join-Path $resolved 'Backend\Fablewood.Backend\Fablewood.Backend.csproj'
    if (-not (Test-Path -LiteralPath $launcher -PathType Leaf) -or
        (-not (Test-Path -LiteralPath $api -PathType Leaf) -and -not (Test-Path -LiteralPath $legacyApi -PathType Leaf))) {
        throw 'Backend source repository was not found. Place CatPjt-Fablewood-Backend beside this game, pass -BackendRoot, or set FABLE_BACKEND_ROOT.'
    }
    return [pscustomobject]@{
        root=$resolved
        launcher=$launcher
        stateFile=(Join-Path $resolved 'Saved\Backend\backend-state.json')
        serverConfig=(Join-Path $resolved 'Saved\Backend\server.json')
    }
}
