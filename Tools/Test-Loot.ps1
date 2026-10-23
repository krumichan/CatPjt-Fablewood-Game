param(
    [string]$EngineRoot,
    [string]$PackagedExecutable,
    [string]$SourceSummary,
    [switch]$Workshop,
    [ValidateRange(1024,65535)][int]$Port = 7796,
    [ValidateRange(180,3600)][int]$TimeoutSeconds = 1800,
    [ValidateRange(900,1920)][int]$Width = 900,
    [ValidateRange(600,1080)][int]$Height = 600
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($Workshop) { . (Join-Path $PSScriptRoot 'Workshop-Validation.ps1') }

function Read-LootJson([string]$Path) {
    # ReadAllText detects UTF-16 Unreal reports and UTF-8 PowerShell summaries.
    return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json)
}
function Get-LootHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
function Get-LootSaveDirectory([string]$Executable) {
    if ($Executable -match '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
        $gameRoot = Split-Path (Split-Path (Split-Path $Executable -Parent) -Parent) -Parent
        return Join-Path $gameRoot 'Saved\SaveGames'
    }
    return Join-Path $script:FableProjectRoot 'Saved\SaveGames'
}
function Assert-LootTrue($Value, [string]$Description) {
    if ($Value -isnot [bool] -or -not $Value) { throw $Description }
}
function Assert-LootScreenshots($Report, [string]$PhaseDirectory, [int]$Minimum) {
    $shots = @($Report.screenshots)
    if ($shots.Count -lt $Minimum -or @($shots | Select-Object -Unique).Count -lt $Minimum) {
        throw "Too few distinct gameplay screenshots for $($Report.role)/$($Report.phase)."
    }
    $allowed = [IO.Path]::GetFullPath($PhaseDirectory).TrimEnd('\') + '\'
    foreach ($shot in $shots) {
        $full = [IO.Path]::GetFullPath([string]$shot)
        if (-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetExtension($full) -ine '.png' -or
            -not (Test-Path -LiteralPath $full -PathType Leaf) -or
            (Get-Item -LiteralPath $full).Length -le 8) {
            throw "Missing or out-of-phase PNG: $full"
        }
        $stream = [IO.File]::OpenRead($full)
        try {
            $header = New-Object byte[] 8
            [void]$stream.Read($header,0,8)
            if (($header -join ',') -cne '137,80,78,71,13,10,26,10') {
                throw "Screenshot is not a PNG: $full"
            }
        } finally { $stream.Dispose() }
    }
}
function Assert-LootInventory($Report) {
    $inventory = @($Report.loot_inventory)
    if ($inventory.Count -le 0 -or $inventory.Count -ne [int]$Report.inventory_count) {
        throw 'Detailed loot inventory count does not match the profile.'
    }
    $ids = @{}
    foreach ($item in $inventory) {
        foreach ($field in @('id','slot','class','rarity','power','upgrade','roll_version','quality',
                              'nominal_power','roll_percent','affixes','set_id','signature','durability_version','durability','max_durability')) {
            if ($null -eq $item.PSObject.Properties[$field]) { throw "Missing item field: $field" }
        }
        $id = [int]$item.id
        if ($id -le 0 -or $ids.ContainsKey($id) -or [string]::IsNullOrWhiteSpace([string]$item.signature)) {
            throw 'Invalid, duplicate or unsigned loot item.'
        }
        $ids[$id] = $item
        foreach ($affix in @($item.affixes)) {
            if ($null -eq $affix -or $null -eq $affix.PSObject.Properties['stat'] -or
                $null -eq $affix.PSObject.Properties['value']) { throw 'Incomplete affix fields.' }
        }
    }
    return $ids
}
function Assert-LootReport($Report, [string]$Role, [string]$Phase, [int]$ProcessId,
                           [string]$PhaseDirectory) {
    $expectedMode = if ($Role -eq 'host') { 'listen_server' } else { 'client' }
    if ($Report.status -cne 'passed' -or $Report.run_id -cne $runId -or $Report.role -cne $Role -or
        $Report.phase -cne $Phase -or $Report.process_id -ne $ProcessId -or
        $Report.network_mode -cne $expectedMode -or $Report.player_count -ne 2) {
        throw "Loot report identity, phase, PID or network mismatch: $Role/$Phase"
    }
    Assert-LootTrue $Report.loot_enabled 'Loot scenario flag is missing.'
    Assert-LootTrue $Report.save_succeeded 'Profile save was not confirmed.'
    $expectedSchema = if ($Role -eq 'host') { 8 } else { 0 }
    if ($Report.saved_schema_version -ne $expectedSchema) { throw 'Saved schema observation mismatch.' }
    if ($Role -eq 'host' -and $Report.profile_slot -cne ('Fablewood_RPG_Test_'+$runId+'_Host')) {
        throw 'Host saved a different profile slot.'
    }
    $slotProperty=$Report.PSObject.Properties['profile_slot']
    if ($Role -eq 'guest' -and $null -ne $slotProperty -and
        -not [string]::IsNullOrEmpty([string]$slotProperty.Value)) {
        throw 'Guest unexpectedly reported a local authoritative save slot.'
    }
    $sourceRole = $sourceReport.phases.resume.$Role
    if ($Report.class -ne $sourceRole.class -or $Report.forest_quest -ne 3 -or
        $Report.dungeon_quest -ne 3 -or $Report.moonwell_quest -ne 3) {
        throw 'Expected original class and completed quests were not retained.'
    }
    if ($Report.moon_set_count -ne 2 -or $Report.root_set_count -ne 0) {
        throw 'The final equipped set is not the full Moonlight pair.'
    }
    $inventoryById = Assert-LootInventory $Report
    if ([string]::IsNullOrWhiteSpace([string]$Report.loot_signature)) { throw 'Full loot signature is missing.' }
    if ($Phase -eq 'fresh') {
        if ($Report.loot_cycle -ne 4 -or
            $Report.initial_root_completions -ne $sourceRole.dungeon_completions -or
            $Report.initial_moon_completions -ne $sourceRole.moonwell_completions -or
            $Report.dungeon_completions -ne ([int]$sourceRole.dungeon_completions+2) -or
            $Report.moonwell_completions -ne ([int]$sourceRole.moonwell_completions+2)) {
            throw 'Four real dungeon cycles did not add exactly two completions per region.'
        }
        if ($Report.legacy_signature -cne $sourceRole.profile_signature -or
            $Report.legacy_attack -ne $sourceRole.attack_power -or
            $Report.legacy_defense -ne $sourceRole.defense) {
            throw 'The copied legacy profile changed during migration.'
        }
        foreach ($check in @('legacy_migrated','normal_enemy_stats','hard_enemy_stats','hard_wipe_seen',
                'hard_retry_fixed','boss_reward_once','root_set_equipped','moon_set_equipped',
                'equipment_preview_matches','rolls_preserved_after_enhance','inventory_sort_valid',
                'shop_roll_received','shop_sale_checked','remote_mutation_rejected')) {
            Assert-LootTrue $Report.loot_checks.$check ("Missing loot checkpoint: "+$Role+'/'+$check)
        }
        $bossIds = @($Report.loot_boss_ids)
        if ($bossIds.Count -ne 4 -or @($bossIds | Select-Object -Unique).Count -ne 4) {
            throw 'Four distinct personal boss item IDs were not observed.'
        }
        $setSlots = @{1=@();2=@()}
        for ($index=0; $index -lt 4; ++$index) {
            $id = [int]$bossIds[$index]
            if (-not $inventoryById.ContainsKey($id)) { throw 'A reported boss item is missing from inventory.' }
            $item = $inventoryById[$id]
            $expectedSet = if ($index -lt 2) { 1 } else { 2 }
            if ($item.class -ne $Report.class -or $item.set_id -ne $expectedSet -or
                $item.roll_version -lt 1 -or $item.quality -lt 1 -or $item.quality -gt 4) {
                throw 'Boss loot is not a randomized, class-specific regional set piece.'
            }
            $setSlots[$expectedSet] += [int]$item.slot
        }
        foreach ($setId in @(1,2)) {
            if ((@($setSlots[$setId] | Sort-Object) -join ',') -cne '0,1') {
                throw 'Two completions did not produce both equipment slots of the regional set.'
            }
        }
        Assert-LootScreenshots $Report $PhaseDirectory 6
    } else {
        Assert-LootTrue $Report.loot_checks.runtime_not_persisted 'Encounter runtime leaked into a restored profile.'
        $fresh = $phaseResults['fresh'][$Role]
        if ($Report.loot_signature -cne $fresh.loot_signature -or
            $Report.dungeon_completions -ne $fresh.dungeon_completions -or
            $Report.moonwell_completions -ne $fresh.moonwell_completions) {
            throw 'Persistent loot/profile signature changed after restart.'
        }
        # Compare all item fields, affixes and per-item signatures, not just derived attack.
        $freshJson = ConvertTo-Json -InputObject @($fresh.loot_inventory) -Depth 20 -Compress
        $resumeJson = ConvertTo-Json -InputObject @($Report.loot_inventory) -Depth 20 -Compress
        if ($resumeJson -cne $freshJson) { throw 'Saved inventory rolls or item signatures changed on reload.' }
        Assert-LootScreenshots $Report $PhaseDirectory 1
    }
    if ($Workshop) { Assert-WorkshopReport $Report $Role $Phase }
}
function Invoke-LootPhase([string]$Phase, [string]$PhaseDirectory) {
    [IO.Directory]::CreateDirectory($PhaseDirectory) | Out-Null
    $hostProcess=$null; $guestProcess=$null; $reports=@{}
    $roles=@('host','guest')
    try {
        if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) {
            throw "Port $Port is already in use; existing processes were not changed."
        }
        $common=' -game -FableLoot -windowed -ResX='+$Width+' -ResY='+$Height+
            ' -nosplash -unattended -FableRPGValidationRun='+$runId+
            ' -FableRPGValidationPhase='+$Phase+' -FableRPGValidationDirectory="'+$PhaseDirectory+
            '" -ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"'
        if ($Workshop) { $common += ' -FableWorkshop' }
        $hostLog=Join-Path $PhaseDirectory 'host.log'
        $guestLog=Join-Path $PhaseDirectory 'guest.log'
        $hostArguments=$projectArg+'/Game/Maps/ForestClearing?listen -port='+$Port+
            ' -MULTIHOME=127.0.0.1 -WinX=20 -WinY=40 -FableRPGValidationRole=host -abslog="'+$hostLog+'"'+$common
        $hostProcess=Start-Process -FilePath $exe -ArgumentList $hostArguments -WindowStyle Hidden -PassThru
        $ready=$false; $deadline=[DateTime]::UtcNow.AddSeconds(240)
        while ([DateTime]::UtcNow -lt $deadline) {
            $hostProcess.Refresh()
            if ($hostProcess.HasExited) { throw "Host exited before listening: $hostLog" }
            if (@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue |
                    Where-Object OwningProcess -eq $hostProcess.Id).Count -gt 0) { $ready=$true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $ready) { throw 'Loot host did not open its loopback endpoint.' }
        $guestArguments=$projectArg+'127.0.0.1:'+$Port+
            ' -WinX=700 -WinY=80 -FableRPGValidationRole=guest -abslog="'+$guestLog+'"'+$common
        $guestProcess=Start-Process -FilePath $exe -ArgumentList $guestArguments -WindowStyle Hidden -PassThru
        Write-Host "Loot $Phase started: $PhaseDirectory"
        $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            foreach ($role in $roles) {
                $path=Join-Path $PhaseDirectory ($role+'.json')
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
                try { $report=Read-LootJson $path } catch { continue }
                if ($report.status -in @('passed','failed')) { $reports[$role]=$report }
            }
            if (@($reports.Values | Where-Object status -eq 'failed').Count -gt 0) {
                throw ('Loot scenario failure: '+(($reports.Values | ForEach-Object reason)-join '; '))
            }
            if ($reports.Count -eq $roles.Count) { break }
            $hostProcess.Refresh(); $guestProcess.Refresh()
            if ($hostProcess.HasExited -or $guestProcess.HasExited) {
                throw 'Game process exited before both loot reports completed.'
            }
            Start-Sleep -Milliseconds 500
        }
        # Preserve received reports even if an assertion fails.
        $phaseResults[$Phase]=$reports
        if ($reports.Count -ne $roles.Count) { throw 'Loot validation timed out.' }
        foreach ($role in $roles) {
            $process=if ($role -eq 'host') { $hostProcess } else { $guestProcess }
            Assert-LootReport $reports[$role] $role $Phase $process.Id $PhaseDirectory
        }
        foreach ($role in @('Host','Guest')) {
            $savePath=Join-Path $targetSaves ('Fablewood_RPG_Test_'+$runId+'_'+$role+'.sav')
            if (-not (Test-Path -LiteralPath $savePath -PathType Leaf) -or
                (Get-Item -LiteralPath $savePath).Length -le 0) { throw "Expected saved test profile missing: $savePath" }
        }
        Write-Host "Loot $Phase passed."
    } finally {
        $phaseResults[$Phase]=$reports
        # Cleanup is restricted to the two process handles owned by this invocation.
        foreach ($process in @($guestProcess,$hostProcess)) {
            if ($null -eq $process) { continue }
            $process.Refresh()
            if (-not $process.HasExited) {
                [void]$process.CloseMainWindow()
                if (-not $process.WaitForExit(10000)) { $process.Kill(); [void]$process.WaitForExit(5000) }
            }
        }
    }
}

