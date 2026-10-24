[CmdletBinding()]
param([string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$files=[Collections.Generic.List[IO.FileInfo]]::new()
$excluded=[Collections.Generic.List[string]]::new()
$allowedExtensions=@('.h','.hpp','.cpp','.c','.cs','.csproj','.props','.targets','.json','.sql','.yaml','.yml','.md','.txt','.ini','.config','.ps1','.py','.cmd','.bat','.sh','.sln','.slnx','.http','.uproject','.uasset','.umap','.obj','.mtl','.png','.jpg','.jpeg','.tga','.dds','.wav','.ogg','.ttf','.otf','.ico','.rc','.manifest','.xml','.editorconfig','.dockerignore')
function Get-RelativeSourcePath([string]$Path) { return $Path.Substring($projectRoot.Length+1).Replace('\','/') }
function Add-SourceDirectory([string]$Directory) {
    foreach ($entry in Get-ChildItem -LiteralPath $Directory -Force) {
        $relative=Get-RelativeSourcePath $entry.FullName
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unexpected reparse point in source: $relative" }
        if ($entry.PSIsContainer) {
            $name=$entry.Name.ToLowerInvariant()
            $skip=$name -in @('.git','.vs','bin','obj','artifacts','testresults','__pycache__','node_modules','.local','.secrets')
            if ($skip) { $excluded.Add($relative+'/'); continue }
            Add-SourceDirectory $entry.FullName
            continue
        }
        if ($entry.Name -match '^\.env(?:\..*)?$' -and $entry.Name -ne '.env.example') { $excluded.Add($relative); continue }
        if ($entry.Name -like 'appsettings.Local.json' -or $entry.Name -like 'appsettings.*.local.json' -or $entry.Extension -in @('.log','.pyc','.pdb','.dll','.exe','.suo','.user','.tmp')) { $excluded.Add($relative); continue }
        if ($entry.Extension.ToLowerInvariant() -notin $allowedExtensions -and $entry.Name -notin @('Dockerfile','.env.example','.dockerignore','.editorconfig')) {
            throw "Unclassified source file must be explicitly included or excluded: $relative"
        }
        $files.Add($entry)
    }
}
foreach ($directory in @('Source','Config','Content','ArtSource','Tools','ThirdPartyNotices','Build','Docs','docs')) {
    $path=Join-Path $projectRoot $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        # Windows is case-insensitive: Docs/docs must not be enumerated twice.
        if ($directory -ceq 'docs' -and (Test-Path -LiteralPath (Join-Path $projectRoot 'Docs'))) { continue }
        Add-SourceDirectory $path
    }
}
foreach ($name in @('Fablewood.uproject','README.md','.gitignore','.gitattributes','.editorconfig','.clang-format')) {
    $path=Join-Path $projectRoot $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { $files.Add((Get-Item -LiteralPath $path)) }
}
foreach ($file in Get-ChildItem -LiteralPath $projectRoot -Filter '*.cmd' -File) { $files.Add($file) }
$reference=Join-Path $projectRoot 'ArtDirection\forest-reference.png'
if (Test-Path -LiteralPath $reference -PathType Leaf) { $files.Add((Get-Item -LiteralPath $reference)) }
$selected=@($files | Sort-Object FullName -Unique)
$paths=@($selected | ForEach-Object { Get-RelativeSourcePath $_.FullName })
$required=@('Fablewood.uproject','.gitignore','README.md','Config/DefaultEngine.ini','Config/DefaultGame.ini','Config/DefaultInput.ini',
    'Source/FablewoodServer.Target.cs','Source/FablewoodClient.Target.cs','Content/Maps/ForestClearing.umap',
    'Source/Fablewood/FableInventorySnapshot.cpp','Source/Fablewood/FableInventorySnapshot.h','Source/Fablewood/FableInventoryTests.cpp',
    'Content/UI/Fonts/F_Korean.uasset','Content/UI/Fonts/F_KoreanFace.uasset','ArtSource/Fonts/NanumGothic.ttf','ArtSource/Fonts/LICENSE.txt',
    'ArtSource/Textures/T_StorybookMeadow.png','Content/Art/Textures/T_StorybookMeadow.uasset',
    'Content/Art/Materials/M_FablePaint.uasset','Content/Art/Materials/M_FableGround.uasset','Content/Art/Materials/M_FableWater.uasset',
    'Tools/Backend-Link.ps1','Tools/Get-SourceManifest.ps1','Docs/DevelopmentHistory.md',
    'Source/Fablewood/FableOnlineFlowSubsystem.h','Source/Fablewood/FableOnlineFlowSubsystem.cpp',
    'Source/Fablewood/FableOnlineWidget.h','Source/Fablewood/FableOnlineWidget.cpp',
    'Source/Fablewood/FableCharacterSummary.h','Source/Fablewood/FableCharacterSummary.cpp','Source/Fablewood/FableCharacterSummaryTests.cpp',
    'Source/Fablewood/FableOnlineValidationDriver.h','Source/Fablewood/FableOnlineValidationDriver.cpp',
    'Tools/Play-Account.ps1','Play-Account.cmd','Tools/Test-OnlineFlow.ps1')
foreach ($name in @('SW_FableHit','SW_FableSkill','SW_FableShield','SW_FableAdvance')) {
    $required+='ArtSource/Audio/'+$name+'.wav';$required+='Content/Art/Audio/'+$name+'.uasset'
}
$required+='Content/Art/Audio/ATT_FableCombat.uasset'
foreach ($relative in $required) { if ($relative -notin $paths) { throw "Required source or runtime asset was omitted: $relative" } }
$meshSources=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'ArtSource\Meshes') -Filter '*.obj' -File | Where-Object BaseName -ne 'SM_ImportProbe')
if ($meshSources.Count -lt 68) { throw 'Expected at least 68 original mesh OBJ files.' }
foreach ($mesh in $meshSources) {
    $relative=Get-RelativeSourcePath $mesh.FullName
    if ($relative -notin $paths -or ('Content/Art/Meshes/'+$mesh.BaseName+'.uasset') -notin $paths) { throw "Mesh source/runtime pair was omitted: $relative" }
}
$allSource=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source') -Recurse -File)
foreach ($file in $allSource) {
    if ((Get-RelativeSourcePath $file.FullName) -notin $paths) { throw 'A file under Source was omitted; review it before accepting the manifest.' }
}
# Match only this task's known private secrets; never print them or serialized credentials.
$knownSecrets=[Collections.Generic.List[string]]::new()
$credentialFile=Join-Path $projectRoot 'Saved\Backend\backend-credentials.json'
if (Test-Path -LiteralPath $credentialFile -PathType Leaf) {
    $privateData=Get-Content -LiteralPath $credentialFile -Raw | ConvertFrom-Json
    foreach ($name in @('databasePassword','serverKey')) {
        $property=$privateData.PSObject.Properties[$name]
        if ($null -ne $property -and [string]$property.Value -and ([string]$property.Value).Length -ge 16) { $knownSecrets.Add([string]$property.Value) }
    }
}
$keyFile=Join-Path $projectRoot 'Saved\Backend\server-key.txt'
if (Test-Path -LiteralPath $keyFile -PathType Leaf) {
    $privateKey=[IO.File]::ReadAllText($keyFile).Trim()
    if ($privateKey.Length -ge 16) { $knownSecrets.Add($privateKey) }
}
$textExtensions=@('.h','.hpp','.cpp','.c','.cs','.csproj','.props','.targets','.json','.sql','.yaml','.yml','.md','.txt','.ini','.config','.ps1','.py','.cmd','.bat','.sh','.sln','.slnx','.http','.uproject','.xml','.manifest','.obj','.mtl')
function Test-Placeholder([string]$Value) {
    return [string]::IsNullOrWhiteSpace($Value) -or $Value -match '^\$\{|^<|^CHANGE_ME|^GENERATED_|^YOUR_|^EXAMPLE|^null$'
}
$records=@()
foreach ($file in $selected) {
    $relative=Get-RelativeSourcePath $file.FullName
    $isText=$file.Extension.ToLowerInvariant() -in $textExtensions -or $file.Name -in @('.gitignore','.gitattributes','.env.example','.dockerignore','.editorconfig','Dockerfile')
    if ($isText) {
        $body=[IO.File]::ReadAllText($file.FullName)
        foreach ($secret in $knownSecrets) { if ($body.Contains($secret)) { throw "Live private credential found in source file: $relative (value suppressed)" } }
        if ($body -match '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----') { throw "Private key material found: $relative (value suppressed)" }
        if ($file.Extension -in @('.json','.ini','.yaml','.yml','.config') -or $file.Name -eq '.env.example') {
            $patterns=@('(?im)^\s*(?:SecurityToken|FABLE_SERVER_KEY|FABLE_PG_PASSWORD|POSTGRES_PASSWORD)\s*=\s*(?<value>[^\r\n#]+)',
                '(?im)["''](?:serverKey|SecurityToken|databasePassword|FABLE_SERVER_KEY|POSTGRES_PASSWORD)["'']\s*:\s*["''](?<value>[^"''\r\n]+)',
                '(?i)(?:Password|Pwd)\s*=\s*(?<value>[^;"''\s]+)')
            foreach ($pattern in $patterns) {
                foreach ($match in [regex]::Matches($body,$pattern)) {
                    if (-not (Test-Placeholder ($match.Groups['value'].Value.Trim().Trim('"',"'")))) { throw "Unexpected literal secret setting in $relative (value suppressed)" }
                }
            }
        }
    }
    $records+=[ordered]@{path=$relative;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
if (-not $OutputPath) {
    $id=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $OutputPath=Join-Path $projectRoot ('Saved\SourceManifest\'+$id+'.json')
}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Manifest output already exists and was preserved.' }
foreach ($record in $records) {
    $file=Join-Path $projectRoot $record.path
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256) { throw 'Source changed while collecting the manifest; no manifest was written.' }
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null
$manifest=[ordered]@{status='ready';kind='source-and-runtime-assets';projectKind='unreal-game';createdUtc=[DateTime]::UtcNow.ToString('o');projectRoot=$projectRoot;fileCount=$records.Count;totalBytes=($selected | Measure-Object Length -Sum).Sum;sourceFileCount=$allSource.Count;meshSourceCount=$meshSources.Count;files=$records;excluded=$excluded;excludedRoots=@('Binaries','Intermediate','Saved','Builds','DerivedDataCache','.vs','ArtDirection/Previews','Backend');assertions=[ordered]@{allSourceFilesIncluded=$true;runtimeAssetsIncluded=$true;allMeshOriginalsIncluded=$true;liveSecretsAbsent=$true;knownPrivateSecretCount=$knownSecrets.Count};limitations=@('This is an inventory and secret check, not a build or gameplay test.','Binary source assets are retained; generated executables, reports, saves and DB volumes are excluded.','Historical regression fixtures remain separate from this source manifest.','The sibling .NET backend has its own source manifest and SDK pin.','Known-secret matching only inspects optional private files under this game root; zero known secrets is not a general proof that no secrets exist.')}
$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Depth 8)+[Environment]::NewLine)
$stream=[IO.File]::Open($OutputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush() } finally { $stream.Dispose() }
Write-Output ([pscustomobject]@{status='ready';manifest=$OutputPath;fileCount=$records.Count;sourceFileCount=$allSource.Count;meshSourceCount=$meshSources.Count})
