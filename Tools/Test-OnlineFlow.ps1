#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$BackendRoot,
    [string]$BackendUrl='http://127.0.0.1:5292',
    [string]$PackagedClientExecutable,
    [ValidateSet('connectivity','dungeon','growth')][string]$Scenario='connectivity',
    [ValidateRange(1024,65535)][int]$Port=7821,
    [ValidateRange(300,1800)][int]$TimeoutSeconds=650
)
# Run only through the approved desktop adapter. No build or initial backend startup occurs here.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Backend-Link.ps1')
if(-not $IsWindows){throw 'Windows and PowerShell 7.4+ are required.'}
$backendLink=Get-FableBackendLink -BackendRoot $BackendRoot
function Assert-Online($Condition,[string]$Code){if(-not $Condition){throw [InvalidOperationException]::new('OnlineValidation:'+$Code)}}
function Initialize-OnlineSocketOwnerReader{
    if('FableOnlineValidation.SocketOwners' -as [type]){return}
    # IP Helper reads the IPv4 owner table without the CIM provider. No service or socket is changed.
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace FableOnlineValidation {
    public static class SocketOwners {
        [DllImport("iphlpapi.dll", ExactSpelling = true)]
        private static extern uint GetExtendedTcpTable(IntPtr table, ref uint size, [MarshalAs(UnmanagedType.Bool)] bool ordered, uint family, uint tableClass, uint reserved);
        [DllImport("iphlpapi.dll", ExactSpelling = true)]
        private static extern uint GetExtendedUdpTable(IntPtr table, ref uint size, [MarshalAs(UnmanagedType.Bool)] bool ordered, uint family, uint tableClass, uint reserved);
        private static uint Read(bool tcp, IntPtr table, ref uint size) {
            // AF_INET=2, TCP_TABLE_OWNER_PID_ALL=5, UDP_TABLE_OWNER_PID=1.
            return tcp ? GetExtendedTcpTable(table, ref size, false, 2, 5, 0) : GetExtendedUdpTable(table, ref size, false, 2, 1, 0);
        }
        private static int[] Owners(bool tcp, int port) {
            if (port < 1 || port > 65535) throw new ArgumentOutOfRangeException(nameof(port));
            uint size = 0;
            uint result = Read(tcp, IntPtr.Zero, ref size);
            if (result != 0 && result != 122) throw new Win32Exception((int)result);
            if (result == 0 && size == 0) return Array.Empty<int>();
            for (int attempt = 0; attempt < 4; ++attempt) {
                if (size < 4 || size > 16777216) throw new InvalidOperationException("Invalid IP Helper table size.");
                IntPtr table = Marshal.AllocHGlobal(checked((int)size));
                try {
                    result = Read(tcp, table, ref size);
                    if (result == 122) continue; // A concurrent socket change may enlarge the table.
                    if (result != 0) throw new Win32Exception((int)result);
                    int rowSize = tcp ? 24 : 12; // Windows SDK MIB_TCPROW_OWNER_PID / MIB_UDPROW_OWNER_PID.
                    uint count = unchecked((uint)Marshal.ReadInt32(table));
                    if ((ulong)count * (uint)rowSize + 4 > size) throw new InvalidOperationException("Truncated IP Helper table.");
                    var owners = new List<int>();
                    for (uint index = 0; index < count; ++index) {
                        IntPtr row = IntPtr.Add(table, checked(4 + (int)index * rowSize));
                        if (tcp && Marshal.ReadInt32(row, 0) != 2) continue; // MIB_TCP_STATE_LISTEN.
                        uint address = unchecked((uint)Marshal.ReadInt32(row, tcp ? 4 : 0));
                        if (address != 0 && address != 0x0100007Fu) continue; // Any or 127.0.0.1.
                        uint rawPort = unchecked((uint)Marshal.ReadInt32(row, tcp ? 8 : 4));
                        int localPort = (int)(((rawPort & 255u) << 8) | ((rawPort >> 8) & 255u));
                        if (localPort != port) continue;
                        int owner = Marshal.ReadInt32(row, tcp ? 20 : 8);
                        if (!owners.Contains(owner)) owners.Add(owner);
                    }
                    owners.Sort();
                    return owners.ToArray();
                } finally { Marshal.FreeHGlobal(table); }
            }
            throw new InvalidOperationException("IP Helper table changed repeatedly.");
        }
        public static int[] TcpListeners(int port) { return Owners(true, port); }
        public static int[] UdpEndpoints(int port) { return Owners(false, port); }
    }
}
"@
}
function Get-OnlineTcpListeningOwnerIds([int]$LocalPort){
    Initialize-OnlineSocketOwnerReader
    return [FableOnlineValidation.SocketOwners]::TcpListeners($LocalPort)
}
function Get-OnlineUdpOwnerIds([int]$LocalPort){
    Initialize-OnlineSocketOwnerReader
    return [FableOnlineValidation.SocketOwners]::UdpEndpoints($LocalPort)
}
function Test-OnlineUdpPortFree([int]$LocalPort){
    $socket=[Net.Sockets.UdpClient]::new([Net.Sockets.AddressFamily]::InterNetwork)
    try{
        $socket.ExclusiveAddressUse=$true
        $socket.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,$LocalPort))
        return $true
    }catch [Net.Sockets.SocketException]{
        if($_.Exception.SocketErrorCode -in @([Net.Sockets.SocketError]::AddressAlreadyInUse,[Net.Sockets.SocketError]::AccessDenied)){return $false}
        throw
    }finally{$socket.Dispose()}
}
function Read-OnlineText([string]$Path){
    $s=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try{$r=[IO.StreamReader]::new($s,[Text.Encoding]::UTF8,$true);try{return $r.ReadToEnd()}finally{$r.Dispose()}}finally{$s.Dispose()}
}
function Read-OnlineJson([string]$Path){return (Read-OnlineText $Path|ConvertFrom-Json -Depth 80)}
function Write-OnlineJson([string]$Path,$Value){
    $temp=$Path+'.writing-'+[Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($temp,($Value|ConvertTo-Json -Depth 80),[Text.UTF8Encoding]::new($false));[IO.File]::Move($temp,$Path,$true)
}
function Hash-Online([string]$Path){
    $s=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read,262144,[IO.FileOptions]::SequentialScan)
    try{$h=[Security.Cryptography.SHA256]::Create();try{return [Convert]::ToHexString($h.ComputeHash($s)).ToLowerInvariant()}finally{$h.Dispose()}}finally{$s.Dispose()}
}
function Require-OnlineFields($Value,[string[]]$Fields,[string]$Code){
    Assert-Online ($null -ne $Value) $Code
    foreach($field in $Fields){Assert-Online ($null -ne $Value.PSObject.Properties[$field]) $Code}
}
function Equal-Online($A,$B){
    if($null -eq $A -or $null -eq $B){return $null -eq $A -and $null -eq $B}
    if($A -is [string] -or $B -is [string]){return $A -is [string] -and $B -is [string] -and $A -ceq $B}
    if($A -is [bool] -or $B -is [bool]){return $A -is [bool] -and $B -is [bool] -and $A -eq $B}
    $numbers=@([byte],[short],[int],[long],[float],[double],[decimal])
    if($A.GetType() -in $numbers -or $B.GetType() -in $numbers){return $A.GetType() -in $numbers -and $B.GetType() -in $numbers -and [decimal]$A -eq [decimal]$B}
    if($A -is [array] -or $B -is [array]){
        if($A -isnot [array] -or $B -isnot [array] -or $A.Count -ne $B.Count){return $false}
        for($i=0;$i -lt $A.Count;$i++){if(-not(Equal-Online $A[$i] $B[$i])){return $false}};return $true
    }
    $ak=@($A.PSObject.Properties.Name|Sort-Object -CaseSensitive);$bk=@($B.PSObject.Properties.Name|Sort-Object -CaseSensitive)
    if(($ak -join '|') -cne ($bk -join '|')){return $false}
    foreach($key in $ak){if(-not(Equal-Online $A.$key $B.$key)){return $false}};return $true
}
function Online-UtcTicks($Value){
    if($Value -is [DateTimeOffset]){return $Value.UtcDateTime.Ticks}
    if($Value -is [DateTime]){return $Value.ToUniversalTime().Ticks}
    return [DateTime]::Parse([string]$Value,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime().Ticks
}
function Api-Online([string]$Method,[string]$Route,$Body=$null,[string]$Token=''){
    $q=@{Uri=($BackendUrl.TrimEnd('/')+$Route);Method=$Method;TimeoutSec=20;ErrorAction='Stop'}
    if($Token){$q.Headers=@{Authorization=('Bearer '+$Token)}}
    if($null -ne $Body){$q.Body=($Body|ConvertTo-Json -Depth 30 -Compress);$q.ContentType='application/json'}
    try{return Invoke-RestMethod @q}catch{throw [InvalidOperationException]::new('OnlineValidation:api_request_failed')}
}
function New-OnlineReaderToken([string]$Role){
    # Account registration happens only in the game. This separate session is used for read-only verification.
    $session=Api-Online POST '/v1/sessions' @{name=$accounts[$Role].name;password=$accounts[$Role].password}
    Require-OnlineFields $session @('accessToken') 'reader_session_missing'
    Assert-Online ($session.accessToken -cmatch '^[A-Za-z0-9_-]{43}$') 'reader_token_format'
    return [string]$session.accessToken
}
function Read-OnlineBackendIdentity{
    foreach($path in @($backendLink.stateFile,$backendLink.serverConfig,$credentialsPath,$serverKeyPath,$backendExe,$backendDll)){
        Assert-Online (Test-Path -LiteralPath $path -PathType Leaf) 'backend_must_already_exist'
    }
    $state=Read-OnlineJson $backendLink.stateFile;$settings=Read-OnlineJson $credentialsPath;$config=Read-OnlineJson $backendLink.serverConfig
    Require-OnlineFields $state @('status','projectRoot','backendExe','backendDll','backendPid','processStartedUtc','baseUrl','serverConfig','serverId','databasePort','schema','containerName','volume','composeProject') 'backend_state_incomplete'
    Require-OnlineFields $settings @('projectRoot','databasePort','apiPort','schema','serverId','containerName','volumeName','composeProject','databasePassword') 'backend_settings_incomplete'
    Require-OnlineFields $config @('url','serverId','serverKey') 'server_config_incomplete'
    Assert-Online ($state.status -ceq 'ready' -and $state.baseUrl.TrimEnd('/') -ceq $BackendUrl.TrimEnd('/') -and $config.url.TrimEnd('/') -ceq $BackendUrl.TrimEnd('/')) 'backend_endpoint_mismatch'
    Assert-Online ([IO.Path]::GetFullPath([string]$state.projectRoot) -ieq $backendLink.root -and [IO.Path]::GetFullPath([string]$settings.projectRoot) -ieq $backendLink.root -and [IO.Path]::GetFullPath([string]$state.serverConfig) -ieq $backendLink.serverConfig) 'backend_root_mismatch'
    Assert-Online ([IO.Path]::GetFullPath([string]$state.backendExe) -ieq $backendExe -and [IO.Path]::GetFullPath([string]$state.backendDll) -ieq $backendDll) 'backend_binary_mismatch'
    Assert-Online ([int]$settings.apiPort -eq $backendUri.Port -and [int]$settings.databasePort -eq [int]$state.databasePort -and [int]$state.databasePort -ge 1024 -and [int]$state.databasePort -le 65535) 'backend_port_mismatch'
    foreach($key in @('schema','serverId','containerName','composeProject')){Assert-Online ([string]$settings.$key -ceq [string]$state.$key) 'backend_identity_mismatch'}
    Assert-Online ([string]$settings.volumeName -ceq [string]$state.volume -and [string]$config.serverId -ceq [string]$state.serverId) 'backend_storage_identity_mismatch'
    $secret=[IO.File]::ReadAllText($serverKeyPath).Trim()
    Assert-Online ($secret.Length -ge 32 -and $secret -ceq [string]$config.serverKey -and [string]$settings.databasePassword -cmatch '^[a-f0-9]{64}$') 'private_config_mismatch'
    $secret=$null;$settings=$null;$config=$null
    # Database is fixed by the product launcher. Isolation is by container, volume, loopback port and schema.
    $compose=Read-OnlineText (Join-Path $backendLink.root 'compose.yaml');$launcher=Read-OnlineText $backendLink.launcher
    Assert-Online ($compose -match '(?m)^\s*POSTGRES_DB:\s*fablewood\s*$' -and $launcher.Contains('Database=fablewood;')) 'fixed_database_contract_changed'
    if($null -ne $state.PSObject.Properties['databaseName']){Assert-Online ([string]$state.databaseName -ceq 'fablewood') 'database_name_mismatch'}
    $process=Get-Process -Id ([int]$state.backendPid) -ErrorAction SilentlyContinue
    Assert-Online ($null -ne $process -and [IO.Path]::GetFullPath($process.Path) -ieq $backendExe -and $process.StartTime.ToUniversalTime().Ticks -eq (Online-UtcTicks $state.processStartedUtc)) 'backend_pid_ownership_mismatch'
    Assert-Online (@(Get-OnlineTcpListeningOwnerIds $backendUri.Port) -contains [int]$process.Id) 'backend_pid_not_listening'
    Assert-Online ((Api-Online GET '/health').status -ceq 'ok') 'backend_not_healthy';return $state
}
function Assert-OnlineSameBackend($A,$B){
    foreach($field in @('projectRoot','backendExe','backendDll','baseUrl','serverConfig','serverId','databasePort','schema','containerName','volume','composeProject')){Assert-Online ([string]$A.$field -ceq [string]$B.$field) 'backend_identity_changed'}
    foreach($field in @('gameAddress','gameServerName','databaseName')){
        $aValue=$A.PSObject.Properties[$field];$bValue=$B.PSObject.Properties[$field]
        Assert-Online (($null -eq $aValue -and $null -eq $bValue) -or ($null -ne $aValue -and $null -ne $bValue -and [string]$aValue.Value -ceq [string]$bValue.Value)) 'optional_backend_identity_changed'
    }
}
function Assert-OnlineCatalog([string]$Token){
    $catalog=Api-Online GET '/v1/servers' $null $Token;Require-OnlineFields $catalog @('servers') 'catalog_missing'
    Assert-Online (@($catalog.servers).Count -eq 1) 'catalog_must_be_single_owned_server'
    $entry=$catalog.servers[0];Require-OnlineFields $entry @('id','name','address') 'catalog_entry_incomplete'
    Assert-Online ([string]$entry.address -ceq ('127.0.0.1:'+$Port) -and -not [string]::IsNullOrWhiteSpace([string]$entry.id)) 'catalog_port_or_host_mismatch';return $entry
}
function Quote-Online([string]$Value){Assert-Online (-not $Value.Contains('"') -and -not $Value.Contains("`n") -and -not $Value.Contains("`r")) 'unsafe_process_argument';return '"'+$Value+'"'}
function Start-OnlineValidationProcess([string]$Role,[string]$Phase,[string]$Context=''){
    $folder=Join-Path $runFolder $Phase;[void][IO.Directory]::CreateDirectory($folder)
    $exe=if($Role -ceq 'server'){$serverExecutable}else{$clientExecutable}
    $arguments=[Collections.Generic.List[string]]::new();if($Role -ceq 'server' -or -not $PackagedClientExecutable){$arguments.Add((Quote-Online $script:FableProjectFile))}
    if($Role -ceq 'server'){
        $arguments.Add('/Game/Maps/ForestClearing?FableDedicated=1?FablePlay=1')
        foreach($arg in @('-server','-NullRHI','-nosound','-unattended',('-port='+$Port),'-MULTIHOME=127.0.0.1')){$arguments.Add($arg)}
        $arguments.Add('-FableBackendConfig='+(Quote-Online $backendLink.serverConfig))
    }else{
        if(-not $PackagedClientExecutable){$arguments.Add('-game')}
        foreach($arg in @('-d3d11','-windowed','-nosplash','-nosound','-unattended','-ResX=900','-ResY=600','-ForceRes','-WinY=40','-ini:Engine:[ConsoleVariables]:r.WarnOfBadDrivers=0')){$arguments.Add($arg)}
        $windowX=if($Role -ceq 'client1'){20}else{940};$arguments.Add('-WinX='+$windowX)
        $arguments.Add('-FableOnlineValidationContext='+(Quote-Online $Context));$arguments.Add('-FableOnlineBackendUrl='+(Quote-Online $BackendUrl))
        if($PackagedClientExecutable){
            # Isolate every packaged process from real user saves/config and the archive itself.
            $userDirectory=Join-Path $script:FableProjectRoot ('Saved\PackagedClientUsers\'+$runId+'\'+$Phase+'\'+$Role)
            Assert-Online (-not(Test-Path -LiteralPath $userDirectory)) 'packaged_user_directory_must_be_new'
            [void][IO.Directory]::CreateDirectory($userDirectory)
            $arguments.Add('-UserDir='+(Quote-Online ($userDirectory.Replace('\','/')+'/')))
        }
    }
    $log=Join-Path $folder ($Role+'.log')
    $arguments.Add('-FableOnlineValidationRole='+$Role);$arguments.Add('-FableOnlineValidationScenario='+$Scenario);$arguments.Add('-FableOnlineValidationDirectory='+(Quote-Online $folder))
    $arguments.Add('-FableOnlineValidationRun='+$runId);$arguments.Add('-abslog='+(Quote-Online $log))
    $arguments.Add('-LogCmds="LogInit off,LogNet off,LogLoad off"');$arguments.Add('-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"')
    try{$process=Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory $(if($Role -cne 'server' -and $PackagedClientExecutable){Split-Path -Parent $exe}else{$script:FableProjectRoot}) -WindowStyle Hidden -PassThru;$null=$process.Handle}catch{throw [InvalidOperationException]::new('OnlineValidation:owned_process_start_failed')}
    $record=[pscustomobject]@{role=$Role;phase=$Phase;pid=$process.Id;executable=$exe;startedUtc=$process.StartTime.ToUniversalTime().ToString('o');log=$log;forcedForScenario=$false;cleanupForced=$false;exitCode=$null}
    $ownedProcesses.Add($record);$processHandles[$process.Id]=$process;return $record
}
function Get-OnlineOwnedProcess($Record){
    $process=Get-Process -Id ([int]$Record.pid) -ErrorAction SilentlyContinue
    if($null -eq $process){return $null}
    Assert-Online ([IO.Path]::GetFullPath($process.Path) -ieq [IO.Path]::GetFullPath($Record.executable) -and $process.StartTime.ToUniversalTime().Ticks -eq (Online-UtcTicks $Record.startedUtc)) 'owned_pid_reused';return $process
}
function Stop-OnlineOwned($Record,[bool]$Scenario=$false){
    $process=Get-OnlineOwnedProcess $Record
    $handle=$processHandles[[int]$Record.pid];Assert-Online ($null -ne $handle) 'owned_process_handle_missing'
    if($process){Stop-Process -InputObject $process -Force -ErrorAction Stop;Assert-Online ($handle.WaitForExit(10000)) 'owned_process_stop_timeout'}
    else{Assert-Online ($handle.WaitForExit(10000)) 'owned_process_exit_unobserved'}
    $Record.exitCode=$handle.ExitCode;Assert-Online ($null -ne $Record.exitCode) 'owned_process_exit_code_missing'
    if($Scenario){$Record.forcedForScenario=$true}else{$Record.cleanupForced=$true}
}
function Read-OnlineReport($Record,[switch]$AllowMissing){
    $path=Join-Path (Join-Path $runFolder $Record.phase) ($Record.role+'.json')
    if(-not(Test-Path -LiteralPath $path)){if($AllowMissing){return $null};Assert-Online $false 'report_missing'}
    try{$report=Read-OnlineJson $path}catch{if($AllowMissing){return $null};Assert-Online $false 'report_unreadable'}
    Require-OnlineFields $report @('role','mode','status','step','processId','checks','screenshots','networkMode','localControllerCount','profiles') 'report_incomplete'
    Assert-Online ($report.role -ceq $Record.role -and [int]$report.processId -eq $Record.pid) 'report_owner_mismatch'
    if($Record.role -cne 'server'){Assert-Online ($report.mode -ceq $Record.phase) 'report_phase_mismatch'}
    Assert-Online ($report.status -cne 'failed') 'driver_reported_failure';return $report
}
function Test-OnlineCheck($Report,[string]$Name){return $null -ne $Report -and $null -ne $Report.checks.PSObject.Properties[$Name] -and $Report.checks.$Name -eq $true}
function Wait-OnlineServer($Record){
    $deadline=[DateTime]::UtcNow.AddSeconds(120)
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Record)) 'server_exited_before_ready';$report=Read-OnlineReport $Record -AllowMissing
        $ports=@(Get-OnlineUdpOwnerIds $Port | Where-Object { $_ -eq [int]$Record.pid })
        if($report -and $report.networkMode -ceq 'dedicated' -and $report.localControllerCount -eq 0 -and $ports.Count -gt 0){return $report}
        Start-Sleep -Milliseconds 300
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $false 'server_actual_dedicated_readiness_timeout'
}
function Wait-OnlineOfflineFailures($Clients){
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do{
        $ready=$true;$reports=@{}
        foreach($role in @('client1','client2')){
            Assert-Online ($null -ne (Get-OnlineOwnedProcess $Clients[$role])) 'client_exited_before_offline_failure'
            $report=Read-OnlineReport $Clients[$role] -AllowMissing;$reports[$role]=$report
            $ready=$ready -and $null -ne $report -and $report.step -eq 5 -and (Test-OnlineCheck $report 'connection_failure_visible')
        }
        if($ready){return $reports}
        Assert-Online (Test-OnlineUdpPortFree $Port) 'unexpected_server_during_failure_probe'
        Start-Sleep -Milliseconds 400
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $false 'offline_failure_observation_timeout'
}
function Assert-OnlineCheckpoint($Checkpoint,[string]$Role){
    Require-OnlineFields $Checkpoint @('playerId','heroClass','level','experience','classTier','revision','acknowledgedRevision','profileHash','ready','savePending','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId') 'checkpoint_incomplete'
    Assert-Online ($Checkpoint.ready -eq $true -and $Checkpoint.savePending -eq $false -and $Checkpoint.revision -ge 1 -and $Checkpoint.revision -eq $Checkpoint.acknowledgedRevision) 'checkpoint_not_acknowledged'
    Assert-Online ([string]$Checkpoint.playerId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and [string]$Checkpoint.profileHash -cmatch '^[a-f0-9]{64}$') 'checkpoint_identity_format'
    Assert-Online ($Checkpoint.heroClass -eq $accounts[$Role].initialClass -and $Checkpoint.classTier -eq 1) 'unexpected_character_class'
    if($Scenario -ceq 'connectivity'){
        Assert-Online ($Checkpoint.level -eq 1 -and $Checkpoint.experience -eq 0) 'unexpected_character_progression'
    }else{
        Require-OnlineFields $Checkpoint @('forestQuest','dungeonQuest','dungeonCompletions') 'dungeon_checkpoint_incomplete'
        if($Scenario -ceq 'dungeon'){
            Assert-Online ($Checkpoint.level -eq 4 -and $Checkpoint.experience -eq 65 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 2 -and $Checkpoint.dungeonCompletions -eq 0) 'dungeon_progression_totals_mismatch'
        }else{
            Assert-Online ($Checkpoint.level -eq 4 -and $Checkpoint.experience -eq 265 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 3 -and $Checkpoint.dungeonCompletions -eq 1) 'growth_progression_totals_mismatch'
        }
    }
    Require-OnlineFields $Checkpoint.wallet @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions') 'checkpoint_wallet_incomplete'
    Assert-Online (@($Checkpoint.wallet.PSObject.Properties).Count -eq 7) 'checkpoint_wallet_shape'
    if($Scenario -ceq 'connectivity'){
        Assert-Online ($Checkpoint.wallet.healthPotions -eq 3 -and $Checkpoint.wallet.manaPotions -eq 1) 'normal_potion_durable_quantity_mismatch'
        foreach($currency in @('gold','ore','essence','rootCores','moonCrystals')){Assert-Online ($Checkpoint.wallet.$currency -eq 0) 'unexpected_checkpoint_currency'}
    }else{
        Assert-Online ($Checkpoint.wallet.healthPotions -ge 0 -and $Checkpoint.wallet.healthPotions -le 3 -and $Checkpoint.wallet.manaPotions -eq 1) 'dungeon_potion_quantity_mismatch'
        if($Scenario -ceq 'dungeon'){
            Assert-Online ($Checkpoint.wallet.gold -eq 383 -and $Checkpoint.wallet.ore -eq 21 -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 1 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 6 -and $Checkpoint.stashCount -eq 0) 'dungeon_reward_totals_mismatch'
        }else{
            Assert-Online ($growthExpectations.ContainsKey($Role)) 'growth_expectation_missing'
            $expected=$growthExpectations[$Role]
            Assert-Online ($Checkpoint.wallet.gold -eq $expected.gold -and $Checkpoint.wallet.ore -eq $expected.ore -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 1 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 7 -and $Checkpoint.stashCount -eq 0) 'growth_reward_and_cost_totals_mismatch'
            Assert-Online ($Checkpoint.equippedWeaponInstanceId -ceq $expected.weapon -and $Checkpoint.equippedArmorInstanceId -ceq $expected.armor) 'growth_equipped_reward_identity_mismatch'
        }
    }
    Assert-Online (@($Checkpoint.items).Count -eq $Checkpoint.bagCount+$Checkpoint.stashCount) 'checkpoint_item_counts'
}
function Assert-OnlineShots($Report,[string]$Phase,[int]$Minimum){
    $shots=@($Report.screenshots);Assert-Online (@($shots|Select-Object -Unique).Count -ge $Minimum) 'screenshots_missing'
    $prefix=[IO.Path]::GetFullPath((Join-Path $runFolder $Phase)).TrimEnd('\')+'\';$result=@()
    foreach($shot in $shots){
        $path=[IO.Path]::GetFullPath([string]$shot)
        Assert-Online ($path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($path) -ieq '.png' -and (Test-Path -LiteralPath $path -PathType Leaf)) 'screenshot_path_invalid'
        $stream=[IO.File]::OpenRead($path);try{$header=[byte[]]::new(24);$length=$stream.Read($header,0,24)}finally{$stream.Dispose()}
        Assert-Online ($length -eq 24 -and ($header[0..7] -join ',') -ceq '137,80,78,71,13,10,26,10') 'screenshot_not_png'
        $result+=@([pscustomobject]@{path=$path;sha256=Hash-Online $path;actuallyViewed=$false})
    };return $result
}
function Assert-OnlineGrowth($Report,[string]$Role){
    Require-OnlineFields $Report @('growth','checkpoint') 'growth_report_missing'
    $g=$Report.growth
    Require-OnlineFields $g @('goldAfterQuest','oreAfterQuest','enhanceGoldCost','enhanceOreCost','enhanceBefore','enhanceAfter','repairGoldCost','repairOreCost','repairBefore','repairAfter','finalGold','finalOre','randomRewards') 'growth_report_incomplete'
    foreach($name in @('root_quest_reward_claimed','random_reward_rolls_valid','new_reward_equipment','normal_enhance_saved_once','normal_repair_saved_once','growth_final_saved')){
        Assert-Online (Test-OnlineCheck $Report $name) 'normal_growth_action_missing'
    }
    Assert-Online ($g.goldAfterQuest -eq 533 -and $g.oreAfterQuest -eq 29 -and $g.enhanceGoldCost -eq 30 -and $g.enhanceOreCost -eq 2 -and $g.randomRewards -eq 3) 'growth_cost_contract_mismatch'
    $enhanced=($g.enhanceBefore|ConvertTo-Json -Depth 12|ConvertFrom-Json)
    Assert-Online ($enhanced.upgrade -eq 0 -and $enhanced.rarity -eq 2 -and $enhanced.slot -eq 0 -and $enhanced.rollVersion -eq 1) 'enhanced_reward_invalid'
    $enhanced.upgrade=1;Assert-Online (Equal-Online $enhanced $g.enhanceAfter) 'enhance_random_fields_changed'
    $repaired=($g.repairBefore|ConvertTo-Json -Depth 12|ConvertFrom-Json)
    $missing=[int]$repaired.maxDurability-[int]$repaired.durability
    Assert-Online ($missing -gt 0 -and $g.repairGoldCost -eq $missing*([int]$repaired.rarity+1) -and $g.repairOreCost -eq [Math]::Ceiling($missing/25.0)) 'repair_cost_contract_mismatch'
    $repaired.durability=$repaired.maxDurability;Assert-Online (Equal-Online $repaired $g.repairAfter) 'repair_random_fields_changed'
    Assert-Online ($g.finalGold -eq 503-$g.repairGoldCost -and $g.finalOre -eq 27-$g.repairOreCost) 'growth_final_currency_mismatch'
    $cp=$Report.checkpoint
    Assert-Online ($cp.equippedWeaponInstanceId -ceq $g.enhanceAfter.instanceId) 'enhanced_weapon_not_equipped'
    $armor=@($cp.items|Where-Object {$_.instanceId -ceq $cp.equippedArmorInstanceId})
    Assert-Online ($armor.Count -eq 1 -and $armor[0].rarity -eq 2 -and $armor[0].slot -eq 1 -and $armor[0].setId -eq 1) 'boss_armor_not_equipped'
    $growthExpectations[$Role]=@{gold=[int]$g.finalGold;ore=[int]$g.finalOre;weapon=$cp.equippedWeaponInstanceId;armor=$cp.equippedArmorInstanceId}
}
function Assert-OnlineClientReport($Report,[string]$Role,[string]$Phase,[bool]$Crash){
    Require-OnlineFields $Report @('movementCm','checkpoint','joinAttempts','hasSession','flowState') 'client_report_incomplete'
    Assert-Online ($Report.movementCm -ge 65) 'normal_movement_missing'
    foreach($name in @('wrong_password_rejected','character_preview','two_distinct_remote_players','normal_movement')){Assert-Online (Test-OnlineCheck $Report $name) 'required_ui_check_missing'}
    if($Phase -ceq 'register'){
        Assert-Online (Test-OnlineCheck $Report 'connection_failure_visible') 'offline_failure_missing'
        Assert-Online (Test-OnlineCheck $Report 'normal_potion_saved_once') 'potion_ack_missing'
        if($Scenario -in @('dungeon','growth')){
            foreach($name in @('both_forest_quests_active','forest_reward_equipped','entered_root_ruins','second_wave','boss_seen','ally_downed','boss_defeated','ally_revived','server_reward_totals')){
                Assert-Online (Test-OnlineCheck $Report $name) 'dungeon_normal_gameplay_check_missing'
            }
        }
        Assert-Online ($Report.joinAttempts -ge 2) 'fresh_ticket_retry_missing'
    }
    if($Scenario -ceq 'growth' -and $Phase -ceq 'register'){Assert-OnlineGrowth $Report $Role}
    Assert-OnlineCheckpoint $Report.checkpoint $Role
    if($Crash){Assert-Online ($Report.status -ceq 'crash-ready' -and $Report.step -eq 30 -and $Report.networkMode -ceq 'client' -and $Report.localControllerCount -eq 1 -and $Report.hasSession -eq $true) 'crash_checkpoint_not_ready'}
    else{
        Require-OnlineFields $Report @('report_snapshot_frozen','menuProfileHash','menuRevision') 'terminal_report_metadata_missing'
        Assert-Online ($Report.status -ceq 'passed' -and $Report.report_snapshot_frozen -eq $true -and $Report.flowState -ceq 'SignedOut' -and $Report.hasSession -eq $false) 'terminal_ui_not_logged_out'
        foreach($name in @('save_release_ack_before_menu','logout_clears_session')){Assert-Online (Test-OnlineCheck $Report $name) 'safe_leave_or_logout_missing'}
    }
}
function Assert-OnlineServerObservation($ServerReport,$ClientReports){
    Require-OnlineFields $ServerReport @('serverObservedPlayerIds') 'server_simultaneous_players_evidence_missing'
    Assert-Online ($ServerReport.networkMode -ceq 'dedicated' -and $ServerReport.localControllerCount -eq 0) 'server_observation_not_dedicated'
    Assert-Online (Test-OnlineCheck $ServerReport 'server_observed_distinct_ready_players') 'server_ready_players_observation_missing'
    $observed=@($ServerReport.serverObservedPlayerIds|Sort-Object -CaseSensitive)
    $expected=@(@($ClientReports.client1.checkpoint.playerId,$ClientReports.client2.checkpoint.playerId)|Sort-Object -CaseSensitive)
    Assert-Online ($observed.Count -eq 2 -and @($observed|Select-Object -Unique).Count -eq 2 -and (Equal-Online $observed $expected)) 'server_simultaneous_player_ids_mismatch'
    if($Scenario -in @('dungeon','growth') -and $ClientReports.client1.mode -ceq 'register'){
        Require-OnlineFields $ServerReport @('serverDungeonObservation') 'server_dungeon_observation_missing'
        Assert-Online (Test-OnlineCheck $ServerReport 'server_observed_dungeon_completion') 'server_dungeon_completion_not_observed'
        $observation=$ServerReport.serverDungeonObservation
        Assert-Online ($observation.bossDefeated -eq $true -and $observation.dungeonWave -eq 3 -and $observation.encounterSerial -gt 0 -and @($observation.members).Count -eq 2) 'server_dungeon_observation_invalid'
        $members=@($observation.members|ForEach-Object playerId|Sort-Object -CaseSensitive)
        Assert-Online (Equal-Online $members $expected) 'server_dungeon_identities_mismatch'
        foreach($member in $observation.members){Assert-Online ($member.wallet.gold -eq 383 -and $member.wallet.ore -eq 21 -and $member.wallet.rootCores -eq 1 -and $member.dungeonQuest -eq 2) 'server_dungeon_reward_totals_mismatch'}
    }
    return ,$observed
}
function Wait-OnlinePhase([string]$Phase,$Server,$Clients,[bool]$CrashSecond){
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$results=@{};$crashObservedAt=$null
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server)) 'server_exited_during_phase'
        foreach($role in @('client1','client2')){
            if($results.ContainsKey($role)){continue}
            $record=$Clients[$role];$report=Read-OnlineReport $record -AllowMissing;$crash=$CrashSecond -and $role -ceq 'client2'
            if($report -and (($crash -and $report.status -ceq 'crash-ready') -or (-not $crash -and $report.status -ceq 'passed'))){
                Assert-OnlineClientReport $report $role $Phase $crash
                if($crash){
                    if($null -eq $crashObservedAt){$crashObservedAt=[DateTime]::UtcNow}
                    # Give the queued PNG write time to complete before killing this process only.
                    if(([DateTime]::UtcNow-$crashObservedAt).TotalSeconds -lt 2){continue}
                    $null=Assert-OnlineShots $report $Phase 5
                    Write-OnlineJson (Join-Path (Join-Path $runFolder $Phase) 'client2-crash-checkpoint.json') $report
                    Stop-OnlineOwned $record $true
                }
                $results[$role]=$report
            }else{Assert-Online ($null -ne (Get-OnlineOwnedProcess $record)) 'client_exited_without_terminal_report'}
        }
        if($results.Count -eq 2){
            Assert-Online ($results.client1.checkpoint.playerId -cne $results.client2.checkpoint.playerId) 'duplicate_player_identity'
            $null=Assert-OnlineServerObservation (Read-OnlineReport $Server) $results
            return $results
        }
        Start-Sleep -Milliseconds 350
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $false 'online_phase_timeout'
}
function Read-OnlineCanonical([string]$Role){
    $token=$readerTokens[$Role]
    $me=Api-Online GET '/v1/me' $null $token;$character=Api-Online GET '/v1/me/character' $null $token;$inventory=Api-Online GET '/v1/me/inventory' $null $token
    Require-OnlineFields $me @('playerId','revision','profileHash','wallet','leaseActive') 'me_response_incomplete'
    Require-OnlineFields $character @('playerId','revision','profileHash','wallet','inventoryVersion','inventory','summary') 'character_response_incomplete'
    Require-OnlineFields $inventory @('playerId','revision','profileHash','inventoryVersion','inventory') 'inventory_response_incomplete'
    foreach($view in @($character,$inventory)){Assert-Online ($view.playerId -ceq $me.playerId -and $view.revision -eq $me.revision -and $view.profileHash -ceq $me.profileHash -and $view.inventoryVersion -eq 1) 'canonical_views_inconsistent'}
    Assert-Online (Equal-Online $character.wallet $me.wallet) 'canonical_wallet_inconsistent'
    Assert-Online (Equal-Online $character.inventory $inventory.inventory) 'canonical_inventory_inconsistent'
    return [pscustomobject]@{me=$me;character=$character;inventory=$inventory}
}
function Assert-OnlineCanonical($Canonical,$Report,[string]$Role,[bool]$Crash){
    $cp=$Report.checkpoint;$me=$Canonical.me;$character=$Canonical.character;$inventory=$Canonical.inventory.inventory
    Assert-Online ($me.playerId -ceq $cp.playerId -and $me.leaseActive -eq $false -and $me.revision -ge $cp.acknowledgedRevision) 'durable_identity_or_lease_mismatch'
    Assert-Online (Equal-Online $me.wallet $cp.wallet) 'wallet_changed_after_checkpoint'
    Require-OnlineFields $character.summary @('version','heroClass','level','experience','classTier') 'character_summary_missing'
    Assert-Online ($character.summary.version -eq 1 -and @($character.summary.PSObject.Properties).Count -eq 5) 'character_summary_shape'
    foreach($field in @('heroClass','level','experience','classTier')){Assert-Online ($character.summary.$field -eq $cp.$field) 'character_summary_checkpoint_mismatch'}
    Assert-Online ($inventory.version -eq 1 -and $inventory.heroClass -eq $cp.heroClass) 'inventory_class_mismatch'
    Assert-Online (Equal-Online @($inventory.items) @($cp.items)) 'full_item_snapshot_changed'
    foreach($field in @('equippedWeaponInstanceId','equippedArmorInstanceId')){Assert-Online ($inventory.$field -ceq $cp.$field) 'equipped_uuid_changed'}
    $ids=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$locals=[Collections.Generic.HashSet[int]]::new();$maximum=0
    foreach($item in $inventory.items){
        Assert-Online ([string]$item.instanceId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $ids.Add([string]$item.instanceId) -and $locals.Add([int]$item.localId)) 'canonical_item_identity_invalid'
        $maximum=[Math]::Max($maximum,[int]$item.localId)
    }
    Assert-Online ($inventory.nextLocalId -gt $maximum) 'canonical_next_item_id_invalid'
    if(-not $Crash){Assert-Online ($Report.menuProfileHash -ceq $me.profileHash -and $Report.menuRevision -eq $me.revision) 'menu_differs_from_final_acknowledged_database'}
    # Cooldown UTC bookkeeping can change the opaque blob at final save; preserve both raw hashes.
    return [pscustomobject]@{role=$Role;checkpointHash=$cp.profileHash;finalHash=$me.profileHash;rawHashChanged=($cp.profileHash -cne $me.profileHash);checkpointRevision=$cp.acknowledgedRevision;finalRevision=$me.revision;walletExact=$true;itemsAndAffixOrderExact=$true;summaryExact=$true;equipmentExact=$true;leaseReleased=$true}
}
function Wait-OnlineDrain([string]$Phase,$Server,$Clients,$Reports){
    $deadline=[DateTime]::UtcNow.AddSeconds(180)
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server)) 'server_exited_before_drain'
        $report=Read-OnlineReport $Server -AllowMissing;$closed=$true
        foreach($role in @('client1','client2')){
            $record=$Clients[$role];$running=Get-OnlineOwnedProcess $record
            $handle=$processHandles[[int]$record.pid];Assert-Online ($null -ne $handle) 'client_process_handle_missing'
            # PID enumeration can disappear before the retained process handle signals its exit.
            # Wait within the existing drain deadline; only a signaled handle proves an exit code.
            if($null -ne $running -or -not $handle.WaitForExit(0)){$closed=$false;continue}
            $record.exitCode=$handle.ExitCode;Assert-Online ($null -ne $record.exitCode) 'client_exit_code_missing'
            if(-not $record.forcedForScenario){Assert-Online ($record.exitCode -eq 0) 'client_terminal_exit_failed'}
        }
        $zero=$null -ne $report -and $report.networkMode -ceq 'dedicated' -and $report.localControllerCount -eq 0 -and @($report.profiles).Count -eq 0
        $released=$true
        foreach($role in @('client1','client2')){$me=Api-Online GET '/v1/me' $null $readerTokens[$role];$released=$released -and $me.leaseActive -eq $false}
        if($closed -and $zero -and $released){
            $observedIds=Assert-OnlineServerObservation $report $Reports
            $result=@{};$comparisons=@()
            foreach($role in @('client1','client2')){$result[$role]=Read-OnlineCanonical $role;$comparisons+=@(Assert-OnlineCanonical $result[$role] $Reports[$role] $role ($Phase -ceq 'register' -and $role -ceq 'client2'))}
            Write-OnlineJson (Join-Path (Join-Path $runFolder $Phase) 'canonical.json') $result
            return [pscustomobject]@{canonical=$result;comparisons=$comparisons;server=$report;serverObservedPlayerIds=$observedIds;serverObservedIdsMatchCheckpoints=$true;allClientProcessesExited=$true;serverRemoteControllers=0;allLeasesInactive=$true}
        }
        Start-Sleep -Milliseconds 500
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $false 'disconnect_drain_or_lease_timeout'
}
function Quit-OnlineServer([string]$Phase,$Server){
    Write-OnlineJson (Join-Path (Join-Path $runFolder $Phase) 'control.json') @{allowJoin=$false;quitServer=$true}
    $process=Get-OnlineOwnedProcess $Server;Assert-Online ($null -ne $process) 'server_missing_before_graceful_quit'
    $handle=$processHandles[[int]$Server.pid];Assert-Online ($null -ne $handle) 'server_process_handle_missing'
    Assert-Online ($handle.WaitForExit(30000)) 'server_graceful_quit_timeout';$Server.exitCode=$handle.ExitCode
    Assert-Online ($null -ne $Server.exitCode) 'server_exit_code_missing'
    Assert-Online ($Server.exitCode -eq 0) 'server_graceful_exit_failed'
}
function Get-OnlineProtectedFiles{
    $items=@();$saved=Join-Path $script:FableProjectRoot 'Saved';$builds=Join-Path $script:FableProjectRoot 'Builds'
    if(Test-Path -LiteralPath $saved){$items+=@(Get-ChildItem -LiteralPath $saved -Recurse -File|Where-Object {$_.Extension -ieq '.sav' -or $_.Name -ceq 'completion.json'})}
    if(Test-Path -LiteralPath $builds){$items+=@(Get-ChildItem -LiteralPath $builds -Recurse -File -Filter '*.sav')};return @($items|Sort-Object FullName -Unique)
}
function Get-OnlineInputFiles{
    $items=@();foreach($directory in @('Source','Config','Tools')){$items+=@(Get-ChildItem -LiteralPath (Join-Path $script:FableProjectRoot $directory) -Recurse -File)}
    foreach($path in @($script:FableProjectFile,(Join-Path $script:FableProjectRoot 'Binaries\Win64\UnrealEditor-Fablewood.dll'),$backendExe,$backendDll,$serverExecutable,$clientExecutable)){$items+=@(Get-Item -LiteralPath $path)}
    return @($items|Sort-Object FullName -Unique)
}
function Scrub-OnlineLogs{
    $count=0
    foreach($log in @(Get-ChildItem -LiteralPath $runFolder -Recurse -File -Filter '*.log')){
        $temp=$log.FullName+'.sanitizing';$reader=[IO.StreamReader]::new($log.FullName,[Text.Encoding]::UTF8,$true)
        try{
            $writer=[IO.StreamWriter]::new($temp,$false,[Text.UTF8Encoding]::new($false))
            try{while($null -ne ($line=$reader.ReadLine())){
                $safe=$line
                foreach($account in $accounts.Values){if($account.password){$safe=$safe.Replace([string]$account.password,'[REDACTED_PASSWORD]')}}
                foreach($token in $allReaderTokens){if($token){$safe=$safe.Replace([string]$token,'[REDACTED_ACCESS_TOKEN]')}}
                $safe=[regex]::Replace($safe,'(?i)(FableTicket=)[A-Za-z0-9_-]+','$1[REDACTED_ONE_TIME_TICKET]')
                $safe=[regex]::Replace($safe,'(?i)(Bearer\s+)[A-Za-z0-9_.-]+','$1[REDACTED_ACCESS_TOKEN]')
                $safe=[regex]::Replace($safe,'(?i)("(?:password|accessToken|ticket|serverKey)"\s*:\s*")[^"\r\n]+','$1[REDACTED_SECRET]')
                if($safe -cne $line){++$count};$writer.WriteLine($safe)
            }}finally{$writer.Dispose()}
        }finally{$reader.Dispose()}
        [IO.File]::Move($temp,$log.FullName,$true)
    };return $count
}

$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$runFolder=Join-Path $script:FableProjectRoot ('Saved\OnlineValidation\'+$runId)
$privateFolder=Join-Path $runFolder 'private'
[void][IO.Directory]::CreateDirectory($privateFolder)
$ownedProcesses=[Collections.Generic.List[object]]::new();$processHandles=@{}
$accounts=@{};$growthExpectations=@{};$readerTokens=@{};$allReaderTokens=[Collections.Generic.List[string]]::new()
$originalFiles=@();$inputFiles=@();$snapshotsCaptured=$false;$backendBefore=$null;$resumeAccounts=$null
$serverExecutable='';$clientExecutable='';$stage='preflight'
$backendExe=Join-Path $backendLink.root 'Fablewood.Backend\bin\Release\net10.0\Fablewood.Backend.exe'
$backendDll=Join-Path $backendLink.root 'Fablewood.Backend\bin\Release\net10.0\Fablewood.Backend.dll'
$credentialsPath=Join-Path $backendLink.root 'Saved\Backend\backend-credentials.json'
$serverKeyPath=Join-Path $backendLink.root 'Saved\Backend\server-key.txt'
$summary=[ordered]@{
    runId=$runId;status='running';scenario=$Scenario;mode=$(if($PackagedClientExecutable){'editor-dedicated-two-packaged-ui-clients'}else{'editor-dedicated-two-ui-clients'});startedUtc=[DateTime]::UtcNow.ToString('o')
    backendUrl=$BackendUrl;gamePort=$Port;databaseName='fablewood';phases=[ordered]@{}
    scope=@('One PC, loopback only','Normal in-game registration, authentication, movement, potion and leave UI','Existing saves are not uploaded or injected','API reader sessions only inspect the two new accounts','API restart reuses the same database; PostgreSQL is not restarted here')
    visualReviewPerformed=$false;privateContextFolder=$privateFolder;initialBackendStartupPerformed=$false
}
try{
    $backendUri=[uri]$BackendUrl
    Assert-Online ($backendUri.IsAbsoluteUri -and $backendUri.Scheme -ceq 'http' -and $backendUri.Host -ceq '127.0.0.1' -and $backendUri.Port -ge 1024 -and $backendUri.Port -le 65535 -and $backendUri.AbsolutePath -ceq '/' -and -not $backendUri.UserInfo -and -not $backendUri.Query -and -not $backendUri.Fragment) 'backend_url_must_be_loopback_root'
    Assert-Online (Test-OnlineUdpPortFree $Port) 'game_port_already_owned'
    $resolvedEngine=Resolve-FableEngine -EngineRoot $EngineRoot
    Assert-FableReady
    $serverExecutable=Join-Path $resolvedEngine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $clientExecutable=Join-Path $resolvedEngine 'Engine\Binaries\Win64\UnrealEditor.exe'
    if($PackagedClientExecutable){
        Assert-Online ([IO.Path]::IsPathRooted($PackagedClientExecutable)) 'packaged_client_path_must_be_absolute'
        $clientExecutable=[IO.Path]::GetFullPath($PackagedClientExecutable)
        Assert-Online ([IO.Path]::GetFileName($clientExecutable) -ceq 'Fablewood.exe' -and (Test-Path -LiteralPath $clientExecutable -PathType Leaf)) 'packaged_client_executable_missing'
        $cursor=Get-Item -LiteralPath $clientExecutable -Force
        while($null -ne $cursor){
            Assert-Online (($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'packaged_client_reparse_rejected'
            if($cursor -is [IO.FileInfo]){$cursor=$cursor.Directory}else{$cursor=$cursor.Parent}
        }
        Assert-Online ($clientExecutable -match '[\\/]Fablewood[\\/]Binaries[\\/]Win64[\\/]Fablewood\.exe$') 'packaged_client_must_use_inner_executable'
        $summary.packagedClient=[ordered]@{executable=$clientExecutable;sha256=(Hash-Online $clientExecutable);isolatedUserDirectories=$true;usesEditorClient=$false}
    }
    foreach($path in @($serverExecutable,$clientExecutable)){Assert-Online (Test-Path -LiteralPath $path -PathType Leaf) 'editor_executable_missing'}
    $backendBefore=Read-OnlineBackendIdentity
    $summary.backendIdentity=[ordered]@{root=$backendLink.root;serverId=$backendBefore.serverId;databasePort=$backendBefore.databasePort;schema=$backendBefore.schema;containerName=$backendBefore.containerName;volume=$backendBefore.volume;composeProject=$backendBefore.composeProject;initialPid=$backendBefore.backendPid}
    $originalFiles=@(Get-OnlineProtectedFiles|ForEach-Object {[pscustomobject]@{path=$_.FullName;sha256=Hash-Online $_.FullName}})
    $inputFiles=@(Get-OnlineInputFiles|ForEach-Object {[pscustomobject]@{path=$_.FullName;sha256=Hash-Online $_.FullName}})
    $snapshotsCaptured=$true
    Write-OnlineJson (Join-Path $runFolder 'inputs-before.json') $inputFiles
    Write-OnlineJson (Join-Path $runFolder 'protected-before.json') $originalFiles

    # Restrict test credentials to the current account on ACL-capable volumes. The folder is always ignored Saved data.
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($privateFolder));$aclProtected=$false
    if($drive.DriveFormat -ceq 'NTFS'){
        $acl=[Security.AccessControl.DirectorySecurity]::new();$acl.SetAccessRuleProtection($true,$false)
        foreach($sid in @([Security.Principal.WindowsIdentity]::GetCurrent().User,[Security.Principal.SecurityIdentifier]::new('S-1-5-18'),[Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))){
            $rule=[Security.AccessControl.FileSystemAccessRule]::new($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow');$acl.AddAccessRule($rule)
        }
        Set-Acl -LiteralPath $privateFolder -AclObject $acl;$aclProtected=$true
    }
    $summary.privateContextAclProtected=$aclProtected
    foreach($role in @('client1','client2')){
        $accounts[$role]=[ordered]@{name=('o'+[Guid]::NewGuid().ToString('N').Substring(0,24));password=[Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(30));initialClass=$(if($role -ceq 'client1'){1}else{2})}
    }
    $contextPath=Join-Path $privateFolder 'register-context.private.json'
    Write-OnlineJson $contextPath @{mode='register';scenario=$Scenario;offlineFailure=$true;crashRole='client2';accounts=$accounts}
    $stage='register-offline-failure';$clients=@{}
    foreach($role in @('client1','client2')){$clients[$role]=Start-OnlineValidationProcess $role 'register' $contextPath}
    $offlineReports=Wait-OnlineOfflineFailures $clients
    foreach($role in @('client1','client2')){$readerTokens[$role]=New-OnlineReaderToken $role;$allReaderTokens.Add($readerTokens[$role])}
    $catalog=Assert-OnlineCatalog $readerTokens.client1
    Assert-Online (Equal-Online $catalog (Assert-OnlineCatalog $readerTokens.client2)) 'catalog_differs_between_accounts'
    $summary.catalog=$catalog
    $stage='register-join';$server=Start-OnlineValidationProcess 'server' 'register'
    $serverInitial=Wait-OnlineServer $server
    Write-OnlineJson (Join-Path (Join-Path $runFolder 'register') 'control.json') @{allowJoin=$true;quitServer=$false}
    $reports=Wait-OnlinePhase 'register' $server $clients $true
    $drained=Wait-OnlineDrain 'register' $server $clients $reports
    $freshCanonical=$drained.canonical
    $shots=@();$shots+=@(Assert-OnlineShots $reports.client1 'register' 7);$shots+=@(Assert-OnlineShots $reports.client2 'register' 5)
    $summary.phases.register=[ordered]@{status='passed';connectionFailureObservedAtStep5=$true;serverNetworkMode=$serverInitial.networkMode;serverLocalControllers=$serverInitial.localControllerCount;serverObservedPlayerIds=$drained.serverObservedPlayerIds;serverObservedIdsMatchCheckpoints=$true;forcedCloseRole='client2';serverDrained=$true;comparisons=$drained.comparisons;screenshots=$shots;reports=@((Join-Path $runFolder 'register\client1.json'),(Join-Path $runFolder 'register\client2-crash-checkpoint.json'));canonical=(Join-Path $runFolder 'register\canonical.json')}
    Quit-OnlineServer 'register' $server

    $stage='owned-api-restart-beforeidentity'
    $beforeRestart=Read-OnlineBackendIdentity;Assert-OnlineSameBackend $backendBefore $beforeRestart
    $stage='owned-api-restart-arguments'
    $restartArguments=@{Restart=$true;ContainerName=[string]$beforeRestart.containerName;VolumeName=[string]$beforeRestart.volume;ComposeProject=[string]$beforeRestart.composeProject;DatabasePort=[int]$beforeRestart.databasePort;ApiPort=$backendUri.Port;Schema=[string]$beforeRestart.schema;ServerId=[string]$beforeRestart.serverId}
    $tokens=$null;$parseErrors=$null;$launcherAst=[Management.Automation.Language.Parser]::ParseFile($backendLink.launcher,[ref]$tokens,[ref]$parseErrors)
    Assert-Online (@($parseErrors).Count -eq 0) 'backend_launcher_parse_failed'
    $parameterNames=@($launcherAst.ParamBlock.Parameters|ForEach-Object {$_.Name.VariablePath.UserPath})
    foreach($pair in @(@('gameAddress','GameAddress'),@('gameServerName','GameServerName'))){
        if($null -ne $beforeRestart.PSObject.Properties[$pair[0]]){Assert-Online ($pair[1] -in $parameterNames) 'catalog_restart_parameter_missing';$restartArguments[$pair[1]]=[string]$beforeRestart.($pair[0])}
    }
    # The existing launcher validates exact PID/path/start-time ownership before stopping its API only.
    $stage='owned-api-restart-call'
    $null=& $backendLink.launcher @restartArguments
    $stage='owned-api-restart-afteridentity'
    $afterRestart=Read-OnlineBackendIdentity;Assert-OnlineSameBackend $beforeRestart $afterRestart
    Assert-Online ($beforeRestart.backendPid -ne $afterRestart.backendPid -or (Online-UtcTicks $beforeRestart.processStartedUtc) -ne (Online-UtcTicks $afterRestart.processStartedUtc)) 'backend_did_not_restart'
    $summary.backendRestart=[ordered]@{sameDatabaseIdentity=$true;beforePid=$beforeRestart.backendPid;afterPid=$afterRestart.backendPid;beforeStartedUtc=$beforeRestart.processStartedUtc;afterStartedUtc=$afterRestart.processStartedUtc;healthy=$true}
    $afterApiRestart=@{}
    foreach($role in @('client1','client2')){
        $readerTokens[$role]=New-OnlineReaderToken $role;$allReaderTokens.Add($readerTokens[$role])
        Assert-Online (Equal-Online $catalog (Assert-OnlineCatalog $readerTokens[$role])) 'catalog_changed_after_restart'
        $afterApiRestart[$role]=Read-OnlineCanonical $role
        Assert-Online (Equal-Online $freshCanonical[$role] $afterApiRestart[$role]) 'api_restart_changed_durable_profile'
    }
    Write-OnlineJson (Join-Path $runFolder 'api-restart-canonical.json') $afterApiRestart

    $stage='login-resume';$resumeAccounts=@{}
    foreach($role in @('client1','client2')){$resumeAccounts[$role]=@{name=$accounts[$role].name;password=$accounts[$role].password;initialClass=$accounts[$role].initialClass;expected=@{playerId=$freshCanonical[$role].me.playerId;profileHash=$freshCanonical[$role].me.profileHash}}}
    $contextPath=Join-Path $privateFolder 'login-context.private.json'
    Write-OnlineJson $contextPath @{mode='login';scenario=$Scenario;offlineFailure=$false;accounts=$resumeAccounts}
    $server=Start-OnlineValidationProcess 'server' 'login';$serverInitial=Wait-OnlineServer $server
    $clients=@{};foreach($role in @('client1','client2')){$clients[$role]=Start-OnlineValidationProcess $role 'login' $contextPath}
    $reports=Wait-OnlinePhase 'login' $server $clients $false
    $drained=Wait-OnlineDrain 'login' $server $clients $reports
    foreach($role in @('client1','client2')){
        $before=$freshCanonical[$role];$after=$drained.canonical[$role]
        Assert-Online ($before.me.playerId -ceq $after.me.playerId) 'resume_player_identity_changed'
        Assert-Online (Equal-Online $before.me.wallet $after.me.wallet) 'resume_wallet_changed'
        Assert-Online (Equal-Online $before.character.summary $after.character.summary) 'resume_character_summary_changed'
        Assert-Online ($before.inventory.inventoryVersion -eq $after.inventory.inventoryVersion -and (Equal-Online $before.inventory.inventory $after.inventory.inventory)) 'resume_full_inventory_changed'
    }
    $shots=@();foreach($role in @('client1','client2')){$shots+=@(Assert-OnlineShots $reports[$role] 'login' 5)}
    $summary.phases.login=[ordered]@{status='passed';serverNetworkMode=$serverInitial.networkMode;serverLocalControllers=$serverInitial.localControllerCount;serverObservedPlayerIds=$drained.serverObservedPlayerIds;serverObservedIdsMatchCheckpoints=$true;serverDrained=$true;comparisons=$drained.comparisons;screenshots=$shots;resumeWalletItemsSummaryExact=$true;reports=@((Join-Path $runFolder 'login\client1.json'),(Join-Path $runFolder 'login\client2.json'));canonical=(Join-Path $runFolder 'login\canonical.json')}
    Quit-OnlineServer 'login' $server
    $summary.status='passed'
}catch{
    $summary.status='failed';$summary.failedStage=$stage
    $message=[string]$_.Exception.Message
    $summary.failure=if($message -cmatch '^OnlineValidation:[a-z0-9_]+$'){$message}else{'OnlineValidation:unexpected_failure_private_details_suppressed'}
    # Constant error metadata only: no message, arguments, target object, path or stack.
    $diagnosticError=$_
    $exceptionType=[string]$diagnosticError.Exception.GetType().FullName
    if($exceptionType -cnotmatch '^(?:System|Microsoft\.PowerShell)\.[A-Za-z0-9_.+`]{1,160}$'){$exceptionType='other-exception-type'}
    $errorId=[string]$diagnosticError.FullyQualifiedErrorId
    if($errorId -cnotmatch '^(?:PropertyNotFoundStrict|PropertyAssignmentException|CommandNotFoundException|ParameterArgumentValidationError|NamedParameterNotFound|PositionalParameterNotFound|AmbiguousParameterSet|InvalidOperation|RuntimeException|ActionPreferenceStopException|NativeCommandExitException|CouldNotAutoloadMatchingModule|InvalidCastFromStringToInteger|ParameterArgumentTransformationError)(?:,Microsoft\.PowerShell\.Commands\.[A-Za-z0-9]+Command)?$'){$errorId='other-error-id'}
    $invocation=$diagnosticError.InvocationInfo
    $scriptBaseName='other-script';$scriptLine=0;$scriptOffset=0
    if($null -ne $invocation){
        $name=[IO.Path]::GetFileName([string]$invocation.ScriptName)
        if($name -cin @('Test-OnlineFlow.ps1','Start-Backend.ps1')){$scriptBaseName=$name}
        $scriptLine=[Math]::Clamp([int]$invocation.ScriptLineNumber,0,1000000)
        $scriptOffset=[Math]::Clamp([int]$invocation.OffsetInLine,0,1000000)
    }
    $summary.failureDiagnostics=[ordered]@{exceptionType=$exceptionType;errorId=$errorId;scriptBaseName=$scriptBaseName;scriptLine=$scriptLine;offset=$scriptOffset}
    Write-Warning ('Online validation failed during '+$stage+'. See the sanitized summary.')
}finally{
    foreach($record in $ownedProcesses){try{if($null -ne (Get-OnlineOwnedProcess $record)){Stop-OnlineOwned $record}}catch{$summary.status='failed';$summary.cleanupOwnershipError=$true}}
    try{$summary.sanitizedLogLines=Scrub-OnlineLogs}catch{$summary.status='failed';$summary.logSanitizationFailed=$true}
    $summary.processes=@($ownedProcesses.ToArray())
    try{
        $changed=@();foreach($entry in $originalFiles){if(-not(Test-Path -LiteralPath $entry.path) -or (Hash-Online $entry.path) -cne $entry.sha256){$changed+=@($entry.path)}}
        $oldPaths=@($originalFiles|ForEach-Object {$_.path});$newSaves=@()
        if($snapshotsCaptured){$newSaves=@(Get-OnlineProtectedFiles|Where-Object {$_.Extension -ieq '.sav' -and $_.FullName -notin $oldPaths}|ForEach-Object {$_.FullName})}
        $summary.protectedFiles=[ordered]@{checked=$originalFiles.Count;changed=$changed;newLocalSaveFiles=$newSaves;comparisonAvailable=$snapshotsCaptured;preserved=($snapshotsCaptured -and $changed.Count -eq 0 -and $newSaves.Count -eq 0)}
        if($snapshotsCaptured -and -not $summary.protectedFiles.preserved){$summary.status='failed'}
    }catch{$summary.status='failed';$summary.preservationCheckFailed=$true}
    try{
        $changed=@();foreach($entry in $inputFiles){if(-not(Test-Path -LiteralPath $entry.path) -or (Hash-Online $entry.path) -cne $entry.sha256){$changed+=@($entry.path)}}
        $summary.inputFiles=[ordered]@{checked=$inputFiles.Count;changed=$changed;comparisonAvailable=$snapshotsCaptured;preserved=($snapshotsCaptured -and $changed.Count -eq 0)}
        if($changed.Count -gt 0){$summary.status='failed'}
    }catch{$summary.status='failed';$summary.inputHashCheckFailed=$true}
    $readerTokens.Clear();$allReaderTokens.Clear();$accounts.Clear();$resumeAccounts=$null
    $summary.finishedUtc=[DateTime]::UtcNow.ToString('o')
    Write-OnlineJson (Join-Path $runFolder 'summary.json') $summary
}
Write-Host ('Online validation summary: '+(Join-Path $runFolder 'summary.json'))
if($summary.status -cne 'passed'){throw 'Online validation did not pass; inspect the sanitized summary.'}