if ($PackagedExecutable) {
    $exe=(Resolve-Path -LiteralPath $PackagedExecutable).Path
    if ($exe -notmatch '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') {
        $inner=Join-Path (Split-Path -Parent $exe) 'Fablewood\Binaries\Win64\Fablewood.exe'
        if (-not (Test-Path -LiteralPath $inner -PathType Leaf)) { throw 'Supply the packaged game executable or its archive bootstrap.' }
        $exe=(Resolve-Path -LiteralPath $inner).Path
    }
    $projectArg=''; $mode='packaged-development'
} else {
    $engine=Resolve-FableEngine -EngineRoot $EngineRoot; Assert-FableReady
    $exe=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
    $projectArg='"'+$script:FableProjectFile+'" '; $mode='editor-game'
}
$executableHashBefore=Get-LootHash $exe
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$validationFolder=if($Workshop){'WorkshopValidation'}else{'LootValidation'}
$directory=Join-Path $script:FableProjectRoot ('Saved\'+$validationFolder+'\'+$runId)
if (Test-Path -LiteralPath $directory) { throw 'Unique validation directory already exists and was preserved.' }
[IO.Directory]::CreateDirectory($directory) | Out-Null
$targetSaves=Get-LootSaveDirectory $exe
$phaseResults=[ordered]@{}
$originals=[ordered]@{}
$fixtureCopies=@()
$originalSnapshotComplete=$false
$sourceReport=$null
$sourceSaves=$null
$failure=$null
$started=[DateTime]::UtcNow
try {
    if (-not $SourceSummary) {
        $completionPath=if($Workshop){Join-Path $script:FableProjectRoot 'Saved\LootValidation\completion.json'}else{Join-Path $script:FableProjectRoot 'Saved\CoopRecoveryValidation\completion.json'}
        $completion=Read-LootJson $completionPath
        if ($completion.status -ne 'completed') { throw 'A completed baseline is required.' }
        $SourceSummary=if($Workshop){[string]$completion.validation.loot}else{[string]$completion.validation.adventureMageArcher}
        $originals[$completionPath]=Get-LootHash $completionPath
    }
    $SourceSummary=(Resolve-Path -LiteralPath $SourceSummary).Path
    $sourceReport=Read-LootJson $SourceSummary
    if ($sourceReport.status -ne 'passed' -or $sourceReport.runId -notmatch '^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$') {
        throw 'Fixtures must come from a completed, identifiable real adventure.'
    }
    $originals[$SourceSummary]=Get-LootHash $SourceSummary
    $saveProperty=$sourceReport.PSObject.Properties['profileSaveFolder']
    $sourceSaves=if ($null -ne $saveProperty -and -not [string]::IsNullOrWhiteSpace([string]$saveProperty.Value)) {
        [IO.Path]::GetFullPath([string]$saveProperty.Value)
    } else { Get-LootSaveDirectory ([string]$sourceReport.executable) }
    [IO.Directory]::CreateDirectory($targetSaves) | Out-Null
    # Preserve every non-test profile in source, target and editor save folders.
    $saveFolders=@($sourceSaves,$targetSaves,(Join-Path $script:FableProjectRoot 'Saved\SaveGames') |
        Select-Object -Unique)
    foreach ($folder in $saveFolders) {
        if (-not (Test-Path -LiteralPath $folder -PathType Container)) { continue }
        foreach ($file in @(Get-ChildItem -LiteralPath $folder -Filter '*.sav' -File |
                Where-Object {$_.Name -notmatch '_Test_|_FrontTest_|ProfileIndex_Test_'})) {
            $originals[$file.FullName]=Get-LootHash $file.FullName
        }
    }
    $fixtureSources=@{}
    foreach ($role in @('host','guest')) {
        $old=$sourceReport.phases.resume.$role
        if ($old.status -ne 'passed' -or $old.role -cne $role -or $old.phase -cne 'resume' -or
            $old.run_id -cne $sourceReport.runId -or $old.player_count -ne 2 -or
            $old.dungeon_quest -ne 3 -or $old.moonwell_quest -ne 3 -or
            $old.dungeon_completions -lt 1 -or $old.moonwell_completions -lt 1) {
            throw 'Source role did not complete the required saved Root Ruins/Moonwell adventure.'
        }
        $suffix=if ($role -eq 'host') { 'Host' } else { 'Guest' }
        $source=Join-Path $sourceSaves ('Fablewood_RPG_Test_'+$sourceReport.runId+'_'+$suffix+'.sav')
        $originals[$source]=Get-LootHash $source
        $fixtureSources[$role]=$source
    }
    $originalSnapshotComplete=$true
    foreach ($role in @('host','guest')) {
        $suffix=if ($role -eq 'host') { 'Host' } else { 'Guest' }
        $source=$fixtureSources[$role]
        $target=Join-Path $targetSaves ('Fablewood_RPG_Test_'+$runId+'_'+$suffix+'.sav')
        if (Test-Path -LiteralPath $target) { throw 'Unique test save slot already exists and was preserved.' }
        [IO.File]::Copy($source,$target,$false)
        $copiedHash=Get-LootHash $target
        if ($copiedHash -cne $originals[$source]) { throw 'Fixture byte-copy SHA-256 mismatch.' }
        $fixtureCopies += [ordered]@{
            role=$role; source=$source; target=$target; sourceSHA256=$originals[$source]
            targetSHA256Before=$copiedHash; byteExact=$true
        }
    }
    foreach ($phase in @('fresh','resume')) {
        Invoke-LootPhase $phase (Join-Path $directory $phase)
    }
} catch { $failure=$_.Exception.Message }

$preservation=@()
$allPreserved=$originalSnapshotComplete
foreach ($entry in $originals.GetEnumerator()) {
    $after=$null
    if (Test-Path -LiteralPath $entry.Key -PathType Leaf) {
        try { $after=Get-LootHash $entry.Key } catch { $after=$null }
    }
    $same=$null -ne $after -and $entry.Value -ceq $after
    $allPreserved=$allPreserved -and $same
    $preservation += [ordered]@{path=$entry.Key;beforeSHA256=$entry.Value;afterSHA256=$after;preserved=$same}
}
if (-not $allPreserved) { $failure=(@($failure,'Original fixture/profile preservation was not confirmed.') | Where-Object {$_}) -join ' ' }
$executableHashAfter=$null
try { $executableHashAfter=Get-LootHash $exe } catch {}
$binaryPreserved=$null -ne $executableHashAfter -and $executableHashAfter -ceq $executableHashBefore
if (-not $binaryPreserved) { $failure=(@($failure,'Executable SHA-256 changed or became unavailable.') | Where-Object {$_}) -join ' ' }
$status=if ($failure) { 'failed' } else { 'passed' }
$sourceRunId=if ($null -ne $sourceReport) { $sourceReport.runId } else { $null }
[ordered]@{
    runId=$runId;status=$status;reason=$failure;mode=$mode;width=$Width;height=$Height;port=$Port
    startedUtc=$started.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o')
    executable=$exe;executableSHA256Before=$executableHashBefore;executableSHA256After=$executableHashAfter
    executablePreserved=$binaryPreserved;sourceSummary=$SourceSummary;sourceRunId=$sourceRunId
    sourceProfileSaveFolder=$sourceSaves;profileSaveFolder=$targetSaves
    fixtureCopies=$fixtureCopies;originalFilesPreserved=$allPreserved;originalFiles=$preservation
    networkScope='Two owned game processes on one PC using IPv4 loopback; no physical two-PC or WAN claim'
    saveScope='Byte-copied legacy fixtures in unique test slots; source and existing non-test saves retained'
    gameplayScope='Four real dungeon cycles, optionally followed by normal-input workshop operations; no HP, reward or quest injection'
    workshopEnabled=[bool]$Workshop
    phases=$phaseResults
} | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $directory 'summary.json') -Encoding UTF8
Write-Host "Loot report: $directory\summary.json"
if ($failure) { throw $failure }
Write-Host 'Loot migration, normal/hard dungeons, personal sets, item rolls and reload checks passed.'
if($Workshop){Write-Host 'Workshop crafting, salvage, reroll, durability repair, storage and v8 reload checks passed.'}
