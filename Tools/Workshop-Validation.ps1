# Dot-sourced by Test-Loot.ps1 only for -Workshop. Windows PowerShell 5.1 compatible.
# The caller owns process/file identity and screenshots; this helper checks reported data.
# Dynamic caller scope supplies $sourceReport and $phaseResults. No files or saves are changed.

function Get-WorkshopField($Object, [string]$Name) {
    if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Name]) {
        throw "Workshop report is missing field '$Name'."
    }
    return ,($Object.PSObject.Properties[$Name].Value)
}
function Assert-WorkshopTrue($Value, [string]$Label) {
    if ($Value -isnot [bool] -or -not $Value) { throw "Workshop checkpoint failed: $Label" }
}
function Assert-WorkshopInteger($Value, [long]$Min, [long]$Max, [string]$Label) {
    if ($null -eq $Value -or $Value -is [bool] -or $Value -is [string] -or
        $Value -isnot [ValueType]) { throw "Workshop numeric field is invalid: $Label" }
    $number = [double]$Value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or
        $number -ne [Math]::Truncate($number) -or $number -lt $Min -or $number -gt $Max) {
        throw "Workshop integer is out of range: $Label = $Value"
    }
}
function Get-WorkshopItemSignature($Item, [switch]$Legacy) {
    $result = '{0},{1},{2},{3},{4},{5},r{6},q{7},n{8},p{9},s{10},a{11}' -f
        $Item.id,$Item.slot,$Item.class,$Item.rarity,$Item.power,$Item.upgrade,
        $Item.roll_version,$Item.quality,$Item.nominal_power,$Item.roll_percent,$Item.set_id,@($Item.affixes).Count
    foreach ($affix in @($Item.affixes)) { $result += ',{0}:{1}' -f $affix.stat,$affix.value }
    if (-not $Legacy) { $result += ',dv{0},d{1},md{2}' -f $Item.durability_version,$Item.durability,$Item.max_durability }
    return $result
}
function Assert-WorkshopItem($Item, [string]$Label, [switch]$Legacy) {
    $limits = @{
        id=@(1,999999); slot=@(0,1); class=@(0,2); rarity=@(0,4); power=@(0,104);
        upgrade=@(0,5); roll_version=@(0,2); quality=@(0,4); nominal_power=@(0,100);
        roll_percent=@(80,140); set_id=@(0,2)
    }
    foreach ($field in $limits.Keys) {
        Assert-WorkshopInteger (Get-WorkshopField $Item $field) $limits[$field][0] $limits[$field][1] ($Label+'/'+$field)
    }
    $affixes = @(Get-WorkshopField $Item 'affixes')
    # Get-WorkshopField preserves an empty array; assignment flattens it exactly once.
    $affixes = @($Item.affixes)
    $seenStats = @{}
    foreach ($affix in $affixes) {
        Assert-WorkshopInteger (Get-WorkshopField $affix 'stat') 0 3 ($Label+'/affix.stat')
        $stat=[int]$affix.stat
        $mins=@(2,1,5,5); $maxs=@(6,4,20,15)
        Assert-WorkshopInteger (Get-WorkshopField $affix 'value') $mins[$stat] $maxs[$stat] ($Label+'/affix.value')
        if ($seenStats.ContainsKey($stat)) { throw "Duplicate workshop affix stat: $Label" }
        $seenStats[$stat]=$true
    }
    if ($Item.roll_version -eq 0) {
        if ($Item.quality -ne 0 -or $Item.nominal_power -ne 0 -or $Item.roll_percent -ne 100 -or
            $Item.set_id -ne 0 -or $affixes.Count -ne 0) { throw "Fixed item was randomized: $Label" }
    } else {
        $quality=[int]$Item.quality; $grade=[int]$Item.rarity
        if ($quality -lt 1 -or $Item.id -le 4 -or $grade -lt 1 -or
            ($grade -eq 4 -and ($Item.roll_version -ne 2 -or $Item.set_id -ne 0)) -or
            ($grade -lt 4 -and $Item.roll_version -ne 1)) { throw "Invalid randomized item version: $Label" }
        $lo=@(0,80,95,110,125); $hi=@(0,94,109,124,140); $counts=@(0,1,2,2,3)
        Assert-WorkshopInteger $Item.roll_percent $lo[$quality] $hi[$quality] ($Label+'/quality range')
        $power=[int][Math]::Floor(([double]$Item.nominal_power*[double]$Item.roll_percent/100.0)+0.5)
        if ($Item.power -ne $power -or $affixes.Count -ne $counts[$quality]) { throw "Invalid roll power or affix count: $Label" }
        $nominals=if ($Item.slot -eq 0) { @(0,30,48,58,74) } else { @(0,6,12,16,22) }
        if ($Item.nominal_power -ne $nominals[$grade] -and
            -not ($grade -eq 1 -and $Item.slot -eq 0 -and $Item.nominal_power -eq 32)) {
            throw "Invalid nominal power for grade/slot: $Label"
        }
        if (($Item.set_id -eq 1 -and $grade -ne 2) -or ($Item.set_id -eq 2 -and $grade -ne 3)) {
            throw "Invalid regional set grade: $Label"
        }
    }
    if (-not $Legacy) {
        Assert-WorkshopInteger (Get-WorkshopField $Item 'durability_version') 0 1 ($Label+'/durability version')
        $max=if ($Item.durability_version -eq 0) { 100 } else { @(60,80,100,120,150)[[int]$Item.rarity] }
        Assert-WorkshopInteger (Get-WorkshopField $Item 'max_durability') $max $max ($Label+'/maximum durability')
        Assert-WorkshopInteger (Get-WorkshopField $Item 'durability') 0 $max ($Label+'/durability')
    }
    if ((Get-WorkshopField $Item 'signature') -cne (Get-WorkshopItemSignature $Item -Legacy:$Legacy)) {
        throw "Item signature does not match its actual fields: $Label"
    }
}
function Assert-WorkshopBalance($Balance, [string]$Label, [switch]$Cost) {
    foreach ($field in @('gold','ore','essence','root_cores','moon_crystals')) {
        $min=if ($Cost) { -1000000 } else { 0 }
        $max=if ($field -eq 'gold') { 1000000 } else { 100000 }
        Assert-WorkshopInteger (Get-WorkshopField $Balance $field) $min $max ($Label+'/'+$field)
    }
}
function Assert-WorkshopSameBalance($Left, $Right, [string]$Label) {
    foreach ($field in @('gold','ore','essence','root_cores','moon_crystals')) {
        if ((Get-WorkshopField $Left $field) -ne (Get-WorkshopField $Right $field)) {
            throw "Workshop resource mismatch: $Label/$field"
        }
    }
}
function Assert-WorkshopSameItem($Before, $After, [string]$Label, [string[]]$Except=@()) {
    foreach ($field in @('id','slot','class','rarity','power','upgrade','roll_version','quality',
        'nominal_power','roll_percent','set_id','durability_version','durability','max_durability')) {
        if ($Except -contains $field) { continue }
        if ((Get-WorkshopField $Before $field) -ne (Get-WorkshopField $After $field)) {
            throw "Unrelated item field changed: $Label/$field"
        }
    }
    if ($Except -notcontains 'affixes') {
        $a=ConvertTo-Json -InputObject @($Before.affixes) -Depth 5 -Compress
        $b=ConvertTo-Json -InputObject @($After.affixes) -Depth 5 -Compress
        if ($a -cne $b) { throw "Unrelated item options changed: $Label" }
    }
}
function Assert-WorkshopReport($Report, [string]$Role, [string]$Phase) {
    if ($Role -cnotin @('host','guest') -or $Phase -cnotin @('fresh','resume')) { throw 'Unknown workshop role or phase.' }
    $label=$Role+'/'+$Phase
    Assert-WorkshopTrue (Get-WorkshopField $Report 'workshop_enabled') ($label+'/enabled')
    $checks=Get-WorkshopField $Report 'workshop_checks'
    $balance=Get-WorkshopField $Report 'materials'
    Assert-WorkshopBalance $balance $label
    if ($balance.gold -ne $Report.gold -or $balance.ore -ne $Report.ore) { throw "Material and profile balances disagree: $label" }
    if ([string]::IsNullOrWhiteSpace([string](Get-WorkshopField $Report 'workshop_signature')) -or
        $Report.workshop_signature -cne $Report.loot_signature) { throw "Missing or conflicting workshop profile signature: $label" }
    $items=@{}; $inv=@{}; $stash=@(Get-WorkshopField $Report 'stash_items'); $stash=@($Report.stash_items)
    if ($stash.Count -ne 1 -or @($Report.loot_inventory).Count -gt 32) { throw "Unexpected storage or inventory count: $label" }
    foreach ($item in @($Report.loot_inventory)) {
        Assert-WorkshopItem $item ($label+'/inventory')
        $id=[int]$item.id
        if ($items.ContainsKey($id)) { throw "Duplicate inventory ID: $id" }
        $items[$id]=$item; $inv[$id]=$item
    }
    foreach ($item in $stash) {
        Assert-WorkshopItem $item ($label+'/stash')
        $id=[int]$item.id
        if ($items.ContainsKey($id)) { throw "Item exists in both inventory and storage: $id" }
        $items[$id]=$item
    }
    $legend=Get-WorkshopField $Report 'workshop_legendary_id'; $stored=Get-WorkshopField $Report 'workshop_stored_id'
    Assert-WorkshopInteger $legend 5 999999 ($label+'/legendary ID')
    Assert-WorkshopInteger $stored 5 999999 ($label+'/stored ID')
    if ($legend -eq $stored -or -not $inv.ContainsKey([int]$legend) -or $stash[0].id -ne $stored -or
        $items[[int]$legend].rarity -ne 4 -or $items[[int]$legend].upgrade -ne 1 -or $stash[0].rarity -ne 3) {
        throw "Final legendary or stored equipment identity is wrong: $label"
    }
    if ($balance.root_cores -ne 1 -or $balance.moon_crystals -ne 0) { throw "Boss materials were not spent exactly: $label" }
    if ($Phase -eq 'resume') {
        Assert-WorkshopTrue (Get-WorkshopField $checks 'runtime_not_persisted') ($label+'/runtime cleared')
        $fresh=$phaseResults['fresh'][$Role]
        if ($null -eq $fresh -or $fresh.status -cne 'passed') { throw 'Missing successful fresh workshop report.' }
        foreach ($field in @('workshop_signature','workshop_legendary_id','workshop_stored_id')) {
            if ((Get-WorkshopField $Report $field) -cne (Get-WorkshopField $fresh $field)) { throw "Workshop restart changed $field" }
        }
        Assert-WorkshopSameBalance $balance $fresh.materials ($label+'/reload')
        foreach ($field in @('loot_inventory','stash_items')) {
            $before=ConvertTo-Json -InputObject @($fresh.$field) -Depth 20 -Compress
            $after=ConvertTo-Json -InputObject @($Report.$field) -Depth 20 -Compress
            if ($before -cne $after) { throw "Workshop restart changed complete $field data." }
        }
        return
    }
    Assert-WorkshopTrue (Get-WorkshopField $Report 'workshop_started') ($label+'/started')
    foreach ($check in @('v5_migrated','actual_weapon_wear','actual_armor_wear','remote_repair_rejected',
        'repair_preserves_item','full_repair_rejected','insufficient_materials_rejected',
        'dismantle_confirmation','dismantle_yield','all_grades_crafted','grade_1_crafted','grade_2_crafted',
        'grade_3_crafted','grade_4_crafted','selected_option_only','protected_actions_rejected',
        'deposit_identity_preserved','duplicate_deposit_rejected','withdraw_identity_preserved',
        'legendary_enhancement_preserved','boss_materials_spent_exactly','unique_storage_ids')) {
        Assert-WorkshopTrue (Get-WorkshopField $checks $check) ($label+'/'+$check)
    }
    # Compare the actual v5 source items, not only the driver's migration flag or signature.
    if ($null -eq $sourceReport -or $sourceReport.status -cne 'passed' -or
        $sourceReport.phases.resume.host.saved_schema_version -ne 5) { throw 'Workshop migration requires the passed actual v5 source summary.' }
    $source=$sourceReport.phases.resume.$Role
    $initial=@(Get-WorkshopField $Report 'initial_v5_inventory'); $initial=@($Report.initial_v5_inventory)
    $sourceItems=@(Get-WorkshopField $source 'loot_inventory'); $sourceItems=@($source.loot_inventory)
    if ($initial.Count -eq 0 -or $initial.Count -ne $sourceItems.Count) { throw 'Migration changed the actual source inventory count.' }
    $originalById=@{}; $initialIds=@{}
    foreach ($item in $sourceItems) {
        Assert-WorkshopItem $item ($label+'/source v5') -Legacy
        $id=[int]$item.id
        if ($originalById.ContainsKey($id)) { throw 'The source v5 inventory has duplicate IDs.' }
        $originalById[$id]=$item
    }
    foreach ($item in $initial) {
        Assert-WorkshopItem $item ($label+'/migrated v5')
        $id=[int]$item.id
        if ($initialIds.ContainsKey($id) -or -not $originalById.ContainsKey($id) -or
            (Get-WorkshopItemSignature $item -Legacy) -cne $originalById[$id].signature -or
            $item.durability_version -ne 0 -or $item.durability -ne 100 -or $item.max_durability -ne 100) {
            throw "Migration rerolled, replaced, lost or damaged source item $id"
        }
        $initialIds[$id]=$true
    }
    $crafted=@(Get-WorkshopField $Report 'workshop_crafted_ids'); $crafted=@($Report.workshop_crafted_ids)
    if ($crafted.Count -ne 4 -or @($crafted | Select-Object -Unique).Count -ne 4) { throw 'Four distinct crafted IDs are required.' }
    $events=@(Get-WorkshopField $Report 'workshop_events'); $events=@($Report.workshop_events)
    $sequence=(@($events | ForEach-Object { $_.action }) -join ',')
    if ($sequence -cnotmatch '^repair,repair,(dismantle,)+craft,craft,craft,craft,reroll,deposit,withdraw,deposit_saved,enhance_legendary$') {
        throw "Missing, duplicated or out-of-order workshop events: $sequence"
    }
    $known=@{}; $deleted=@{}; $repairSlots=@(); $craftedIndex=0; $previousBalance=$null
    foreach ($event in $events) {
        $action=Get-WorkshopField $event 'action'; $id=Get-WorkshopField $event 'item_id'
        Assert-WorkshopInteger $id 5 999999 ($label+'/'+$action+'/ID'); $id=[int]$id
        $before=Get-WorkshopField $event 'before'; $after=Get-WorkshopField $event 'after'
        $recordedCost=Get-WorkshopField $event 'expected_cost'
        Assert-WorkshopBalance $before ($action+'/before'); Assert-WorkshopBalance $after ($action+'/after')
        Assert-WorkshopBalance $recordedCost ($action+'/cost') -Cost
        if ($null -ne $previousBalance) { Assert-WorkshopSameBalance $before $previousBalance ($action+'/continuous ledger') }
        elseif ($before.essence -ne 0 -or $before.root_cores -ne 3 -or $before.moon_crystals -ne 3) {
            throw 'Four real boss rewards must supply exactly 3 cores/3 crystals and no preexisting essence.'
        }
        $previousBalance=$after
        $old=$null; $new=$null
        if ($action -ne 'craft') {
            $old=Get-WorkshopField $event 'item_before_data'; Assert-WorkshopItem $old ($action+'/item before')
            if ($old.id -ne $id -or (Get-WorkshopField $event 'item_before') -cne $old.signature) { throw "Before item identity mismatch: $action" }
            if ($known.ContainsKey($id) -and $known[$id].signature -cne $old.signature) { throw "Item changed between workshop events: $id" }
            if ($deleted.ContainsKey($id)) { throw "A removed item was reused: $id" }
        } elseif (-not [string]::IsNullOrEmpty([string](Get-WorkshopField $event 'item_before'))) { throw 'Craft event unexpectedly has an original item.' }
        if ($action -ne 'dismantle') {
            $new=Get-WorkshopField $event 'item_after_data'; Assert-WorkshopItem $new ($action+'/item after')
            if ($new.id -ne $id -or (Get-WorkshopField $event 'item_after') -cne $new.signature) { throw "After item identity mismatch: $action" }
        } elseif (-not [string]::IsNullOrEmpty([string](Get-WorkshopField $event 'item_after')) -or
            $null -ne $event.PSObject.Properties['item_after_data']) { throw 'Dismantled item is still present in the event.' }
        $cost=[pscustomobject]@{gold=0;ore=0;essence=0;root_cores=0;moon_crystals=0}
        switch ($action) {
            'repair' {
                $missing=[int]$old.max_durability-[int]$old.durability
                if ($missing -le 0 -or $new.durability -ne $new.max_durability) { throw 'Repair did not restore an actually worn item.' }
                Assert-WorkshopSameItem $old $new 'repair' @('durability')
                $cost.gold=$missing*([int]$old.rarity+1); $cost.ore=[int][Math]::Ceiling($missing/25.0)
                $repairSlots += [int]$old.slot
            }
            'dismantle' {
                if (@($Report.loot_boss_ids) -contains $id -or $items.ContainsKey($id)) { throw "Protected boss loot or final item was dismantled: $id" }
                $yield=3*[int]$old.rarity+2*[int]$old.quality+2*[int]$old.upgrade
                if ($yield -le 0) { throw 'Dismantle event has no legitimate essence yield.' }
                $cost.essence=-$yield; $deleted[$id]=$true
            }
            'craft' {
                $grade=$craftedIndex+1; $offset=if ($Role -eq 'host') { 0 } else { 1 }; $slot=($grade+$offset)%2
                $nominal=if ($slot -eq 0) { @(0,30,48,58,74)[$grade] } else { @(0,6,12,16,22)[$grade] }
                if ($crafted[$craftedIndex] -ne $id -or $known.ContainsKey($id) -or $initialIds.ContainsKey($id) -or $deleted.ContainsKey($id) -or
                    $new.rarity -ne $grade -or $new.slot -ne $slot -or $new.class -ne $Report.class -or $new.set_id -ne 0 -or
                    $new.nominal_power -ne $nominal -or $new.upgrade -ne 0 -or $new.durability_version -ne 1 -or $new.durability -ne $new.max_durability) {
                    throw "Crafted grade $grade has incorrect identity, slot, class, roll or durability."
                }
                $cost.gold=@(0,80,150,240,500)[$grade]; $cost.ore=@(0,3,6,10,20)[$grade]
                $cost.essence=@(0,8,16,24,50)[$grade]; $cost.root_cores=@(0,0,1,0,1)[$grade]; $cost.moon_crystals=@(0,0,0,1,2)[$grade]
                ++$craftedIndex
            }
            'reroll' {
                if ($id -ne $legend -or @($old.affixes).Count -ne @($new.affixes).Count) { throw 'Reroll target/count is wrong.' }
                Assert-WorkshopSameItem $old $new 'reroll' @('affixes')
                $aa=@($old.affixes); $bb=@($new.affixes)
                if ($aa.Count -eq 0 -or ($aa[0].stat -eq $bb[0].stat -and $aa[0].value -eq $bb[0].value)) { throw 'Selected option did not change.' }
                for ($i=1;$i -lt $aa.Count;++$i) {
                    if ($aa[$i].stat -ne $bb[$i].stat -or $aa[$i].value -ne $bb[$i].value) { throw 'Reroll changed an unselected option.' }
                }
                $cost.gold=50+25*[int]$old.rarity+10*[int]$old.upgrade; $cost.essence=3+2*[int]$old.rarity
            }
            'enhance_legendary' {
                if ($id -ne $legend -or $old.upgrade -ne 0 -or $new.upgrade -ne 1) { throw 'Legendary enhancement target/rank mismatch.' }
                Assert-WorkshopSameItem $old $new 'enhance' @('upgrade')
                $next=[int]$old.upgrade+1; $cost.gold=30*$next*$next; $cost.ore=2*$next
            }
            default {
                $target=if ($action -eq 'deposit_saved') { $stored } else { $legend }
                if ($id -ne $target) { throw "Wrong storage transfer target: $action" }
                Assert-WorkshopSameItem $old $new $action
            }
        }
        Assert-WorkshopSameBalance $recordedCost $cost ($action+'/independent cost formula')
        foreach ($field in @('gold','ore','essence','root_cores','moon_crystals')) {
            if ($after.$field -ne ($before.$field-$cost.$field)) { throw "Actual resource delta is wrong: $action/$field" }
        }
        if ($null -ne $new) { $known[$id]=$new } else { [void]$known.Remove($id) }
    }
    if ((@($repairSlots | Sort-Object) -join ',') -cne '0,1') { throw 'Both worn equipment slots were not repaired.' }
    Assert-WorkshopSameBalance $balance $previousBalance 'final workshop ledger'
    foreach ($id in $known.Keys) {
        if (-not $items.ContainsKey($id) -or $items[$id].signature -cne $known[$id].signature) { throw "Final item differs from its last workshop event: $id" }
    }
    if ($crafted[3] -ne $legend -or $crafted[2] -ne $stored) { throw 'Crafted legendary/stored item IDs were substituted.' }
    $shotNames=@($Report.screenshots | ForEach-Object { Split-Path -Leaf ([string]$_) })
    foreach ($suffix in @('workshop-repair-before','workshop-repaired','workshop-dismantle-confirm',
        'workshop-crafted-grade-1','workshop-crafted-grade-2','workshop-crafted-grade-3','workshop-crafted-grade-4',
        'workshop-option-before','workshop-option-rerolled','workshop-stash-saved','workshop-legendary-enhanced','workshop-final-equipment')) {
        if ($shotNames -cnotcontains ($Role+'-'+$suffix+'.png')) { throw "Missing workshop operation screenshot: $Role/$suffix" }
    }
}
