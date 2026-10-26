#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$BackendRoot,
    [string]$BackendUrl='http://127.0.0.1:5292',
    [string]$PackagedClientExecutable,
    [ValidateSet('connectivity','dungeon','growth','reconnect','party','party-entry','party-isolation','party-results','unit4-legacy-services')][string]$Scenario='connectivity',
    [ValidateSet('none','unit2-legacy-anchors')][string]$TownTravelScenario='none',
    [switch]$PartyReadyCancellation,
    [ValidateRange(1024,65535)][int]$Port=7821,
    [ValidateRange(300,1800)][int]$TimeoutSeconds=650
)
# Run only through the approved desktop adapter. No build or initial backend startup occurs here.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Backend-Link.ps1')
if(-not $IsWindows){throw 'Windows and PowerShell 7.4+ are required.'}
$backendLink=Get-FableBackendLink -BackendRoot $BackendRoot
function Assert-Online($Condition,[string]$Code){if(-not $Condition){throw [InvalidOperationException]::new('OnlineValidation:'+$Code)}}
Assert-Online ($TownTravelScenario -ceq 'none' -or $Scenario -ceq 'party-results') 'town_subscenario_requires_results'
Assert-Online (-not $PartyReadyCancellation -or $Scenario -ceq 'party-results') 'party_ready_cancellation_requires_results'
if($TownTravelScenario -ceq 'unit2-legacy-anchors' -and -not $PSBoundParameters.ContainsKey('TimeoutSeconds')){$TimeoutSeconds=830}
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
    $arguments.Add('-FableTownTravelScenario='+$TownTravelScenario)
    if($PartyReadyCancellation){$arguments.Add('-FablePartyReadyCancellation')}
    $arguments.Add('-FableOnlineValidationRun='+$runId);$arguments.Add('-abslog='+(Quote-Online $log))
    $arguments.Add('-LogCmds="LogInit off,LogNet off,LogLoad off"');$arguments.Add('-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"')
    try{$process=Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory $(if($Role -cne 'server' -and $PackagedClientExecutable){Split-Path -Parent $exe}else{$script:FableProjectRoot}) -WindowStyle Hidden -PassThru;$null=$process.Handle}catch{throw [InvalidOperationException]::new('OnlineValidation:owned_process_start_failed')}
    $record=[pscustomobject]@{role=$Role;phase=$Phase;validationMode=$(if($Phase -ceq 'register-resume'){'login'}else{$Phase});pid=$process.Id;executable=$exe;startedUtc=$process.StartTime.ToUniversalTime().ToString('o');log=$log;forcedForScenario=$false;cleanupForced=$false;exitCode=$null}
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
    if($script:Scenario -ceq 'unit4-legacy-services'){$Record|Add-Member -NotePropertyName exitedUtc -NotePropertyValue $handle.ExitTime.ToUniversalTime().ToString('o') -Force}
    if($Scenario){$Record.forcedForScenario=$true}else{$Record.cleanupForced=$true}
}
function Read-OnlineReport($Record,[switch]$AllowMissing){
    $path=Join-Path (Join-Path $runFolder $Record.phase) ($Record.role+'.json')
    if(-not(Test-Path -LiteralPath $path)){if($AllowMissing){return $null};Assert-Online $false 'report_missing'}
    try{$report=Read-OnlineJson $path}catch{if($AllowMissing){return $null};Assert-Online $false 'report_unreadable'}
    Require-OnlineFields $report @('role','mode','status','step','processId','checks','screenshots','networkMode','localControllerCount','profiles') 'report_incomplete'
    Assert-Online ($report.role -ceq $Record.role -and [int]$report.processId -eq $Record.pid) 'report_owner_mismatch'
    if($Record.role -cne 'server'){Assert-Online ($report.mode -ceq $Record.validationMode) 'report_phase_mismatch'}
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
function Assert-OnlineCheckpoint($Checkpoint,[string]$Role,[bool]$BeforeSolo=$false){
    Require-OnlineFields $Checkpoint @('playerId','heroClass','level','experience','classTier','revision','acknowledgedRevision','profileHash','ready','savePending','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId') 'checkpoint_incomplete'
    Assert-Online ($Checkpoint.ready -eq $true -and $Checkpoint.savePending -eq $false -and $Checkpoint.revision -ge 1 -and $Checkpoint.revision -eq $Checkpoint.acknowledgedRevision) 'checkpoint_not_acknowledged'
    Assert-Online ([string]$Checkpoint.playerId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and [string]$Checkpoint.profileHash -cmatch '^[a-f0-9]{64}$') 'checkpoint_identity_format'
    Assert-Online ($Checkpoint.heroClass -eq $accounts[$Role].initialClass -and $Checkpoint.classTier -eq 1) 'unexpected_character_class'
    if($Scenario -in @('connectivity','party')){
        Assert-Online ($Checkpoint.level -eq 1 -and $Checkpoint.experience -eq 0) 'unexpected_character_progression'
    }else{
        Require-OnlineFields $Checkpoint @('forestQuest','dungeonQuest','dungeonCompletions') 'dungeon_checkpoint_incomplete'
        if($Scenario -ceq 'party-results'){
            $extra=$Role -ceq 'client1' -and -not $BeforeSolo
            Assert-Online ($Checkpoint.level -eq 4 -and $Checkpoint.experience -eq $(if($extra){100}else{65}) -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 2 -and $Checkpoint.dungeonCompletions -eq 0) 'results_pending_quest_progression_mismatch'
        }elseif($Scenario -ceq 'party-entry'){
            Assert-Online ($Checkpoint.level -eq 2 -and $Checkpoint.experience -eq 105 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 1 -and $Checkpoint.dungeonCompletions -eq 0) 'entry_forest_progression_totals_mismatch'
        }elseif($Scenario -ceq 'unit4-legacy-services'){
            # 포털은 준비 UI까지만 열며 별도 던전 의뢰 수락·입장·완료는 하지 않는다.
            Assert-Online ($Checkpoint.level -eq 2 -and $Checkpoint.experience -eq 105 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 0 -and $Checkpoint.dungeonCompletions -eq 0) 'services_final_progression_mismatch'
        }elseif($Scenario -ceq 'dungeon'){
            Assert-Online ($Checkpoint.level -eq 4 -and $Checkpoint.experience -eq 65 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 2 -and $Checkpoint.dungeonCompletions -eq 0) 'dungeon_progression_totals_mismatch'
        }else{
            if($Scenario -ceq 'growth'){
                Assert-Online ($Checkpoint.level -eq 4 -and $Checkpoint.experience -eq 265 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 3 -and $Checkpoint.dungeonCompletions -eq 1) 'growth_progression_totals_mismatch'
            }else{
                Assert-Online ($Checkpoint.level -eq 2 -and $Checkpoint.experience -eq 140 -and $Checkpoint.forestQuest -eq 3 -and $Checkpoint.dungeonQuest -eq 1 -and $Checkpoint.dungeonCompletions -eq 0) 'midrun_progression_totals_mismatch'
            }
        }
    }
    Require-OnlineFields $Checkpoint.wallet @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions') 'checkpoint_wallet_incomplete'
    Assert-Online (@($Checkpoint.wallet.PSObject.Properties).Count -eq 7) 'checkpoint_wallet_shape'
    if($Scenario -in @('connectivity','party')){
        Assert-Online ($Checkpoint.wallet.healthPotions -eq 3 -and $Checkpoint.wallet.manaPotions -eq 1) 'normal_potion_durable_quantity_mismatch'
        foreach($currency in @('gold','ore','essence','rootCores','moonCrystals')){Assert-Online ($Checkpoint.wallet.$currency -eq 0) 'unexpected_checkpoint_currency'}
    }else{
        if($Scenario -ceq 'unit4-legacy-services'){
            # 서비스 run만 초기 마나 물약 사용을 건너뛰며 숲 전투의 체력 물약 사용은 허용한다.
            Assert-Online ($Checkpoint.wallet.healthPotions -ge 1 -and $Checkpoint.wallet.healthPotions -le 4 -and $Checkpoint.wallet.manaPotions -eq 2) 'services_potion_quantity_mismatch'
        }else{
            Assert-Online ($Checkpoint.wallet.healthPotions -ge 0 -and $Checkpoint.wallet.healthPotions -le 3 -and $Checkpoint.wallet.manaPotions -eq 1) 'dungeon_potion_quantity_mismatch'
        }
        if($Scenario -ceq 'party-results'){
            $extra=$Role -ceq 'client1' -and -not $BeforeSolo
            Assert-Online ($Checkpoint.wallet.gold -eq $(if($extra){401}else{383}) -and $Checkpoint.wallet.ore -eq $(if($extra){22}else{21}) -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 1 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 6 -and $Checkpoint.stashCount -eq 0) 'results_personal_reward_totals_mismatch'
        }elseif($Scenario -ceq 'party-entry'){
            Assert-Online ($Checkpoint.wallet.gold -eq 135 -and $Checkpoint.wallet.ore -eq 7 -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 0 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 5 -and $Checkpoint.stashCount -eq 0) 'entry_forest_reward_totals_mismatch'
        }elseif($Scenario -ceq 'unit4-legacy-services'){
            Assert-Online ($Checkpoint.wallet.gold -eq 90 -and $Checkpoint.wallet.ore -eq 5 -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 0 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 5 -and $Checkpoint.stashCount -eq 0) 'services_final_economy_mismatch'
        }elseif($Scenario -ceq 'dungeon'){
            Assert-Online ($Checkpoint.wallet.gold -eq 383 -and $Checkpoint.wallet.ore -eq 21 -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 1 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 6 -and $Checkpoint.stashCount -eq 0) 'dungeon_reward_totals_mismatch'
        }else{
            if($Scenario -ceq 'growth'){
                Assert-Online ($growthExpectations.ContainsKey($Role)) 'growth_expectation_missing'
                $expected=$growthExpectations[$Role]
                Assert-Online ($Checkpoint.wallet.gold -eq $expected.gold -and $Checkpoint.wallet.ore -eq $expected.ore -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 1 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 7 -and $Checkpoint.stashCount -eq 0) 'growth_reward_and_cost_totals_mismatch'
                Assert-Online ($Checkpoint.equippedWeaponInstanceId -ceq $expected.weapon -and $Checkpoint.equippedArmorInstanceId -ceq $expected.armor) 'growth_equipped_reward_identity_mismatch'
            }else{
                Assert-Online ($Checkpoint.wallet.gold -eq 153 -and $Checkpoint.wallet.ore -eq 8 -and $Checkpoint.wallet.essence -eq 0 -and $Checkpoint.wallet.rootCores -eq 0 -and $Checkpoint.wallet.moonCrystals -eq 0 -and $Checkpoint.bagCount -eq 5 -and $Checkpoint.stashCount -eq 0) 'midrun_reward_totals_mismatch'
            }
        }
    }
    Assert-Online (@($Checkpoint.items).Count -eq $Checkpoint.bagCount+$Checkpoint.stashCount) 'checkpoint_item_counts'
}
function Assert-OnlineShots($Report,[string]$Phase,[int]$Minimum){
    $shots=@($Report.screenshots);Assert-Online (@($shots|Select-Object -Unique).Count -ge $Minimum) 'screenshots_missing'
    if($TownTravelScenario -ceq 'unit2-legacy-anchors'){
        # 추가 Unit2 캡처로 기존 회귀의 원래 최소 개수를 대체하지 않는다.
        $legacyShots=@($shots|Where-Object {[IO.Path]::GetFileName([string]$_) -cnotmatch '-unit2-'}|Select-Object -Unique)
        Assert-Online ($legacyShots.Count -ge $Minimum) 'original_results_screenshots_missing'
    }
    $prefix=[IO.Path]::GetFullPath((Join-Path $runFolder $Phase)).TrimEnd('\')+'\';$result=@()
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    foreach($shot in $shots){
        $path=[IO.Path]::GetFullPath([string]$shot)
        Assert-Online ($path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($path) -ieq '.png') 'screenshot_path_invalid'
        # 파일이 생기는 시점과 비동기 PNG 저장이 끝나는 시점 사이의 짧은 차이를 기다린다.
        $stream=$null
        do{
            if(Test-Path -LiteralPath $path -PathType Leaf){
                $candidate=$null
                try{
                    $candidate=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
                    if($candidate.Length -ge 24){$stream=$candidate;$candidate=$null;break}
                }catch [IO.IOException]{}catch [UnauthorizedAccessException]{}finally{if($candidate){$candidate.Dispose()}}
            }
            if([DateTime]::UtcNow -ge $deadline){break}
            Start-Sleep -Milliseconds 100
        }while($true)
        Assert-Online ($null -ne $stream) 'screenshot_save_timeout'
        try{$header=[byte[]]::new(24);$length=$stream.Read($header,0,24)}finally{$stream.Dispose()}
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
function Assert-OnlineMidrun($Report,[string]$Role,[bool]$Interrupted){
    Require-OnlineFields $Report @('midrun','checkpoint','movementCm') 'midrun_report_missing'
    $m=$Report.midrun
    Assert-Online ($m.encounterSerial -gt 0 -and $m.wave -eq 1 -and $m.defeated -eq 1 -and $m.liveEnemies -eq 2) 'midrun_scene_mismatch'
    Assert-Online (Test-OnlineCheck $Report 'midrun_reward_saved_once') 'midrun_reward_not_acknowledged'
    Assert-OnlineCheckpoint $Report.checkpoint $Role
    if($Interrupted){
        foreach($name in @('wrong_password_rejected','character_preview','connection_failure_visible','two_distinct_remote_players','normal_movement','normal_potion_saved_once','both_forest_quests_active','forest_reward_equipped','entered_root_ruins')){Assert-Online (Test-OnlineCheck $Report $name) 'interrupted_normal_flow_missing'}
        Assert-Online ($Report.status -ceq 'crash-ready' -and $Report.step -eq 30 -and $Report.networkMode -ceq 'client' -and $Report.localControllerCount -eq 1 -and $Report.hasSession -eq $true -and $Report.movementCm -ge 65) 'interrupted_checkpoint_invalid'
    }else{Assert-Online (Test-OnlineCheck $Report 'midrun_same_encounter_rejoined') 'midrun_rejoin_not_observed'}
}
function Assert-OnlineParty($Report,[string]$Role,[bool]$Resume=$false){
    Require-OnlineFields $Report @('partyStage1') 'party_stage_report_missing'
    $party=$Report.partyStage1
    Require-OnlineFields $party @('selfKey','peerKey','finalPartyId','snapshot') 'party_stage_report_incomplete'
    Assert-Online ([string]$party.finalPartyId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$') 'party_final_id_invalid'
    Assert-Online (-not [string]::IsNullOrWhiteSpace([string]$party.selfKey) -and -not [string]::IsNullOrWhiteSpace([string]$party.peerKey) -and $party.selfKey -cne $party.peerKey) 'party_public_member_keys_invalid'
    if($Resume){
        Assert-Online ($party.restored -eq $true -and (Test-OnlineCheck $Report 'party_same_character_restored') -and
            (Test-OnlineCheck $Report 'party_restore_handshake_complete')) 'party_same_character_not_restored'
        return
    }
    Assert-Online ($party.completed -eq $true) 'party_steps_not_complete'
    $required=if($Role -ceq 'client1'){
        @('party_created','party_invite_declined','party_invite_accepted','party_invalid_requests_rejected','party_leader_transferred','party_left','party_rejoined_after_leave','party_dissolved','party_final_roster_ready','party_disconnected_leader_reserved','party_disconnect_leader_failover','party_same_character_restored','party_restore_handshake_complete')
    }else{
        @('party_invite_declined','party_invite_accepted','party_member_transfer_rejected','party_duplicate_create_rejected','party_invite_replay_rejected','party_leader_transferred','party_member_departure_observed','party_rejoined_after_leave','party_dissolved','party_final_roster_ready')
    }
    foreach($name in $required){Assert-Online (Test-OnlineCheck $Report $name) 'party_ui_action_or_server_rejection_missing'}
}
function Resume-OnlinePartyClient($Server,$ClientOne,$Interrupted){
    $deadline=[DateTime]::UtcNow.AddSeconds(100);$ready=$false;$latest=$null
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server) -and $null -ne (Get-OnlineOwnedProcess $ClientOne)) 'party_survivors_exited_during_disconnect'
        $latest=Read-OnlineReport $Server -AllowMissing
        $survivor=Read-OnlineReport $ClientOne -AllowMissing
        $me=Api-Online GET '/v1/me' $null $readerTokens.client2
        if($latest -and @($latest.profiles).Count -eq 1 -and $me.leaseActive -eq $false -and
            (Test-OnlineCheck $latest 'server_party_disconnected_member_reserved') -and
            (Test-OnlineCheck $survivor 'party_disconnected_leader_reserved') -and
            (Test-OnlineCheck $survivor 'party_disconnect_leader_failover')){$ready=$true;break}
        Start-Sleep -Milliseconds 350
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $ready 'party_disconnect_or_lease_release_timeout'
    Require-OnlineFields $latest @('serverPartyBefore','serverPartySolo') 'party_server_before_or_solo_missing'
    $canonical=Read-OnlineCanonical 'client2';$comparison=Assert-OnlineCanonical $canonical $Interrupted 'client2' $true
    Write-OnlineJson (Join-Path (Join-Path $runFolder 'register') 'client2-party-disconnect-canonical.json') $canonical
    $entry=$accounts.client2;$context=Join-Path $privateFolder 'register-resume-context.private.json'
    Write-OnlineJson $context @{mode='login';scenario='party';offlineFailure=$false;resumeExpedition=$true;accounts=@{client2=@{name=$entry.name;password=$entry.password;initialClass=$entry.initialClass;expected=@{playerId=$canonical.me.playerId;profileHash=$canonical.me.profileHash;partyId=$Interrupted.partyStage1.finalPartyId}}}}
    $resumed=Start-OnlineValidationProcess 'client2' 'register-resume' $context
    $summary['interruption']=[ordered]@{status='reconnecting';originalCheckpoint=$Interrupted.checkpoint;originalParty=$Interrupted.partyStage1;canonicalAfterDisconnect=$comparison;serverSurvived=$true;clientOneSurvived=$true;serverControllersAfterDisconnect=1;clientTwoLeaseReleased=$true;resumeProcessId=$resumed.pid;resumeReportDirectory='register-resume'}
    return $resumed
}
# 실제 한 처치가 진행된 뒤에도 종료된 개인의 정규 데이터가 그대로인지 먼저 확인한다.
function Resume-OnlineResultsClient($Server,$ClientOne,$Interrupted){
    $deadline=[DateTime]::UtcNow.AddSeconds(100);$ready=$false;$before=$null;$latest=$null
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server) -and $null -ne (Get-OnlineOwnedProcess $ClientOne)) 'results_survivors_exited_during_reconnect'
        $latest=Read-OnlineReport $Server -AllowMissing;$survivor=Read-OnlineReport $ClientOne -AllowMissing
        $me=Api-Online GET '/v1/me' $null $readerTokens.client2
        if($latest -and @($latest.profiles).Count -eq 1 -and $me.leaseActive -eq $false -and (Test-OnlineCheck $latest 'server_results_retry_member_reserved')){
            if($null -eq $before){
                $before=Read-OnlineCanonical 'client2';$comparison=Assert-OnlineCanonical $before $Interrupted 'client2' $true
                Write-OnlineJson (Join-Path (Join-Path $runFolder 'register') 'client2-results-disconnect-canonical.json') $before
            }
            if((Test-OnlineCheck $latest 'server_results_online_member_only_reward') -and (Test-OnlineCheck $survivor 'results_solo_kill_saved')){$ready=$true;break}
        }
        Start-Sleep -Milliseconds 350
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online ($ready -and $null -ne $before) 'results_disconnect_solo_kill_or_lease_timeout'
    Require-OnlineFields $latest @('serverResults') 'results_server_interruption_missing'
    Require-OnlineFields $latest.serverResults @('retryBefore','retryDisconnected','retrySoloKill') 'results_interruption_snapshots_missing'
    $after=Read-OnlineCanonical 'client2';$afterComparison=Assert-OnlineCanonical $after $Interrupted 'client2' $true
    Assert-Online (Equal-Online $before $after) 'results_offline_character_received_or_lost_data'
    Write-OnlineJson (Join-Path (Join-Path $runFolder 'register') 'client2-results-after-solo-kill-canonical.json') $after
    $value=$Interrupted.partyResults;$entry=$accounts.client2;$context=Join-Path $privateFolder 'register-resume-context.private.json'
    Write-OnlineJson $context @{mode='login';scenario='party-results';offlineFailure=$false;resumeExpedition=$true;accounts=@{client2=@{name=$entry.name;password=$entry.password;initialClass=$entry.initialClass;expected=@{playerId=$after.me.playerId;profileHash=$after.me.profileHash;partyId=$value.partyId;clearedExpeditionId=$value.clearedExpeditionId;retryExpeditionId=$value.retryExpeditionId;completionId=$value.clearResult.completionId;originalClearResult=$value.clearResult;interruptedCheckpoint=$Interrupted.checkpoint}}}}
    $resumed=Start-OnlineValidationProcess 'client2' 'register-resume' $context
    $summary['interruption']=[ordered]@{status='reconnecting';originalCheckpoint=$Interrupted.checkpoint;originalPartyResults=$value;canonicalAfterDisconnect=$comparison;canonicalAfterSoloKill=$afterComparison;offlineCanonicalUnchanged=$true;serverSurvived=$true;clientOneSurvived=$true;serverControllersAfterDisconnect=1;reservedPartyMembers=2;clientTwoLeaseReleased=$true;resumeProcessId=$resumed.pid;resumeReportDirectory='register-resume'}
    return $resumed
}
function Resume-OnlineInterruptedClient($Server,$ClientOne,$Interrupted){
    $deadline=[DateTime]::UtcNow.AddSeconds(180);$ready=$false;$latest=$null
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server) -and $null -ne (Get-OnlineOwnedProcess $ClientOne)) 'survivors_exited_during_midrun_drain'
        $latest=Read-OnlineReport $Server -AllowMissing
        $me=Api-Online GET '/v1/me' $null $readerTokens.client2
        if($latest -and @($latest.profiles).Count -eq 1 -and $me.leaseActive -eq $false -and $null -ne $latest.PSObject.Properties['serverMidSolo']){$ready=$true;break}
        Start-Sleep -Milliseconds 350
    }while([DateTime]::UtcNow -lt $deadline)
    Assert-Online $ready 'midrun_disconnect_or_lease_release_timeout'
    Require-OnlineFields $latest @('serverMidBefore','serverMidSolo') 'server_midrun_before_or_solo_missing'
    $canonical=Read-OnlineCanonical 'client2';$comparison=Assert-OnlineCanonical $canonical $Interrupted 'client2' $true
    $midPath=Join-Path (Join-Path $runFolder 'register') 'client2-midrun-canonical.json';Write-OnlineJson $midPath $canonical
    $entry=$accounts.client2
    $context=Join-Path $privateFolder 'register-resume-context.private.json'
    Write-OnlineJson $context @{mode='login';scenario='reconnect';offlineFailure=$false;resumeExpedition=$true;accounts=@{client2=@{name=$entry.name;password=$entry.password;initialClass=$entry.initialClass;expected=@{playerId=$canonical.me.playerId;profileHash=$canonical.me.profileHash;encounterSerial=$Interrupted.midrun.encounterSerial}}}}
    $resumed=Start-OnlineValidationProcess 'client2' 'register-resume' $context
    $summary['interruption']=[ordered]@{status='reconnecting';originalCheckpoint=$Interrupted.checkpoint;originalMidrun=$Interrupted.midrun;canonicalAfterDisconnect=$comparison;serverSurvived=$true;clientOneSurvived=$true;serverControllersAfterDisconnect=1;clientTwoLeaseReleased=$true;resumeProcessId=$resumed.pid;resumeReportDirectory='register-resume'}
    return $resumed
}
function Assert-OnlineIsolationServer($ServerReport,$ClientReports){
    Require-OnlineFields $ServerReport @('serverIsolation') 'isolation_server_report_missing'
    $isolation=$ServerReport.serverIsolation
    Require-OnlineFields $isolation @('baseline','afterAKill','duringARecovery','afterAReturn','afterBKill','actualCrossDamageGuardAttempted') 'isolation_server_snapshots_missing'
    Assert-Online ($isolation.actualCrossDamageGuardAttempted -eq $false) 'isolation_actual_guard_scope_misrepresented'
    foreach($name in @('server_isolation_distinct_instances','server_isolation_a_only_reward','server_isolation_a_recovery_only','server_isolation_a_return_b_preserved','server_isolation_b_reward_a_preserved')){Assert-Online (Test-OnlineCheck $ServerReport $name) 'isolation_server_event_missing'}
    $base=$isolation.baseline;$killed=$isolation.afterAKill;$recovering=$isolation.duringARecovery;$returned=$isolation.afterAReturn;$final=$isolation.afterBKill
    foreach($view in @($base,$killed,$recovering,$returned,$final)){
        Require-OnlineFields $view @('a','b','activeInstances') 'isolation_snapshot_incomplete'
        foreach($row in @($view.a,$view.b)){
            Require-OnlineFields $row @('playerKey','partyId','expeditionId','ticketPartyId','ticketExpeditionId','ticketMembers','region','difficulty','wave','defeated','required','recovering','recoveryCount','encounterSerial','worldOrigin','enemies','profile','saved','safe','alive','inRegion','downedCount','boundEnemyActors') 'isolation_snapshot_row_incomplete'
            Assert-Online (@($row.worldOrigin).Count -eq 3 -and $row.profile.playerId -cin @($ClientReports.client1.checkpoint.playerId,$ClientReports.client2.checkpoint.playerId)) 'isolation_row_identity_invalid'
            foreach($enemy in $row.enemies){Assert-Online ([string]$enemy.id -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $enemy.id -cne '00000000-0000-0000-0000-000000000000' -and $enemy.expeditionId -ceq $row.expeditionId -and $enemy.health -gt 0) 'isolation_enemy_uuid_or_owner_invalid'}
            Assert-Online (@($row.enemies|ForEach-Object id|Select-Object -Unique).Count -eq @($row.enemies).Count) 'isolation_enemy_identity_duplicated'
        }
    }
    $one=$ClientReports.client1.partyIsolation;$two=$ClientReports.client2.partyIsolation
    Assert-Online ($one.partyId -cne $two.partyId -and $one.expeditionId -cne $two.expeditionId -and $one.selfKey -cne $two.selfKey -and $base.activeInstances -eq 2 -and -not(Equal-Online $one.worldOrigin $two.worldOrigin)) 'isolation_instances_not_distinct'
    foreach($pair in @(@($base.a,$one,$ClientReports.client1),@($base.b,$two,$ClientReports.client2))){
        $row=$pair[0];$client=$pair[1];$report=$pair[2]
        Assert-Online ($row.partyId -ceq $client.partyId -and $row.expeditionId -ceq $client.expeditionId -and $row.ticketPartyId -ceq $client.partyId -and $row.ticketExpeditionId -ceq $client.expeditionId -and @($row.ticketMembers).Count -eq 1 -and $row.ticketMembers[0] -ceq $client.selfKey -and $row.playerKey -ceq $client.selfKey -and $row.profile.playerId -ceq $report.checkpoint.playerId -and (Equal-Online @($row.worldOrigin) @($client.worldOrigin))) 'isolation_ticket_or_client_scope_mismatch'
        Assert-Online ($row.region -eq 1 -and $row.difficulty -eq 0 -and $row.wave -eq 1 -and $row.defeated -eq 0 -and $row.required -eq 3 -and $row.recovering -eq $false -and $row.safe -eq $true -and $row.saved -eq $true -and @($row.enemies).Count -eq 3 -and $row.profile.wallet.gold -eq 135 -and $row.profile.wallet.ore -eq 7 -and $row.profile.level -eq 2 -and $row.profile.experience -eq 105 -and $row.profile.bagCount -eq 5) 'isolation_entry_baseline_invalid'
    }
    # 다른 파티의 모든 영구 게임 값과 적·진행 정본을 직접 대조한다.
    $stableRows=@('playerKey','partyId','expeditionId','ticketPartyId','ticketExpeditionId','ticketMembers','region','difficulty','wave','defeated','required','recovering','recoveryCount','encounterSerial','worldOrigin','enemies')
    $persistent=@('playerId','heroClass','level','experience','classTier','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId','forestQuest','dungeonQuest','dungeonCompletions')
    foreach($after in @($killed.b,$recovering.b,$returned.b)){
        foreach($field in $stableRows){Assert-Online (Equal-Online $base.b.$field $after.$field) 'isolation_b_encounter_changed_by_a'}
        foreach($field in $persistent){Assert-Online (Equal-Online $base.b.profile.$field $after.profile.$field) 'isolation_b_profile_changed_by_a'}
    }
    foreach($field in $stableRows){Assert-Online (Equal-Online $returned.a.$field $final.a.$field) 'isolation_a_town_state_changed_by_b'}
    foreach($field in $persistent){Assert-Online (Equal-Online $returned.a.profile.$field $final.a.profile.$field) 'isolation_a_profile_changed_by_b'}
    Assert-Online ($killed.a.defeated -eq 1 -and @($killed.a.enemies).Count -eq 2 -and $killed.a.saved -eq $true -and $killed.a.profile.wallet.gold -eq 153 -and $killed.a.profile.wallet.ore -eq 8 -and $killed.a.profile.experience -eq 140) 'isolation_a_first_kill_invalid'
    Assert-Online ($recovering.a.recovering -eq $true -and $recovering.a.downedCount -gt $base.a.downedCount -and $recovering.b.recovering -eq $false -and $returned.a.region -eq 0 -and $returned.a.alive -eq $true -and $returned.a.inRegion -eq $true -and $returned.a.saved -eq $true -and $returned.a.expeditionId -ceq '00000000-0000-0000-0000-000000000000' -and $returned.a.boundEnemyActors -eq 0 -and $returned.activeInstances -eq 1) 'isolation_own_wipe_or_return_not_isolated'
    Assert-Online ($final.b.defeated -eq 1 -and @($final.b.enemies).Count -eq 2 -and $final.b.safe -eq $true -and $final.b.saved -eq $true -and $final.b.profile.wallet.gold -eq 153 -and $final.b.profile.wallet.ore -eq 8 -and $final.b.profile.experience -eq 140 -and $final.activeInstances -eq 1) 'isolation_b_independent_progress_missing'
    foreach($row in @($final.a,$final.b)){Assert-Online ($row.profile.level -eq 2 -and $row.profile.bagCount -eq 5 -and $row.profile.forestQuest -eq 3 -and $row.profile.dungeonQuest -eq 1 -and $row.profile.dungeonCompletions -eq 0) 'isolation_final_profile_scope_invalid'}
    $summary['partyIsolation']=[ordered]@{status='passed';twoSoloPartiesAndSeparateArenas=$true;exactOwnTicketsAndEnemyIds=$true;aKillOnlyRewardsA=$true;aNormalWipeAndOwnRecovery=$true;bUnchangedThroughAKillWipeAndReturn=$true;bCanContinueAndKillOwnEnemy=$true;bKillDoesNotRewardA=$true;actualCrossDamageGuardAttempted=$false;crashReconnectPerformed=$false;partyIds=@($one.partyId,$two.partyId);expeditionIds=@($one.expeditionId,$two.expeditionId)}
}
# 결과 확인은 서버가 이미 지급한 내역을 읽기만 하며 장비의 실제 영구 식별자까지 비교한다.
# 숫자 문자열/bool을 실제 JSON 숫자로 오인하지 않는다. / JSON型を厳密に読む。
function Assert-Unit2Number($Value,[bool]$Integer=$false){
    Assert-Online ($null -ne $Value -and $Value.GetType() -in @([byte],[short],[int],[long],[float],[double],[decimal]) -and [double]::IsFinite([double]$Value)) 'town_number_type_invalid'
    if($Integer){Assert-Online ([math]::Truncate([double]$Value) -eq [double]$Value -and [math]::Abs([double]$Value) -le 9007199254740991) 'town_integer_invalid'}
}
# 좌표와 속도는 유한 숫자 세 개로만 인정한다.
function Assert-Unit2Vector($Value){
    Assert-Online ($Value -is [array] -and $Value.Count -eq 3) 'town_vector_shape_invalid'
    foreach($entry in $Value){Assert-Unit2Number $entry}
}
# 같은 월드의 실제 관측 좌표 사이 거리를 계산한다.
function Unit2-Distance($A,$B){
    Assert-Unit2Vector $A;Assert-Unit2Vector $B
    return [math]::Sqrt([math]::Pow($A[0]-$B[0],2)+[math]::Pow($A[1]-$B[1],2)+[math]::Pow($A[2]-$B[2],2))
}
# 비어 있거나 정규 형태가 아닌 상관 식별자는 거절한다.
function Assert-Unit2Guid($Value){Assert-Online ($Value -is [string] -and $Value -cmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' -and $Value -cne '00000000-0000-0000-0000-000000000000') 'town_guid_invalid'}

# 각 서버 이벤트의 순간 상태를 독립 기준본과 비교한다. nonce 원문은 열지 않는다.
function Assert-Unit2Snapshot($Before,$After,[bool]$OwnerMoved){
    foreach($key in @('worldId','worldName','gameModeId','gameStateId','fieldScopeId','field','livingFieldEnemies','streamingLevelCount','newTownGeometryReady','newTownLoadedLevels')){
        Require-OnlineFields $Before @($key) 'town_baseline_field_missing';Require-OnlineFields $After @($key) 'town_snapshot_field_missing'
        Assert-Online (Equal-Online $Before.$key $After.$key) 'town_field_or_world_changed'
    }
    Assert-Online ($After.newTownLoadedLevels -eq 0 -and @($After.newTownGeometryReady).Count -eq 4) 'town_new_content_loaded'
    foreach($ready in $After.newTownGeometryReady){Assert-Online ($ready -is [bool] -and -not $ready) 'town_unapproved_geometry_ready'}
    foreach($slot in @('a','b')){
        $old=$Before.$slot;$current=$After.$slot
        foreach($key in @('economy','publicPlayerKey','controllerId','playerStateId','pawnId','mana','maxMana','alive','combat','worldEpoch','ownerConnectionGeneration','connectionNonceDigest','physicalTownId','physicalFloorId','physicalAreaKind','physicalCatalogVersion','physicalGeometryReady','physicalConnectorId','potionCooldownEnd')){
            Require-OnlineFields $old @($key) 'town_baseline_member_missing';Require-OnlineFields $current @($key) 'town_snapshot_member_missing'
            Assert-Online (Equal-Online $old.$key $current.$key) 'town_owner_or_observer_state_changed'
        }
        Assert-Online ($current.alive -is [bool] -and $current.alive -and $current.onGround -is [bool] -and $current.onGround -and $current.privateExpeditionId -ceq '00000000-0000-0000-0000-000000000000') 'town_member_not_alive_grounded_public'
        Assert-Online ($current.connectionNonceDigest -cmatch '^[0-9a-f]{64}$') 'town_nonce_digest_invalid'
        if($slot -ceq 'b' -or -not $OwnerMoved){
            foreach($key in @('presenceRevision','safeCheckpointRevision','safeAnchor','safeSpawnLocation','safeSpawnRotation','publicScopeId')){Assert-Online (Equal-Online $old.$key $current.$key) 'town_failed_scope_or_checkpoint_changed'}
            Assert-Online ((Unit2-Distance $old.position $current.position) -le 1) 'town_stationary_member_moved'
            Assert-Unit2Vector $old.rotation;Assert-Unit2Vector $current.rotation
            for($i=0;$i -lt 3;$i++){$angle=[math]::Abs((($old.rotation[$i]-$current.rotation[$i]+540)%360)-180);Assert-Online ($angle -le .5) 'town_stationary_member_rotated'}
        }
        if($slot -ceq 'b'){
            foreach($key in @('noticeSerial','lastNotice')){Assert-Online (Equal-Online $old.$key $current.$key) 'town_observer_notice_leaked'}
        }
    }
    # 一人の名簿も配列として保持し、厳密比較の型を変えない。
    $expectedRoster=@(if($OwnerMoved){[string]$Before.b.publicPlayerKey}else{$Before.publicRoster})
    Assert-Online (Equal-Online @($After.publicRoster) $expectedRoster) 'town_public_roster_changed_incorrectly'
    if($OwnerMoved){
        Assert-Online ($After.a.presenceRevision -gt $Before.a.presenceRevision -and $After.a.safeCheckpointRevision -gt $Before.a.safeCheckpointRevision -and $After.a.publicScopeId -ceq '00000000-0000-0000-0000-000000000000' -and $After.b.publicScopeId -ceq $Before.b.publicScopeId) 'town_success_scope_or_checkpoint_invalid'
    }
}

# 各AttemptIdを実際の権威応答と結び、欠けた証拠をboolで代用しない。
function Assert-OnlineUnit2($ServerReport,$ClientReport,$OriginalPeer){
    Require-OnlineFields $ServerReport @('serverTownTravel') 'town_server_evidence_missing'
    Require-OnlineFields $ClientReport @('townTravel') 'town_owner_evidence_missing'
    Require-OnlineFields $OriginalPeer @('townTravel') 'town_observer_evidence_missing'
    $server=$ServerReport.serverTownTravel;$owner=$ClientReport.townTravel;$observer=$OriginalPeer.townTravel
    Require-OnlineFields $server @('before','after','committed','events','completed') 'town_server_evidence_incomplete'
    Assert-Online ($server.completed -is [bool] -and $server.completed -and $owner.completed -is [bool] -and $owner.completed -and $observer.completed -is [bool] -and $observer.completed) 'town_workflow_not_completed'
    $before=$server.before;$after=$server.after
    Assert-Online ($before.worldName -ceq '/Game/Maps/ForestClearing' -and $before.a.worldEpoch -ceq $before.b.worldEpoch) 'town_actual_world_identity_mismatch'
    Assert-Online ($before.field.objectiveComplete -is [bool] -and $before.field.objectiveComplete -and $before.field.defeatedEnemies -eq 3 -and $before.field.requiredEnemies -eq 3) 'town_field_three_kills_not_complete'
    Assert-Online (@($before.publicRoster).Count -eq 2 -and @($before.livingFieldEnemies).Count -eq 0 -and $before.a.publicPlayerKey -cne $before.b.publicPlayerKey -and $before.a.economy.playerId -cne $before.b.economy.playerId) 'town_ready_baseline_not_exact_two'
    foreach($slot in @('a','b')){
        $member=$before.$slot
        Assert-Online ($member.economy.forestQuest -eq 2 -and $member.economy.questKills -eq 3 -and $member.economy.ready -is [bool] -and $member.economy.ready -and $member.economy.savePending -is [bool] -and -not $member.economy.savePending -and $member.mana -eq $member.maxMana -and $member.onGround -is [bool] -and $member.onGround -and $member.alive -is [bool] -and $member.alive) 'town_ready_baseline_invalid'
        Assert-Unit2Guid $member.publicScopeId;Assert-Unit2Number $member.ownerConnectionGeneration $true
        Assert-Unit2Guid $member.worldEpoch
        Assert-Online ($member.physicalTownId -eq 1 -and $member.physicalFloorId -ceq 'Ground' -and $member.physicalAreaKind -eq 1 -and $member.physicalCatalogVersion -eq 1 -and $member.physicalGeometryReady -is [bool] -and $member.physicalGeometryReady) 'town_departure_physical_context_invalid'
    }
    Assert-Online ($before.a.publicScopeId -ceq $before.b.publicScopeId -and $before.a.publicScopeId -ceq $before.fieldScopeId) 'town_baseline_public_generation_differs'
    $events=@($server.events);$commits=@($events|Where-Object kind -CEQ 'Committed')
    Assert-Online ($events.Count -gt 20 -and $commits.Count -eq 1) 'town_commit_count_invalid'
    $moved=$false;$sequence=0
    foreach($event in $events){
        Assert-Unit2Number $event.sequence $true;Assert-Unit2Number $event.serverTime
        Assert-Online ($event.sequence -gt $sequence) 'town_authority_event_order_invalid';$sequence=$event.sequence
        if($event.kind -ceq 'Committed'){$moved=$true}
        Assert-Unit2Snapshot $before $event.snapshot $moved
    }
    Assert-Unit2Snapshot $before $after $true
    $labels=@('sunbrook','starbough','snowbloom','eldergrove','unknownAnchor','staleCatalog','loadedFalse','cancelled','wrongChallenge','wrongConnection','wrongGeneration','success')
    $attempts=@($owner.attempts)
    Assert-Online ($attempts.Count -eq 12 -and @($observer.attempts).Count -eq 0) 'town_attempt_count_invalid'
    $allIds=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $transactionIds=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$previousTravelGeneration=0
    $matched=@()
    for($index=0;$index -lt $attempts.Count;$index++){
        $attempt=$attempts[$index];$request=$attempt.request
        Assert-Online ($attempt.label -ceq $labels[$index] -and $attempt.completed -is [bool] -and $attempt.completed) 'town_attempt_incomplete'
        Assert-Unit2Guid $request.requestId;Assert-Online ($allIds.Add($request.requestId)) 'town_request_id_reused'
        Assert-Online ((@($request.PSObject.Properties.Name|Sort-Object) -join ',') -ceq 'anchorId,catalogVersion,requestId,targetTownId') 'town_request_has_client_authority_fields'
        Assert-Unit2Number $request.targetTownId $true;Assert-Unit2Number $request.catalogVersion $true
        Assert-Online ($request.targetTownId -eq $(if($index -lt 4){$index+2}else{1}) -and $request.anchorId -ceq $(if($index -eq 4){'Unknown.Unit2'}else{'Entry'}) -and $request.catalogVersion -eq $(if($index -eq 5){2}else{1})) 'town_logical_target_mismatch'
        $responses=@($attempt.responses)
        foreach($response in $responses){
            foreach($key in @('eventSequence','clientSequence','generation')){Assert-Unit2Number $response.$key $true}
            Assert-Unit2Guid $response.attemptId;Assert-Online ($response.requestId -ceq $request.requestId) 'town_client_response_request_mismatch'
            if($response.attemptId -cne $request.requestId){Assert-Online ($allIds.Add($response.attemptId)) 'town_attempt_id_reused'}
            $authority=@($events|Where-Object {$null -ne $_.PSObject.Properties['response'] -and $_.response.attemptId -ceq $response.attemptId -and $_.response.requestId -ceq $request.requestId})
            Assert-Online ($authority.Count -eq 1) 'town_authority_response_correlation_missing'
            foreach($property in $authority[0].response.PSObject.Properties){Assert-Online (Equal-Online $property.Value $response.($property.Name)) 'town_client_authority_response_mismatch'}
            if($index -ge 6){Assert-Online ($authority[0].serverTime -lt $attempt.prepared.expiresAtServerTime) 'town_response_expired_instead_of_exact_rejection'}
        }
        $find={param($AttemptKey,$ExpectedErrorCode) $rows=@($responses|Where-Object {$_.attemptId -ceq $AttemptKey -and $_.error -ceq $ExpectedErrorCode});Assert-Online ($rows.Count -eq 1) 'town_exact_attempt_response_missing';return $rows[0]}
        if($index -lt 6){
            $expected=if($index -lt 4){'DestinationNotReady'}elseif($index -eq 4){'UnknownAnchor'}else{'StaleCatalog'}
            $row=& $find $request.requestId $expected
            Assert-Online ($null -eq $attempt.PSObject.Properties['prepared']) 'town_rejected_destination_prepared'
        }else{
            $prepared=$attempt.prepared;Assert-Unit2Guid $prepared.transactionId;Assert-Unit2Number $prepared.generation $true
            Assert-Online ($transactionIds.Add($prepared.transactionId) -and $prepared.generation -gt $previousTravelGeneration) 'town_fresh_transaction_or_generation_reused';$previousTravelGeneration=$prepared.generation
            Assert-Unit2Number $prepared.expiresAtServerTime;Assert-Unit2Number $prepared.ownerConnectionGeneration $true;Assert-Unit2Number $prepared.sourcePresenceRevision $true
            Assert-Online ($prepared.requestId -ceq $request.requestId -and $prepared.descriptorKind -ceq 'ExistingPersistent' -and $prepared.sourcePublicScopeId -ceq $before.fieldScopeId -and $prepared.connectionNonceDigest -ceq $before.a.connectionNonceDigest -and $prepared.challengeNonceDigest -cmatch '^[0-9a-f]{64}$') 'town_prepared_identity_invalid'
            Assert-Online ($prepared.sourcePresenceRevision -eq $before.a.presenceRevision -and $prepared.ownerConnectionGeneration -eq $before.a.ownerConnectionGeneration -and $prepared.sourceTownId -eq 1 -and $prepared.sourceFloorId -ceq 'Ground' -and $prepared.sourceCatalogVersion -eq 1 -and $prepared.sourceGeometryReady -is [bool] -and $prepared.sourceGeometryReady -and $prepared.targetAnchor.townId -eq 1 -and $prepared.targetAnchor.anchorId -ceq 'Entry' -and $prepared.targetAnchor.catalogVersion -eq 1) 'town_prepared_source_or_destination_mismatch'
            $authoritativePrepared=@($events|Where-Object {$_.requestId -ceq $request.requestId -and $null -ne $_.PSObject.Properties['prepared']})
            Assert-Online ($authoritativePrepared.Count -ge 1 -and (Equal-Online $prepared $authoritativePrepared[0].prepared)) 'town_prepared_server_client_mismatch'
            foreach($preparedEvent in $authoritativePrepared){Assert-Online (Equal-Online $prepared $preparedEvent.prepared) 'town_pending_tuple_or_original_ttl_changed'}
            if($index -lt 11){
                if($index -ne 7){
                    $expected=if($index -eq 6){'ClientLoadFailed'}elseif($index -eq 8){'WrongChallengeNonce'}elseif($index -eq 9){'WrongConnectionNonce'}else{'WrongGeneration'}
                    $bad=& $find $attempt.malformedAttemptId $expected
                    Assert-Online ($bad.operation -ceq 'Acknowledge') 'town_malformed_not_actual_ack'
                    Assert-Online ($bad.transactionId -ceq $prepared.transactionId -and $bad.generation -eq $prepared.generation -and $bad.state -ceq $(if($index -eq 6){'Cancelled'}else{'AwaitClientGeometry'})) 'town_malformed_ack_changed_pending_or_terminal_state'
                    $sent=$attempt.sentAck
                    Assert-Unit2Number $sent.generation $true
                    Assert-Online ($sent.loaded -is [bool] -and $sent.loaded -eq ($index -ne 6)) 'town_loaded_flag_invalid'
                    Assert-Online (($sent.generation -eq $prepared.generation) -eq ($index -ne 10) -and ($sent.connectionNonceDigest -ceq $prepared.connectionNonceDigest) -eq ($index -ne 9) -and ($sent.challengeNonceDigest -ceq $prepared.challengeNonceDigest) -eq ($index -ne 8)) 'town_malformed_tuple_not_exact'
                }
                if($index -ne 6){
                    $cancel=& $find $attempt.cancelAttemptId 'CancelledByOwner'
                    Assert-Online ($cancel.operation -ceq 'Cancel' -and $cancel.state -ceq 'Cancelled') 'town_cancel_not_authoritative_terminal'
                    if($index -ge 8){Assert-Online ($bad.eventSequence -lt $cancel.eventSequence -and $bad.clientSequence -lt $cancel.clientSequence) 'town_malformed_cancel_order_invalid'}
                }
                $replay=& $find $attempt.replayAttemptId 'AlreadyCancelled'
                Assert-Online ($replay.operation -ceq 'Acknowledge' -and $replay.state -ceq 'Cancelled' -and $replay.transactionId -ceq $prepared.transactionId -and $replay.generation -eq $prepared.generation) 'town_old_ack_not_cancelled'
                $terminal=if($index -eq 6){$bad}else{$cancel}
                Assert-Online ($terminal.clientSequence -lt $replay.clientSequence -and $terminal.eventSequence -lt $replay.eventSequence -and $attempt.replayAttemptId -cne $terminal.attemptId) 'town_replay_before_cancel_response'
            }
        }
        $matched+=@([ordered]@{label=$attempt.label;requestId=$request.requestId;responseCount=$responses.Count;rawResponsesMatched=$true})
    }
    $success=$attempts[11];$commit=$owner.commitResponse.commit;$placement=$server.committed.placement;$arrival=$owner.arrival
    Assert-Online ($owner.commitResponse.error -ceq 'None' -and $owner.commitResponse.operation -ceq 'Acknowledge' -and $commit.commitCount -eq 1 -and $commit.transactionId -ceq $success.prepared.transactionId) 'town_legitimate_ack_missing'
    Assert-Online ($placement.capsuleClear -is [bool] -and $placement.capsuleClear -and $placement.walkingOnGround -is [bool] -and $placement.walkingOnGround -and $placement.floorBlocking -is [bool] -and $placement.floorBlocking -and $placement.floorWalkable -is [bool] -and $placement.floorWalkable -and $placement.floorPenetrating -is [bool] -and -not $placement.floorPenetrating) 'town_floor_or_capsule_invalid'
    Assert-Online ($placement.intendedSupport -is [bool] -and $placement.intendedSupport) 'town_floor_not_intended_geometry'
    foreach($key in @('capsuleRadius','capsuleHalfHeight','querySequence','queryServerTime','floorDistanceCm')){Assert-Unit2Number $placement.$key}
    Assert-Online ($placement.capsuleRadius -gt 0 -and $placement.capsuleHalfHeight -ge $placement.capsuleRadius -and $placement.querySequence -gt 0 -and $placement.floorDistanceCm -ge 0 -and $placement.floorDistanceCm -le 3 -and $placement.queryServerTime -le $server.committed.serverTime -and -not [string]::IsNullOrWhiteSpace($placement.supportName)) 'town_fresh_floor_evidence_invalid'
    Assert-Online ((Unit2-Distance $before.a.position $placement.acceptedPosition) -gt 2*$placement.capsuleRadius -and (Unit2-Distance $commit.acceptedPosition $placement.acceptedPosition) -le .01 -and (Unit2-Distance $after.a.position $placement.acceptedPosition) -le 1 -and (Unit2-Distance $after.a.safeSpawnLocation $placement.acceptedPosition) -le .01) 'town_accepted_transform_or_displacement_invalid'
    Assert-Online ($arrival.transactionId -ceq $commit.transactionId -and $arrival.generation -eq $commit.generation -and $arrival.samples -ge 3 -and $arrival.stableSeconds -ge .3 -and $arrival.planarDistanceCm -le 10 -and $arrival.verticalDistanceCm -le 10 -and $arrival.orderingFailed -is [bool] -and -not $arrival.orderingFailed -and $arrival.ownArrivalEventObserved -eq $true) 'town_own_arrival_not_correlated'
    $samples=@($arrival.observations)
    Assert-Online ($samples.Count -eq $arrival.samples -and $samples.Count -ge 3) 'town_arrival_raw_samples_missing'
    $lastTime=-1.0
    foreach($sample in $samples){
        foreach($key in @('clientTime','generation','presenceRevision','safeCheckpointRevision','ownerConnectionGeneration')){Assert-Unit2Number $sample.$key ($key -cne 'clientTime')}
        Assert-Online ($sample.clientTime -gt $lastTime -and $sample.transactionId -ceq $commit.transactionId -and $sample.generation -eq $commit.generation -and $sample.presenceRevision -eq $commit.presenceRevision -and $sample.safeCheckpointRevision -eq $commit.safeCheckpointRevision -and $sample.ownerConnectionGeneration -eq $before.a.ownerConnectionGeneration -and $sample.connectionNonceDigest -ceq $before.a.connectionNonceDigest -and $sample.onGround -is [bool] -and $sample.onGround) 'town_arrival_sample_identity_invalid'
        Assert-Unit2Vector $sample.position;Assert-Unit2Vector $sample.velocity
        $dx=$sample.position[0]-$commit.acceptedPosition[0];$dy=$sample.position[1]-$commit.acceptedPosition[1]
        Assert-Online ([math]::Sqrt($dx*$dx+$dy*$dy) -le 10 -and [math]::Abs($sample.position[2]-$commit.acceptedPosition[2]) -le 10 -and (Unit2-Distance $sample.velocity @(0,0,0)) -le 1) 'town_arrival_sample_not_settled'
        $lastTime=$sample.clientTime
    }
    Assert-Online ($samples[-1].clientTime-$samples[0].clientTime -ge .3 -and [math]::Abs(($samples[-1].clientTime-$samples[0].clientTime)-$arrival.stableSeconds) -le .001) 'town_arrival_stable_span_invalid'
    $progress=@($owner.progressEvents|Where-Object {$_.transactionId -ceq $commit.transactionId -and $_.emitter -ceq 'client'})
    $ownArrival=@($progress|Where-Object kind -CEQ 'OwnArrivalObserved');$unlock=@($progress|Where-Object kind -CEQ 'UiUnlocked');$release=@($progress|Where-Object kind -CEQ 'SourceLeaseReleased')
    Assert-Online ($ownArrival.Count -eq 1 -and $unlock.Count -eq 1 -and $release.Count -eq 1 -and $unlock[0].clientSequence -gt $ownArrival[0].clientSequence -and $unlock[0].clientSequence -gt $owner.commitResponse.clientSequence -and $release[0].clientSequence -gt $ownArrival[0].clientSequence -and $release[0].clientSequence -gt $owner.commitResponse.clientSequence) 'town_unlock_or_source_release_too_early'
    $duplicate=@($success.responses|Where-Object {$_.attemptId -ceq $success.duplicateAttemptId -and $_.error -ceq 'AlreadyCommitted'})
    Assert-Online ($duplicate.Count -eq 1 -and (Equal-Online $duplicate[0].commit $commit)) 'town_duplicate_changed_receipt'
    $confirmation=@($success.responses|Where-Object {$_.operation -ceq 'ConfirmArrival' -and $_.error -ceq 'None'})
    Assert-Online ($confirmation.Count -eq 1 -and $confirmation[0].ownArrivalConfirmed -is [bool] -and $confirmation[0].ownArrivalConfirmed -and (Equal-Online $confirmation[0].commit $commit) -and $confirmation[0].clientSequence -lt $unlock[0].clientSequence -and $confirmation[0].clientSequence -lt $release[0].clientSequence) 'town_server_source_lease_released_before_arrival_confirmation'
    $gates=[ordered]@{
        u2_same_owned_server_world=($before.worldId -eq $after.worldId -and $before.gameModeId -eq $after.gameModeId -and $before.gameStateId -eq $after.gameStateId)
        u2_ready_barrier_registered=$true;u2_logical_target_only=$true;u2_descriptor_kind=[string]$success.prepared.descriptorKind
        u2_four_unapproved_towns_rejected=$true;u2_unknown_anchor_rejected=$true;u2_stale_catalog_rejected=$true
        u2_loaded_false_rejected=$true;u2_cancel_source_preserved=$true;u2_cancelled_old_ack_rejected=$true
        u2_wrong_challenge_rejected=$true;u2_wrong_connection_nonce_rejected=$true;u2_wrong_generation_rejected=$true
        u2_failed_attempt_scope_unchanged=$true;u2_legitimate_ack_observed=$true;u2_server_floor_capsule_verified=$true
        u2_owner_nonzero_displacement=$true;u2_commit_correlated_once=$true;u2_client_own_arrival_correlated=$true
        u2_client_unlock_after_both=$true;u2_only_owner_scope_changed=$true;u2_observer_unchanged=$true
        u2_field_progress_unchanged=$true;u2_owner_economy_unchanged=$true;u2_checkpoint_memory_only=$true
        u2_duplicate_ack_no_second_commit=$true;u2_new_content_claim_limits=$true
    }
    return [ordered]@{status='running';scenario=$TownTravelScenario;gates=$gates;perAttemptChecks=$matched;authorityEventCount=$events.Count;authorityCommitCount=$commits.Count;descriptorKind=$success.prepared.descriptorKind;evidence=@{client1='register/client1.json';client2='register/client2-crash-checkpoint.json';server='register/server.json'};newTownGeometryReady=$false;newTownActualPlayClaimed=$false;newTownLoadedLevels=$after.newTownLoadedLevels;persistentMapUnloadClaimed=$false;anchorPersistenceAcrossRestartClaimed=$false}
}

function Assert-OnlineResultsReceipt($Value,$Checkpoint,[string]$Role,[string]$Key,[string]$Expedition,[int]$Outcome,[bool]$Cleared){
    Require-OnlineFields $Value @('expeditionId','partyId','recipientKey','region','difficulty','memberKeys','outcome','completionId','resultRevision','acknowledged','allGrantsCommitted','saveReady','requiresBackendCommit','retryExpeditionId','receipts','gold','ore','experience','rootCores','moonCrystals','itemCount') 'results_receipt_incomplete'
    Assert-Online ($Value.expeditionId -ceq $Expedition -and $Value.recipientKey -ceq $Key -and $Value.region -eq 1 -and $Value.difficulty -eq 0 -and @($Value.memberKeys).Count -eq 2 -and $Key -cin @($Value.memberKeys) -and $Value.outcome -eq $Outcome -and $Value.resultRevision -gt 0 -and $Value.saveReady -eq $true -and $Value.requiresBackendCommit -eq $true) 'results_receipt_owner_or_scope_invalid'
    if($Outcome -ne 0){Assert-Online ([string]$Value.completionId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $Value.completionId -cne '00000000-0000-0000-0000-000000000000') 'results_completion_id_invalid'}
    $extra=-not $Cleared -and $Role -ceq 'client1';$expectedCount=if($Cleared){7}elseif($extra){1}else{0}
    Assert-Online (@($Value.receipts).Count -eq $expectedCount) 'results_grant_count_mismatch'
    if($expectedCount){Assert-Online ($Value.allGrantsCommitted -eq $true) 'results_backend_grants_not_committed'}
    $ids=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$sequences=[Collections.Generic.HashSet[long]]::new();$itemIds=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $gold=0;$ore=0;$xp=0;$cores=0;$crystals=0;$items=0;$bosses=0
    foreach($receipt in $Value.receipts){
        Require-OnlineFields $receipt @('expeditionId','grantId','recipientKey','sequence','boss','gold','ore','experience','rootCores','moonCrystals','saveState','committedRevision','items') 'results_grant_fields_missing'
        Assert-Online ($receipt.expeditionId -ceq $Expedition -and $receipt.recipientKey -ceq $Key -and [string]$receipt.grantId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $receipt.grantId -cne '00000000-0000-0000-0000-000000000000' -and $ids.Add([string]$receipt.grantId) -and $receipt.sequence -gt 0 -and $sequences.Add([long]$receipt.sequence)) 'results_grant_identity_replayed_or_foreign'
        Assert-Online ($receipt.saveState -eq 2 -and $receipt.committedRevision -gt 0 -and $receipt.committedRevision -le $Checkpoint.acknowledgedRevision) 'results_grant_ack_revision_invalid'
        $bossGrant=$Cleared -and $receipt.boss -eq $true
        Assert-Online ($receipt.gold -eq $(if($bossGrant){140}else{18}) -and $receipt.ore -eq $(if($bossGrant){8}else{1}) -and $receipt.experience -eq $(if($bossGrant){250}else{35}) -and $receipt.rootCores -eq $(if($bossGrant){1}else{0}) -and $receipt.moonCrystals -eq 0 -and @($receipt.items).Count -eq $(if($bossGrant){1}else{0})) 'results_individual_enemy_grant_mismatch'
        $gold+=[int]$receipt.gold;$ore+=[int]$receipt.ore;$xp+=[int]$receipt.experience;$cores+=[int]$receipt.rootCores;$crystals+=[int]$receipt.moonCrystals
        if($receipt.boss -eq $true){++$bosses}
        foreach($item in $receipt.items){
            Assert-Online ($itemIds.Add([string]$item.instanceId)) 'results_boss_item_repeated'
            $actual=@($Checkpoint.items|Where-Object instanceId -CEQ $item.instanceId)
            Assert-Online ($actual.Count -eq 1) 'results_boss_item_not_in_personal_inventory'
            foreach($field in @('instanceId','localId','slot','heroClass','rarity','power','upgrade','rollVersion','quality','nominalPower','rollPercent','setId','durabilityVersion','durability','maxDurability','affixes')){Assert-Online (Equal-Online $item.$field $actual[0].$field) 'results_boss_item_snapshot_changed'}
            ++$items
        }
    }
    $expectedGold=if($Cleared){248}elseif($extra){18}else{0};$expectedOre=if($Cleared){14}elseif($extra){1}else{0};$expectedXp=if($Cleared){460}elseif($extra){35}else{0}
    Assert-Online ($gold -eq $expectedGold -and $ore -eq $expectedOre -and $xp -eq $expectedXp -and $cores -eq $(if($Cleared){1}else{0}) -and $crystals -eq 0 -and $items -eq $(if($Cleared){1}else{0}) -and $bosses -eq $(if($Cleared){1}else{0})) 'results_actual_receipt_delta_mismatch'
    foreach($pair in @(@('gold',$gold),@('ore',$ore),@('experience',$xp),@('rootCores',$cores),@('moonCrystals',$crystals),@('itemCount',$items))){Assert-Online ($Value.($pair[0]) -eq $pair[1]) 'results_declared_totals_differ_from_grants'}
}
function Assert-OnlineResultsSameGrant($Before,$After){
    foreach($field in @('expeditionId','partyId','recipientKey','region','difficulty','preparationRevision','memberKeys','outcome','completionId','requiresBackendCommit','receipts')){Assert-Online (Equal-Online $Before.$field $After.$field) 'results_reopen_or_restore_changed_grant'}
}
function Assert-OnlineResultsClient($Report,[string]$Role,[string]$Phase,[bool]$Crash){
    Require-OnlineFields $Report @('partyResults') 'results_client_report_missing';$value=$Report.partyResults
    Require-OnlineFields $value @('selfKey','partyId','clearedExpeditionId','retryExpeditionId','resumedProcess','serverRestartReceiptPersistenceClaimed') 'results_client_scope_missing'
    Assert-Online ($value.serverRestartReceiptPersistenceClaimed -eq $false -and $value.clearedExpeditionId -cne $value.retryExpeditionId) 'results_retry_or_persistence_scope_invalid'
    foreach($id in @($value.partyId,$value.clearedExpeditionId,$value.retryExpeditionId)){Assert-Online ([string]$id -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $id -cne '00000000-0000-0000-0000-000000000000') 'results_client_uuid_invalid'}
    if($Phase -ceq 'register'){
        Require-OnlineFields $value @('forestCheckpoint','firstClearProfile','clearResult','reopenedClearResult','acknowledgedClearResult','retryBeforeProfile','retryPreparationRevision') 'results_initial_evidence_missing'
        foreach($name in @('both_forest_quests_active','forest_reward_equipped','results_initial_exact_two_arrived','results_second_wave_played','results_boss_played','results_clear_receipts_saved','results_opened_own_result','results_reopened_without_grant','results_acknowledged_without_grant','results_retry_resets_all_consent','results_retry_unready_rejected','results_retry_fresh_both_ready','results_retry_new_uuid_two_arrived','results_retry_safe_before_disconnect')){Assert-Online (Test-OnlineCheck $Report $name) 'results_normal_ui_flow_missing'}
        Assert-Online ($value.forestCheckpoint.wallet.gold -eq 135 -and $value.forestCheckpoint.wallet.ore -eq 7 -and $value.forestCheckpoint.level -eq 2 -and $value.forestCheckpoint.experience -eq 105 -and $value.forestCheckpoint.bagCount -eq 5) 'results_normal_forest_baseline_invalid'
        Assert-OnlineCheckpoint $value.firstClearProfile $Role $true
        Assert-OnlineCheckpoint $value.retryBeforeProfile $Role $true
        Assert-OnlineResultsReceipt $value.clearResult $value.firstClearProfile $Role $value.selfKey $value.clearedExpeditionId 1 $true
        Assert-OnlineResultsSameGrant $value.clearResult $value.reopenedClearResult
        Assert-OnlineResultsSameGrant $value.clearResult $value.acknowledgedClearResult
        Assert-Online ($value.acknowledgedClearResult.acknowledged -eq $true -and $value.retryPreparationRevision -gt $value.clearResult.preparationRevision) 'results_ack_or_fresh_consent_invalid'
    }else{
        Require-OnlineFields $value @('originalClearResult','interruptedCheckpoint','restoredClearResult','restoredRetryResult','restoredProfile') 'results_restored_evidence_missing'
        foreach($name in @('results_original_receipt_restored','results_no_offline_reward_fullprofile_preserved','results_same_character_new_uuid_restored','results_peer_server_restoration_observed')){Assert-Online (Test-OnlineCheck $Report $name) 'results_restoration_flow_missing'}
        Assert-Online ($value.resumedProcess -eq $true -and $Role -ceq 'client2') 'results_resume_process_role_invalid'
        Assert-OnlineResultsSameGrant $value.originalClearResult $value.restoredClearResult
        Assert-OnlineResultsReceipt $value.restoredClearResult $value.restoredProfile $Role $value.selfKey $value.clearedExpeditionId 1 $true
        Assert-OnlineResultsReceipt $value.restoredRetryResult $value.restoredProfile $Role $value.selfKey $value.retryExpeditionId 0 $false
        foreach($field in @('playerId','heroClass','level','experience','classTier','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId','forestQuest','dungeonQuest','dungeonCompletions')){Assert-Online (Equal-Online $value.interruptedCheckpoint.$field $value.restoredProfile.$field) 'results_interrupted_fullprofile_changed'}
    }
    if($Crash){
        Assert-Online ($Role -ceq 'client2' -and $Phase -ceq 'register' -and $value.status -ceq 'crash-ready') 'results_planned_interruption_wrong_phase'
        Assert-Online (Equal-Online $Report.checkpoint $value.retryBeforeProfile) 'results_crash_checkpoint_not_original_profile'
        return
    }
    Require-OnlineFields $value @('abandonedResult','acknowledgedAbandonedResult','finalProfile') 'results_terminal_evidence_missing'
    foreach($name in @('results_peer_server_restoration_observed','results_explicit_abandon_consent_sent','results_consented_return_town','results_final_saved','results_final_peer_server_handshake')){Assert-Online (Test-OnlineCheck $Report $name) 'results_terminal_ui_flow_missing'}
    if($Role -ceq 'client1'){Assert-Online (Test-OnlineCheck $Report 'results_solo_kill_saved') 'results_surviving_member_kill_missing'}
    Assert-Online ($value.status -ceq 'passed') 'results_client_not_passed'
    Assert-OnlineResultsReceipt $value.abandonedResult $Report.checkpoint $Role $value.selfKey $value.retryExpeditionId 3 $false
    Assert-OnlineResultsSameGrant $value.abandonedResult $value.acknowledgedAbandonedResult
    Assert-Online ($value.acknowledgedAbandonedResult.acknowledged -eq $true -and (Equal-Online $value.finalProfile $Report.checkpoint)) 'results_terminal_checkpoint_or_ack_changed'
}

# Unit4 opt-in 보고서는 일반 UI와 서버 authority/ACK를 서로 다른 증거로 검증한다.
function Get-OnlineTownServiceLabels{
    return @('guide-out-of-range-reject','guide-open','forest-accept','forest-three-kills','guide-turnin-open','forest-turnin','shop-wrong-service-reject','shop-open','shop-buy-one','forge-wrong-service-reject','forge-open','forge-upgrade-one','portal-open')
}
function Get-OnlineTownServiceOutcome([string]$Label){
    if($Label -cin @('guide-out-of-range-reject','shop-wrong-service-reject','forge-wrong-service-reject')){return 'authority-rejected'}
    if($Label -cin @('guide-open','guide-turnin-open','shop-open','forge-open','portal-open')){return 'opened-ui'}
    if($Label -ceq 'forest-three-kills'){return 'shared-forest-ack'}
    return 'acknowledged-save'
}
function Assert-OnlineTownServiceInteger($Value,[long]$Minimum,[string]$Code){
    Assert-Online ($Value -isnot [bool] -and ($Value -is [int] -or $Value -is [long] -or $Value -is [double]) -and
        -not [double]::IsNaN([double]$Value) -and -not [double]::IsInfinity([double]$Value) -and
        [double]$Value -eq [math]::Truncate([double]$Value) -and [double]$Value -ge $Minimum -and
        [double]$Value -le 9007199254740991) $Code
}
function Assert-OnlineTownServiceProfile($Profile,[string]$PlayerId){
    Require-OnlineFields $Profile @('playerId','revision','acknowledgedRevision','profileHash','ready','savePending','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId','forestQuest','forestKills','moonwellQuest','moonwellCompletions','storyChapter','storyObjectMask','storySealMask') 'services_full_profile_incomplete'
    Assert-Online ([string]$Profile.playerId -ceq $PlayerId -and $PlayerId -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and
        [string]$Profile.profileHash -cmatch '^[a-f0-9]{64}$' -and $Profile.ready -is [bool] -and $Profile.ready -eq $true -and
        $Profile.savePending -is [bool] -and $Profile.savePending -eq $false) 'services_profile_not_saved_ready'
    Assert-OnlineTownServiceInteger $Profile.revision 1 'services_profile_revision_invalid'
    Assert-OnlineTownServiceInteger $Profile.acknowledgedRevision 1 'services_profile_ack_invalid'
    Assert-Online ($Profile.revision -eq $Profile.acknowledgedRevision) 'services_profile_not_exact_ack'
    Require-OnlineFields $Profile.wallet @('gold','ore','essence','rootCores','moonCrystals','healthPotions','manaPotions') 'services_wallet_incomplete'
}
function Assert-OnlineTownServicesNoRawNonce($Value){
    if($null -eq $Value -or $Value -is [string] -or $Value -is [ValueType]){return}
    if($Value -is [array]){foreach($item in $Value){Assert-OnlineTownServicesNoRawNonce $item};return}
    foreach($property in $Value.PSObject.Properties){
        Assert-Online ($property.Name -cnotmatch '(?i)^(nonce|rawNonce|connectionNonce)$') 'services_raw_nonce_in_public_report'
        Assert-OnlineTownServicesNoRawNonce $property.Value
    }
}
function Assert-OnlineTownServicesClient($Report,[string]$Role){
    Require-OnlineFields $Report @('townServiceCheckpoints','processId','checkpoint') 'services_client_report_missing'
    $rows=@($Report.townServiceCheckpoints);$labels=@(Get-OnlineTownServiceLabels)
    Assert-Online ($rows.Count -eq 13 -and (Equal-Online @($rows.label) $labels)) 'services_exact_thirteen_labels_missing'
    $ownerId=[string]$Report.checkpoint.playerId;$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$lastSequence=0
    foreach($row in $rows){
        Require-OnlineFields $row @('label','expectedOutcome','ownerRole','clientProcessId','networkMode','actualUiObserved','clientBefore','before','after','serverObservation','peerUnchangedRequired','peerUnchanged','terminal') 'services_client_row_incomplete'
        $outcome=Get-OnlineTownServiceOutcome ([string]$row.label)
        Assert-Online ($row.expectedOutcome -ceq $outcome -and $row.terminal -ceq $outcome -and $row.ownerRole -ceq $Role -and
            $row.clientProcessId -eq $Report.processId -and $row.networkMode -ceq 'client' -and $row.actualUiObserved -is [bool]) 'services_client_row_identity_or_terminal'
        if($outcome -ceq 'opened-ui'){Assert-Online ($row.actualUiObserved -eq $true) 'services_actual_ui_not_observed'}
        # 최초 client 관측과 서버 before 사본을 별도 보존하며 둘의 기록 시점은 혼동하지 않는다.
        Assert-OnlineTownServiceProfile $row.clientBefore $ownerId
        Assert-OnlineTownServiceProfile $row.before $ownerId
        Assert-OnlineTownServiceProfile $row.after $ownerId
        $event=$row.serverObservation
        Require-OnlineFields $event @('observationId','sequence','ownerRole','ownerPlayerId','serverProcessId','networkMode','action','ownerCurrent','grounded','inRange','authorityAllowedBefore','rpgGateAccepted','rpcReceived','noticeBefore','noticeAfter','noticeMatched','before','after','ownerBeforeCore','ownerAfterCore','peerBeforeCore','peerAfterCore','peerBefore','peerAfter','peerUnchangedRequired','peerUnchanged','terminal') 'services_server_event_incomplete'
        Assert-OnlineTownServicesNoRawNonce $row
        Assert-Online (-not [string]::IsNullOrWhiteSpace([string]$event.observationId) -and $seen.Add([string]$event.observationId) -and
            $event.ownerRole -ceq $Role -and $event.ownerPlayerId -ceq $ownerId -and $event.networkMode -ceq 'dedicated' -and
            $event.ownerCurrent -is [bool] -and $event.ownerCurrent -eq $true) 'services_server_event_owner_or_identity'
        Assert-OnlineTownServiceInteger $event.sequence 1 'services_server_sequence_invalid'
        Assert-Online ($event.sequence -gt $lastSequence) 'services_owner_event_sequence_not_monotonic'
        $lastSequence=$event.sequence
        Assert-OnlineTownServiceInteger $event.serverProcessId 1 'services_server_process_invalid'
        foreach($flag in @('grounded','inRange','authorityAllowedBefore','rpgGateAccepted','rpcReceived','noticeMatched','peerUnchangedRequired','peerUnchanged')){
            Assert-Online ($event.$flag -is [bool]) 'services_server_event_flag_type'
        }
        foreach($name in @('ownerBeforeCore','ownerAfterCore','peerBeforeCore','peerAfterCore')){
            Assert-Online ($null -ne $event.$name -and $event.$name -is [pscustomobject] -and @($event.$name.PSObject.Properties).Count -gt 0) 'services_core_object_missing'
        }
        Assert-OnlineTownServiceInteger $event.noticeBefore 0 'services_notice_before_invalid'
        Assert-OnlineTownServiceInteger $event.noticeAfter 0 'services_notice_after_invalid'
        Assert-OnlineTownServiceProfile $event.before $ownerId
        Assert-OnlineTownServiceProfile $event.after $ownerId
        foreach($endpoint in @('before','after')){
            foreach($field in @('revision','acknowledgedRevision','profileHash')){
                Assert-Online (Equal-Online $row.$endpoint.$field $event.$endpoint.$field) 'services_client_server_ack_mismatch'
            }
        }
        $peerId=[string]$event.peerBefore.playerId
        Assert-Online ($peerId -cne $ownerId -and $event.peerAfter.playerId -ceq $peerId) 'services_distinct_peer_missing'
        Assert-OnlineTownServiceProfile $event.peerBefore $peerId
        Assert-OnlineTownServiceProfile $event.peerAfter $peerId
        $peerRequired=$row.label -cne 'forest-three-kills'
        Assert-Online ($row.peerUnchangedRequired -is [bool] -and $row.peerUnchanged -is [bool] -and
            $row.peerUnchangedRequired -eq $peerRequired -and $event.peerUnchangedRequired -eq $peerRequired -and
            $row.peerUnchanged -eq $event.peerUnchanged) 'services_peer_scope_flags_mismatch'
        if($peerRequired){
            # 동료의 경제·아이템·장비·퀘스트 core만 비교하며 이동/UTC 저장 bookkeeping의 hash·revision은 구별한다.
            Assert-Online ($row.peerUnchanged -eq $true -and (Equal-Online $event.peerBeforeCore $event.peerAfterCore)) 'services_peer_profile_changed'
        }
        if($outcome -ceq 'shared-forest-ack'){
            Assert-Online ($event.action -ceq 'forest' -and $event.rpcReceived -eq $false -and $event.rpgGateAccepted -eq $false -and
                $event.terminal -ceq 'shared-forest-ack' -and $row.after.revision -gt $row.before.revision) 'services_shared_forest_ack_invalid'
        }else{
            Assert-Online ($event.rpcReceived -eq $true -and $event.rpgGateAccepted -eq $true) 'services_rpc_not_received_or_gate_skipped'
            if($outcome -ceq 'authority-rejected'){
                Assert-Online ($event.authorityAllowedBefore -eq $false -and $event.inRange -eq $false -and
                    $event.noticeMatched -eq $true -and $event.noticeAfter -gt $event.noticeBefore -and
                    $event.terminal -ceq 'authority-rejected') 'services_authority_rejection_not_observed'
            }else{
                Assert-Online ($event.authorityAllowedBefore -eq $true -and $event.grounded -eq $true -and $event.inRange -eq $true) 'services_current_owner_ground_or_range_missing'
                Assert-Online ($event.terminal -ceq $(if($outcome -ceq 'opened-ui'){'dialogue-authorized'}else{'acknowledged-save'})) 'services_server_terminal_invalid'
            }
        }
        if($outcome -ceq 'acknowledged-save'){
            Assert-Online ($row.after.revision -eq $row.before.revision+1 -and $row.after.profileHash -cne $row.before.profileHash -and
                $event.after.revision -eq $event.before.revision+1) 'services_single_exact_ack_not_observed'
        }elseif($outcome -cne 'shared-forest-ack'){
            Assert-Online ($row.before.revision -eq $row.after.revision -and $row.before.profileHash -ceq $row.after.profileHash -and
                (Equal-Online $event.ownerBeforeCore $event.ownerAfterCore)) 'services_nonmutation_changed_profile'
        }
        $actions=switch([string]$row.label){
            'guide-out-of-range-reject' {@('interact','quest')}
            {$_ -in @('guide-open','guide-turnin-open','shop-open','forge-open','portal-open')} {@('interact')}
            {$_ -in @('forest-accept','forest-turnin')} {@('quest')}
            'forest-three-kills' {@('forest')}
            {$_ -in @('shop-wrong-service-reject','shop-buy-one')} {@('shop')}
            default {@('enhance')}
        }
        Assert-Online ($event.action -cin @($actions)) 'services_action_kind_mismatch'
        if($row.label -ceq 'forest-three-kills'){
            Assert-Online ($row.after.forestQuest -eq 2 -and $row.after.forestKills -eq 3 -and $row.after.wallet.gold -eq 45 -and $row.after.wallet.ore -eq 3) 'services_actual_three_kill_totals_mismatch'
        }elseif($row.label -ceq 'forest-turnin'){
            Assert-Online ($row.after.forestQuest -eq 3 -and $row.after.forestKills -eq 3 -and $row.after.wallet.gold -eq 135 -and $row.after.wallet.ore -eq 7 -and $row.after.level -eq 2 -and $row.after.experience -eq 105 -and $row.after.bagCount -eq 5) 'services_quest_turnin_totals_mismatch'
        }elseif($row.label -ceq 'shop-buy-one'){
            Assert-Online ($row.after.wallet.gold -eq $row.before.wallet.gold-15 -and $row.after.wallet.healthPotions -eq $row.before.wallet.healthPotions+1 -and
                $row.after.wallet.ore -eq $row.before.wallet.ore -and $row.after.wallet.manaPotions -eq $row.before.wallet.manaPotions -and
                (Equal-Online @($row.before.items) @($row.after.items))) 'services_purchase_delta_mismatch'
        }elseif($row.label -ceq 'forge-upgrade-one'){
            Assert-Online ($row.after.wallet.gold -eq $row.before.wallet.gold-30 -and $row.after.wallet.ore -eq $row.before.wallet.ore-2 -and
                @($row.before.items).Count -eq @($row.after.items).Count) 'services_upgrade_cost_or_count_mismatch'
            $changed=0
            for($index=0;$index -lt @($row.before.items).Count;$index++){
                $beforeItem=$row.before.items[$index];$afterItem=$row.after.items[$index]
                if(Equal-Online $beforeItem $afterItem){continue}
                $expectedItem=$beforeItem|ConvertTo-Json -Depth 80|ConvertFrom-Json -Depth 80
                Assert-Online ($beforeItem.upgrade -eq 0 -and $beforeItem.slot -eq 0) 'services_upgrade_target_not_zero_weapon'
                $expectedItem.upgrade=1
                Assert-Online (Equal-Online $expectedItem $afterItem) 'services_upgrade_changed_identity_or_other_item_fields'
                $changed++
            }
            Assert-Online ($changed -eq 1 -and $row.before.equippedWeaponInstanceId -ceq $row.after.equippedWeaponInstanceId -and
                $row.before.equippedArmorInstanceId -ceq $row.after.equippedArmorInstanceId) 'services_upgrade_not_exactly_one_preserved_weapon'
        }
    }
}
function Assert-OnlineTownServicesServer($ServerReport,$ClientReports){
    Require-OnlineFields $ServerReport @('serverTownServices','processId') 'services_server_report_missing'
    $value=$ServerReport.serverTownServices
    Require-OnlineFields $value @('phase','status','events','actualAirborneNegativeClaimed') 'services_server_report_incomplete'
    Assert-Online ($value.phase -eq 9 -and $value.status -ceq 'complete' -and $value.actualAirborneNegativeClaimed -is [bool] -and
        $value.actualAirborneNegativeClaimed -eq $false) 'services_terminal_server_scope_invalid'
    $events=@($value.events);Assert-Online ($events.Count -eq 26) 'services_exact_both_role_events_missing'
    Assert-OnlineTownServicesNoRawNonce $value
    $sequences=[Collections.Generic.HashSet[long]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($event in $events){
        # 계정 간 비동기 ACK의 완료 순서와 사건의 sequence를 구별한다. 역할별 순서는 client row에서 검사한다.
        Assert-OnlineTownServiceInteger $event.sequence 1 'services_server_sequence_invalid'
        Assert-Online ($event.ownerRole -cin @('client1','client2') -and $event.serverProcessId -eq $ServerReport.processId -and
            $sequences.Add([long]$event.sequence) -and $seen.Add([string]$event.observationId)) 'services_server_sequence_or_process_mismatch'
    }
    foreach($role in @('client1','client2')){
        $report=$ClientReports.$role
        Assert-OnlineTownServicesClient $report $role
        $peer=if($role -ceq 'client1'){'client2'}else{'client1'}
        foreach($row in @($report.townServiceCheckpoints)){
            $event=$row.serverObservation
            Assert-Online ($event.peerBefore.playerId -ceq $ClientReports.$peer.checkpoint.playerId) 'services_event_peer_identity_mismatch'
            $matches=@($events|Where-Object {$_.observationId -ceq $event.observationId -and $_.ownerRole -ceq $role})
            Assert-Online ($matches.Count -eq 1 -and (Equal-Online $matches[0] $event)) 'services_embedded_event_not_exact_server_event'
        }
    }
    $summary['unit4LegacyServices']=[ordered]@{status='running';exactClientCheckpointLabels=$true;serverAuthorityAndExactAckValidated=$true;peerInvarianceValidated=$true;actualAirborneNegativeClaimed=$false;serverEventCount=26;reports=@('register/client1.json','register/client2.json','register/server.json');nativeOnlyProofUsed=$false;servicePlacementValidationClaimed=$false;newTownAdoptionClaimed=$false}
}

function Assert-OnlineClientReport($Report,[string]$Role,[string]$Phase,[bool]$Crash){
    Require-OnlineFields $Report @('movementCm','checkpoint','joinAttempts','hasSession','flowState') 'client_report_incomplete'
    Assert-Online ($Report.movementCm -ge 65) 'normal_movement_missing'
    foreach($name in @('wrong_password_rejected','character_preview','two_distinct_remote_players','normal_movement')){Assert-Online (Test-OnlineCheck $Report $name) 'required_ui_check_missing'}
    if($Phase -ceq 'register'){
        Assert-Online (Test-OnlineCheck $Report 'connection_failure_visible') 'offline_failure_missing'
        if($Scenario -cne 'unit4-legacy-services'){Assert-Online (Test-OnlineCheck $Report 'normal_potion_saved_once') 'potion_ack_missing'}
        if($Scenario -in @('dungeon','growth')){
            foreach($name in @('both_forest_quests_active','forest_reward_equipped','entered_root_ruins','second_wave','boss_seen','ally_downed','boss_defeated','ally_revived','server_reward_totals')){
                Assert-Online (Test-OnlineCheck $Report $name) 'dungeon_normal_gameplay_check_missing'
            }
        }
        Assert-Online ($Report.joinAttempts -ge 2) 'fresh_ticket_retry_missing'
    }
    if($Scenario -ceq 'unit4-legacy-services' -and $Phase -ceq 'register'){Assert-OnlineTownServicesClient $Report $Role}
    if($Scenario -ceq 'growth' -and $Phase -ceq 'register'){Assert-OnlineGrowth $Report $Role}
    if($Scenario -ceq 'reconnect' -and $Phase -in @('register','resume')){Assert-OnlineMidrun $Report $Role $false}
    if($Scenario -ceq 'party' -and $Phase -in @('register','resume')){Assert-OnlineParty $Report $Role ($Phase -ceq 'resume')}
    if($Scenario -ceq 'party-results' -and $Phase -in @('register','resume')){Assert-OnlineResultsClient $Report $Role $Phase $Crash}
    if($Scenario -ceq 'party-isolation' -and $Phase -ceq 'register'){
        Require-OnlineFields $Report @('partyIsolation') 'isolation_client_report_missing'
        $isolation=$Report.partyIsolation
        Require-OnlineFields $isolation @('status','selfKey','partyId','expeditionId','worldOrigin','forestCheckpoint','baselineProfile','firstKillProfile','finalProfile','actualCrossDamageGuardAttempted') 'isolation_client_report_incomplete'
        Assert-Online ($isolation.status -ceq 'passed' -and $isolation.actualCrossDamageGuardAttempted -eq $false -and @($isolation.worldOrigin).Count -eq 3) 'isolation_client_scope_or_arrival_missing'
        foreach($id in @($isolation.partyId,$isolation.expeditionId)){Assert-Online ([string]$id -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $id -cne '00000000-0000-0000-0000-000000000000') 'isolation_client_uuid_invalid'}
        foreach($name in @('both_forest_quests_active','forest_reward_equipped','isolation_solo_party','isolation_ready','isolation_private_arrival','isolation_safe_baseline_ready','isolation_final_saved','isolation_peer_server_handshake')){Assert-Online (Test-OnlineCheck $Report $name) 'isolation_common_ui_check_missing'}
        $roleChecks=if($Role -ceq 'client1'){@('isolation_a_first_kill_saved','isolation_a_normally_downed','isolation_a_recovered_town','isolation_a_no_reward_from_b')}else{@('isolation_b_unchanged_after_a_kill','isolation_b_preserved_during_a_recovery','isolation_b_preserved_after_a_return','isolation_b_first_kill_saved')}
        foreach($name in $roleChecks){Assert-Online (Test-OnlineCheck $Report $name) 'isolation_role_ui_check_missing'}
        foreach($cp in @($isolation.forestCheckpoint,$isolation.baselineProfile)){Assert-Online ($cp.forestQuest -eq 3 -and $cp.dungeonQuest -eq 1 -and $cp.wallet.gold -eq 135 -and $cp.wallet.ore -eq 7 -and $cp.level -eq 2 -and $cp.experience -eq 105 -and $cp.bagCount -eq 5) 'isolation_normal_forest_baseline_missing'}
        Assert-OnlineCheckpoint $isolation.firstKillProfile $Role
        Assert-OnlineCheckpoint $isolation.finalProfile $Role
        Assert-Online (Equal-Online $Report.checkpoint $isolation.finalProfile) 'isolation_final_checkpoint_changed'
    }
    if($Scenario -ceq 'party-entry' -and $Phase -ceq 'register'){
        Require-OnlineFields $Report @('partyEntry') 'entry_client_report_missing'
        $entry=$Report.partyEntry
        Require-OnlineFields $entry @('status','selfKey','peerKey','partyId','expeditionId','preparationRevision','region','difficulty','safeSaved','peerAndServerObserved','extendedNegatives','earnedBeforeEntry') 'entry_client_report_incomplete'
        Assert-Online ($entry.status -ceq 'passed' -and $entry.extendedNegatives -eq $true -and $entry.safeSaved -eq $true -and $entry.peerAndServerObserved -eq $true -and $entry.preparationRevision -ge 3 -and $entry.region -eq 1 -and $entry.difficulty -eq 0) 'entry_client_not_observed'
        foreach($id in @($entry.partyId,$entry.expeditionId)){Assert-Online ([string]$id -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $id -cne '00000000-0000-0000-0000-000000000000') 'entry_uuid_invalid'}
        Assert-Online (-not [string]::IsNullOrEmpty($entry.selfKey) -and -not [string]::IsNullOrEmpty($entry.peerKey) -and $entry.selfKey -cne $entry.peerKey) 'entry_member_key_invalid'
        foreach($name in @('both_forest_quests_active','forest_reward_equipped','entered_root_ruins','entry_formal_party_two','entry_unready_rejected','entry_nonleader_selection_rejected','entry_initial_both_ready','entry_selection_resets_all_ready','entry_stale_ready_rejected','entry_stale_selection_rejected','entry_nonleader_start_rejected','entry_stale_start_rejected','entry_final_selection_observed','entry_final_both_ready','entry_exact_two_arrived','entry_safe_saved_server_ticket_observed','entry_peer_and_server_handshake','entry_forest_only_totals')){Assert-Online (Test-OnlineCheck $Report $name) 'entry_required_network_check_missing'}
        Assert-Online ($entry.earnedBeforeEntry.forestQuest -eq 3 -and $entry.earnedBeforeEntry.dungeonQuest -eq 1 -and $entry.earnedBeforeEntry.wallet.gold -eq 135 -and $entry.earnedBeforeEntry.wallet.ore -eq 7 -and $entry.earnedBeforeEntry.level -eq 2 -and $entry.earnedBeforeEntry.experience -eq 105 -and $entry.earnedBeforeEntry.bagCount -eq 5) 'entry_normal_forest_progress_missing'
    }
    Assert-OnlineCheckpoint $Report.checkpoint $Role
    if($Crash){Assert-Online ($Report.status -ceq 'crash-ready' -and $Report.step -eq 30 -and $Report.networkMode -ceq 'client' -and $Report.localControllerCount -eq 1 -and $Report.hasSession -eq $true) 'crash_checkpoint_not_ready'}
    else{
        Require-OnlineFields $Report @('report_snapshot_frozen','menuProfileHash','menuRevision') 'terminal_report_metadata_missing'
        Assert-Online ($Report.status -ceq 'passed' -and $Report.report_snapshot_frozen -eq $true -and $Report.flowState -ceq 'SignedOut' -and $Report.hasSession -eq $false) 'terminal_ui_not_logged_out'
        foreach($name in @('save_release_ack_before_menu','logout_clears_session')){Assert-Online (Test-OnlineCheck $Report $name) 'safe_leave_or_logout_missing'}
    }
}
# 서버 사건 스냅숏과 원본·복구된 클라이언트 보고서의 모든 개인 지급을 교차검사한다.
function Assert-OnlineResultsServer($ServerReport,$ClientReports){
    Require-OnlineFields $ServerReport @('serverResults') 'results_server_report_missing';$value=$ServerReport.serverResults
    Require-OnlineFields $value @('cleared','resultsAcknowledged','retryBefore','retryDisconnected','retrySoloKill','retryRejoined','retreatOneVote','abandoned','serverRestartReceiptPersistenceClaimed') 'results_server_event_missing'
    Assert-Online ($value.serverRestartReceiptPersistenceClaimed -eq $false) 'results_restart_scope_misrepresented'
    foreach($name in @('server_results_one_boss_clear_saved','server_results_reopen_ack_no_duplicate','server_results_retry_new_uuid_two_arrived','server_results_retry_member_reserved','server_results_online_member_only_reward','server_results_same_retry_uuid_rejoined','server_results_single_vote_did_not_return_party','server_results_consented_own_return_preserved_grants')){Assert-Online (Test-OnlineCheck $ServerReport $name) 'results_server_observation_missing'}
    $one=$ClientReports.client1.partyResults;$two=$ClientReports.client2.partyResults;$original=$summary.interruption.originalPartyResults
    Assert-Online ($summary.interruption.offlineCanonicalUnchanged -eq $true -and $summary.interruption.clientTwoLeaseReleased -eq $true -and $summary.interruption.serverControllersAfterDisconnect -eq 1 -and $summary.interruption.reservedPartyMembers -eq 2) 'results_offline_canonical_proof_missing'
    Assert-Online ($one.selfKey -cne $two.selfKey -and $one.partyId -ceq $two.partyId -and $one.clearedExpeditionId -ceq $two.clearedExpeditionId -and $one.retryExpeditionId -ceq $two.retryExpeditionId -and $one.retryExpeditionId -cne $one.clearedExpeditionId -and $two.selfKey -ceq $original.selfKey -and $two.retryExpeditionId -ceq $original.retryExpeditionId -and $ClientReports.client2.checkpoint.playerId -ceq $summary.interruption.originalCheckpoint.playerId) 'results_retry_or_authenticated_owner_changed'
    Assert-OnlineResultsSameGrant $original.clearResult $two.restoredClearResult
    $clear=$value.cleared;$ack=$value.resultsAcknowledged;$before=$value.retryBefore;$gone=$value.retryDisconnected;$solo=$value.retrySoloKill;$back=$value.retryRejoined;$vote=$value.retreatOneVote;$end=$value.abandoned
    $keys=@($one.selfKey,$two.selfKey)|Sort-Object -CaseSensitive
    $bossIds=@();$bossItemIds=@()
    foreach($pair in @(@('a','client1',$one),@('b','client2',$original))){
        $slot=$pair[0];$role=$pair[1];$client=$pair[2];$row=$clear.$slot
        Assert-Online ($row.connected -eq $true -and $row.saved -eq $true -and $row.publicKey -ceq $client.selfKey) 'results_clear_server_person_invalid'
        Assert-OnlineCheckpoint $row.profile $role $true
        Assert-OnlineResultsReceipt $row.clearedResult $row.profile $role $client.selfKey $one.clearedExpeditionId 1 $true
        Assert-OnlineResultsSameGrant $client.clearResult $row.clearedResult
        Assert-OnlineResultsSameGrant $row.clearedResult $ack.$slot.clearedResult
        Assert-OnlineResultsSameGrant $row.clearedResult $back.$slot.clearedResult
        Assert-OnlineResultsSameGrant $row.clearedResult $end.$slot.clearedResult
        Assert-Online ($ack.$slot.clearedResult.acknowledged -eq $true) 'results_server_ack_missing'
        foreach($field in @('playerId','heroClass','level','experience','classTier','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId','forestQuest','dungeonQuest','dungeonCompletions')){Assert-Online (Equal-Online $row.profile.$field $ack.$slot.profile.$field) 'results_reopen_or_ack_granted_again'}
        $boss=@($row.clearedResult.receipts|Where-Object boss -EQ $true)
        Assert-Online ($boss.Count -eq 1 -and @($boss[0].items).Count -eq 1) 'results_boss_grant_shape_invalid'
        $bossIds+=@($boss[0].grantId);$bossItemIds+=@($boss[0].items[0].instanceId)
    }
    Assert-Online (@($bossIds|Select-Object -Unique).Count -eq 1 -and @($bossItemIds|Select-Object -Unique).Count -eq 2) 'results_more_than_one_boss_or_shared_personal_loot'
    foreach($view in @($before,$gone,$solo,$back,$vote)){
        Assert-Online ($view.activeInstances -eq 1 -and $view.activeExpeditionId -ceq $one.retryExpeditionId -and $view.ticketPartyId -ceq $one.partyId -and $view.ticketExpeditionId -ceq $one.retryExpeditionId -and $view.region -eq 1 -and $view.difficulty -eq 0 -and $view.ticketRegion -eq 1 -and $view.ticketDifficulty -eq 0 -and $view.ticketPreparationRevision -eq $one.retryPreparationRevision -and (Equal-Online @($view.ticketMembers|Sort-Object -CaseSensitive) @($keys)) -and $view.wave -eq 1 -and $view.required -eq 3 -and $view.recovering -eq $false) 'results_retry_server_ticket_or_progress_changed'
        foreach($enemy in $view.enemies){Assert-Online ([string]$enemy.id -cmatch '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$' -and $enemy.id -cne '00000000-0000-0000-0000-000000000000' -and $enemy.expeditionId -ceq $one.retryExpeditionId -and $enemy.health -gt 0) 'results_retry_enemy_identity_invalid'}
        Assert-Online (@($view.enemies|ForEach-Object id|Select-Object -Unique).Count -eq @($view.enemies).Count) 'results_retry_enemy_identity_repeated'
    }
    Assert-Online ($before.connectedMembers -eq 2 -and $before.reservedMembers -eq 2 -and $before.defeated -eq 0 -and @($before.enemies).Count -eq 3 -and $before.a.safe -eq $true -and $before.b.safe -eq $true) 'results_retry_before_crash_invalid'
    Assert-Online ($gone.connectedMembers -eq 1 -and $gone.reservedMembers -eq 2 -and $gone.a.connected -eq $true -and $gone.b.connected -eq $false -and $gone.defeated -eq 0 -and (Equal-Online $before.enemies $gone.enemies)) 'results_retry_disconnect_not_reserved'
    Assert-Online ($solo.connectedMembers -eq 1 -and $solo.b.connected -eq $false -and $solo.defeated -eq 1 -and @($solo.enemies).Count -eq 2 -and $solo.a.safe -eq $true) 'results_survivor_did_not_progress_alone'
    Assert-OnlineCheckpoint $solo.a.profile 'client1'
    Assert-OnlineResultsReceipt $solo.a.retryResult $solo.a.profile 'client1' $one.selfKey $one.retryExpeditionId 0 $false
    Assert-Online ($back.connectedMembers -eq 2 -and $back.reservedMembers -eq 2 -and $back.a.expeditionId -ceq $one.retryExpeditionId -and $back.b.expeditionId -ceq $one.retryExpeditionId -and $back.defeated -eq 1 -and $back.a.safe -eq $true -and $back.b.safe -eq $true -and (Equal-Online $solo.enemies $back.enemies)) 'results_rejoined_enemy_or_progress_changed'
    Assert-OnlineResultsReceipt $back.a.retryResult $back.a.profile 'client1' $one.selfKey $one.retryExpeditionId 0 $false
    Assert-OnlineResultsReceipt $back.b.retryResult $back.b.profile 'client2' $two.selfKey $one.retryExpeditionId 0 $false
    foreach($field in @('playerId','heroClass','level','experience','classTier','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId','forestQuest','dungeonQuest','dungeonCompletions')){Assert-Online (Equal-Online $summary.interruption.originalCheckpoint.$field $back.b.profile.$field) 'results_offline_owner_profile_changed_on_restore'}
    Assert-Online ($vote.connectedMembers -eq 2 -and @($vote.departureConsents).Count -eq 1 -and $vote.departureConsents[0] -cin @($keys) -and $vote.a.expeditionId -ceq $one.retryExpeditionId -and $vote.b.expeditionId -ceq $one.retryExpeditionId) 'results_single_vote_returned_or_invalid_owner'
    Assert-Online ($end.activeInstances -eq 0 -and $end.connectedMembers -eq 2 -and $end.a.inTown -eq $true -and $end.b.inTown -eq $true -and $end.a.alive -eq $true -and $end.b.alive -eq $true -and $end.a.saved -eq $true -and $end.b.saved -eq $true -and $end.a.expeditionId -ceq '00000000-0000-0000-0000-000000000000' -and $end.b.expeditionId -ceq '00000000-0000-0000-0000-000000000000') 'results_consented_return_not_complete'
    foreach($pair in @(@('a','client1',$one),@('b','client2',$two))){$row=$end.($pair[0]);Assert-OnlineCheckpoint $row.profile $pair[1];Assert-OnlineResultsReceipt $row.retryResult $row.profile $pair[1] $pair[2].selfKey $one.retryExpeditionId 3 $false}
    $summary.interruption.status='passed';$summary.interruption.sameAuthenticatedRetryUuid=$true;$summary.interruption.enemyUuidAndHealthExact=$true;$summary.interruption.noOfflineGrant=$true
    $summary['partyResults']=[ordered]@{status='running';oneBossCleared=$true;personalReceiptsMatchActualGrants=$true;bossItemIdentityPreserved=$true;resultReopenNoDuplicateGrant=$true;allGrantSaveAcknowledgementsValidated=$true;npcQuestStillReadyWithoutTurnIn=$true;freshConsentRetryNewUuid=$true;exactTicketAndTwoArrivals=$true;sameAuthenticatedCharacterRejoinedNewUuid=$true;enemyUuidAndHealthPreservedOnRejoin=$true;offlineMemberReceivedNoReward=$true;unanimousOwnPartyRetreat=$true;personalAbandonedResultsPreserveGrants=$true;serverRestartReceiptPersistenceClaimed=$false;clearedExpeditionId=$one.clearedExpeditionId;retryExpeditionId=$one.retryExpeditionId;partyId=$one.partyId;memberKeys=@($keys)}
}
function Assert-OnlineServerObservation($ServerReport,$ClientReports){
    Require-OnlineFields $ServerReport @('serverObservedPlayerIds') 'server_simultaneous_players_evidence_missing'
    Assert-Online ($ServerReport.networkMode -ceq 'dedicated' -and $ServerReport.localControllerCount -eq 0) 'server_observation_not_dedicated'
    Assert-Online (Test-OnlineCheck $ServerReport 'server_observed_distinct_ready_players') 'server_ready_players_observation_missing'
    $observed=@($ServerReport.serverObservedPlayerIds|Sort-Object -CaseSensitive)
    $expected=@(@($ClientReports.client1.checkpoint.playerId,$ClientReports.client2.checkpoint.playerId)|Sort-Object -CaseSensitive)
    Assert-Online ($observed.Count -eq 2 -and @($observed|Select-Object -Unique).Count -eq 2 -and (Equal-Online $observed $expected)) 'server_simultaneous_player_ids_mismatch'
    if($Scenario -ceq 'unit4-legacy-services' -and $ClientReports.client1.mode -ceq 'register'){
        Assert-OnlineTownServicesServer $ServerReport $ClientReports
    }
    if($Scenario -ceq 'party-results' -and $ClientReports.client1.mode -ceq 'register'){
        Assert-OnlineResultsServer $ServerReport $ClientReports
    }
    if($Scenario -ceq 'party-isolation' -and $ClientReports.client1.mode -ceq 'register'){
        Assert-OnlineIsolationServer $ServerReport $ClientReports
    }
    if($Scenario -ceq 'party-entry' -and $ClientReports.client1.mode -ceq 'register'){
        Require-OnlineFields $ServerReport @('serverExpeditionArrival') 'entry_server_ticket_missing'
        Assert-Online (Test-OnlineCheck $ServerReport 'server_exact_expedition_arrival') 'entry_server_arrival_not_observed'
        $ticket=$ServerReport.serverExpeditionArrival;$one=$ClientReports.client1.partyEntry;$two=$ClientReports.client2.partyEntry
        Require-OnlineFields $ticket @('expeditionId','partyId','region','difficulty','preparationRevision','encounterSerial','defeatedEnemies','memberKeys','members') 'entry_server_ticket_incomplete'
        Assert-Online ($ticket.expeditionId -ceq $one.expeditionId -and $ticket.expeditionId -ceq $two.expeditionId -and $ticket.partyId -ceq $one.partyId -and $ticket.partyId -ceq $two.partyId -and $ticket.region -eq 1 -and $ticket.difficulty -eq 0 -and $ticket.preparationRevision -eq $one.preparationRevision -and $ticket.preparationRevision -eq $two.preparationRevision -and $ticket.encounterSerial -gt 0 -and $ticket.defeatedEnemies -eq 0) 'entry_ticket_selection_or_uuid_mismatch'
        $keys=@(@($one.selfKey,$two.selfKey)|Sort-Object -CaseSensitive)
        Assert-Online ($one.selfKey -ceq $two.peerKey -and $two.selfKey -ceq $one.peerKey -and @($ticket.memberKeys).Count -eq 2 -and @($ticket.members).Count -eq 2 -and (Equal-Online @($ticket.memberKeys|Sort-Object -CaseSensitive) $keys) -and (Equal-Online @($ticket.members|ForEach-Object playerKey|Sort-Object -CaseSensitive) $keys) -and (Equal-Online @($ticket.members|ForEach-Object playerId|Sort-Object -CaseSensitive) $expected)) 'entry_ticket_participants_not_exact'
        foreach($member in $ticket.members){Assert-Online ($member.arrived -eq $true -and $member.expeditionId -ceq $ticket.expeditionId -and $member.ready -eq $true -and $member.savePending -eq $false -and $member.revision -eq $member.acknowledgedRevision -and $member.forestQuest -eq 3 -and $member.dungeonQuest -eq 1 -and $member.wallet.gold -eq 135 -and $member.wallet.ore -eq 7) 'entry_server_member_not_arrived_or_saved'}
        $summary['partyEntry']=[ordered]@{status='passed';normalForestQuestAndReward=$true;formalPartySelectionAndReady=$true;unreadyAndNonleaderRejected=$true;staleSelectionReadyAndEntryRejected=$true;selectionResetsReadiness=$true;exactTicketAndTwoArrivals=$true;twoClientServerHandshake=$true;entryOnlyNoDungeonRewards=$true;partyId=$ticket.partyId;expeditionId=$ticket.expeditionId;memberKeys=$keys;region=$ticket.region;difficulty=$ticket.difficulty;preparationRevision=$ticket.preparationRevision;crashReconnectPerformed=$false}
    }
    if($Scenario -ceq 'party' -and $ClientReports.client1.mode -ceq 'register'){
        Require-OnlineFields $ServerReport @('serverPartyBefore','serverPartySolo','serverPartyRejoined') 'party_server_snapshots_missing'
        foreach($name in @('server_party_before_disconnect','server_party_disconnected_member_reserved','server_party_same_character_restored')){Assert-Online (Test-OnlineCheck $ServerReport $name) 'party_server_lifecycle_not_observed'}
        $before=$ServerReport.serverPartyBefore;$solo=$ServerReport.serverPartySolo;$after=$ServerReport.serverPartyRejoined
        $partyId=$summary.interruption.originalParty.finalPartyId
        $memberKeys=@(@($ClientReports.client1.partyStage1.selfKey,$ClientReports.client2.partyStage1.selfKey)|Sort-Object -CaseSensitive)
        foreach($view in @($before,$solo,$after)){
            Assert-Online (@($view.parties).Count -eq 1) 'party_server_unexpected_roster_count'
            $row=$view.parties[0]
            Assert-Online ($row.partyId -ceq $partyId -and @($row.members).Count -eq 2 -and (Equal-Online @($row.members|ForEach-Object playerKey|Sort-Object -CaseSensitive) $memberKeys)) 'party_same_roster_or_id_not_preserved'
        }
        Assert-Online ($before.controllers -eq 2 -and $solo.controllers -eq 1 -and $after.controllers -eq 2 -and @($before.parties[0].members|Where-Object connected).Count -eq 2 -and @($solo.parties[0].members|Where-Object connected).Count -eq 1 -and @($after.parties[0].members|Where-Object connected).Count -eq 2) 'party_server_connected_counts_mismatch'
        Assert-Online ($before.parties[0].leaderKey -ceq $ClientReports.client2.partyStage1.selfKey -and $solo.parties[0].leaderKey -ceq $ClientReports.client1.partyStage1.selfKey -and $after.parties[0].leaderKey -ceq $ClientReports.client1.partyStage1.selfKey) 'party_disconnect_leader_transfer_not_preserved'
        $summary.interruption.status='passed';$summary.interruption['partyMembershipRestored']=$true;$summary.interruption['samePartyId']=$partyId
        $summary['partyStage1']=[ordered]@{status='passed';uiCreateDeclineAcceptLeaveTransferAndDisband=$true;invalidMemberTransferDuplicateCreateAndInviteReplayRejected=$true;serverRosterBeforeDisconnectAfterReconnect=$true;partyId=$partyId;memberKeys=$memberKeys}
    }
    if($Scenario -in @('dungeon','growth') -and $ClientReports.client1.mode -ceq 'register'){
        Require-OnlineFields $ServerReport @('serverDungeonObservation') 'server_dungeon_observation_missing'
        Assert-Online (Test-OnlineCheck $ServerReport 'server_observed_dungeon_completion') 'server_dungeon_completion_not_observed'
        $observation=$ServerReport.serverDungeonObservation
        Assert-Online ($observation.bossDefeated -eq $true -and $observation.dungeonWave -eq 3 -and $observation.encounterSerial -gt 0 -and @($observation.members).Count -eq 2) 'server_dungeon_observation_invalid'
        $members=@($observation.members|ForEach-Object playerId|Sort-Object -CaseSensitive)
        Assert-Online (Equal-Online $members $expected) 'server_dungeon_identities_mismatch'
        foreach($member in $observation.members){Assert-Online ($member.wallet.gold -eq 383 -and $member.wallet.ore -eq 21 -and $member.wallet.rootCores -eq 1 -and $member.dungeonQuest -eq 2) 'server_dungeon_reward_totals_mismatch'}
    }
    if($Scenario -ceq 'reconnect' -and $ClientReports.client1.mode -ceq 'register'){
        Require-OnlineFields $ServerReport @('serverMidBefore','serverMidSolo','serverMidRejoined') 'server_midrun_snapshots_missing'
        foreach($name in @('server_midrun_before','server_midrun_solo','server_midrun_same_id_rejoin')){Assert-Online (Test-OnlineCheck $ServerReport $name) 'server_midrun_observation_missing'}
        $before=$ServerReport.serverMidBefore;$solo=$ServerReport.serverMidSolo;$after=$ServerReport.serverMidRejoined
        foreach($view in @($before,$solo,$after)){
            Assert-Online ($view.encounterSerial -eq $before.encounterSerial -and $view.encounterSerial -gt 0 -and $view.wave -eq 1 -and $view.defeated -eq 1 -and $view.required -eq 3 -and @($view.enemies).Count -eq 2 -and $view.allSavedReady -eq $true -and $view.allSafe -eq $true) 'same_live_dungeon_not_preserved'
            Assert-Online (Equal-Online @($view.enemies) @($before.enemies)) 'surviving_enemy_identity_or_health_changed'
            foreach($member in $view.members){Assert-Online ($member.wallet.gold -eq 153 -and $member.wallet.ore -eq 8 -and $member.level -eq 2 -and $member.experience -eq 140 -and $member.bagCount -eq 5) 'server_midrun_reward_changed'}
        }
        $beforeIds=@($before.members|ForEach-Object playerId|Sort-Object -CaseSensitive);$afterIds=@($after.members|ForEach-Object playerId|Sort-Object -CaseSensitive)
        Assert-Online (@($before.members).Count -eq 2 -and @($solo.members).Count -eq 1 -and @($after.members).Count -eq 2 -and (Equal-Online $beforeIds $expected) -and (Equal-Online $afterIds $expected) -and $solo.members[0].playerId -ceq $ClientReports.client1.checkpoint.playerId) 'server_midrun_authenticated_roster_mismatch'
        $mid=$summary.interruption.originalCheckpoint;$final=$ClientReports.client2.checkpoint
        foreach($field in @('playerId','heroClass','level','experience','classTier','wallet','items','bagCount','stashCount','equippedWeaponInstanceId','equippedArmorInstanceId')){Assert-Online (Equal-Online $mid.$field $final.$field) 'rejoined_midrun_character_data_changed'}
        $summary.interruption.status='passed';$summary.interruption.sameEncounterSerial=$before.encounterSerial;$summary.interruption.liveEnemyCount=2;$summary.interruption.completeMidrunWalletItemsAndEquipmentExact=$true
    }
    return ,$observed
}
function Wait-OnlinePhase([string]$Phase,$Server,$Clients,[bool]$CrashSecond){
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$results=@{};$crashObservedAt=$null;$reconnectResumed=$false
    do{
        Assert-Online ($null -ne (Get-OnlineOwnedProcess $Server)) 'server_exited_during_phase'
        foreach($role in @('client1','client2')){
            if($results.ContainsKey($role)){continue}
            $record=$Clients[$role];$report=Read-OnlineReport $record -AllowMissing
            $crash=$CrashSecond -and $role -ceq 'client2' -and -not($Scenario -in @('reconnect','party','party-results') -and $reconnectResumed)
            if($report -and (($crash -and $report.status -ceq 'crash-ready') -or (-not $crash -and $report.status -ceq 'passed'))){
                if($Scenario -ceq 'reconnect' -and $crash){Assert-OnlineMidrun $report $role $true}
                else{
                    $validationPhase=if($record.phase -ceq 'register-resume'){'resume'}else{$Phase}
                    Assert-OnlineClientReport $report $role $validationPhase $crash
                }
                if($crash){
                    if($null -eq $crashObservedAt){$crashObservedAt=[DateTime]::UtcNow}
                    if(([DateTime]::UtcNow-$crashObservedAt).TotalSeconds -lt 2){continue}
                    $null=Assert-OnlineShots $report $record.phase 5
                    Write-OnlineJson (Join-Path (Join-Path $runFolder $Phase) 'client2-crash-checkpoint.json') $report
                    Stop-OnlineOwned $record $true
                    if($Scenario -in @('reconnect','party','party-results')){
                        $Clients.client2=if($Scenario -ceq 'party'){Resume-OnlinePartyClient $Server $Clients.client1 $report}elseif($Scenario -ceq 'party-results'){Resume-OnlineResultsClient $Server $Clients.client1 $report}else{Resume-OnlineInterruptedClient $Server $Clients.client1 $report}
                        $reconnectResumed=$true;$crashObservedAt=$null;continue
                    }
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
            if($Scenario -ceq 'unit4-legacy-services'){$record|Add-Member -NotePropertyName exitedUtc -NotePropertyValue $handle.ExitTime.ToUniversalTime().ToString('o') -Force}
            if(-not $record.forcedForScenario){Assert-Online ($record.exitCode -eq 0) 'client_terminal_exit_failed'}
        }
        $zero=$null -ne $report -and $report.networkMode -ceq 'dedicated' -and $report.localControllerCount -eq 0 -and @($report.profiles).Count -eq 0
        $released=$true
        foreach($role in @('client1','client2')){$me=Api-Online GET '/v1/me' $null $readerTokens[$role];$released=$released -and $me.leaseActive -eq $false}
        if($closed -and $zero -and $released){
            $observedIds=Assert-OnlineServerObservation $report $Reports
            $result=@{};$comparisons=@()
            foreach($role in @('client1','client2')){$result[$role]=Read-OnlineCanonical $role;$comparisons+=@(Assert-OnlineCanonical $result[$role] $Reports[$role] $role ([bool]$Clients[$role].forcedForScenario))}
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
    if($Scenario -ceq 'unit4-legacy-services'){$Server|Add-Member -NotePropertyName exitedUtc -NotePropertyValue $handle.ExitTime.ToUniversalTime().ToString('o') -Force}
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
    runId=$runId;status='running';scenario=$Scenario;townTravelScenario=$TownTravelScenario;mode=$(if($PackagedClientExecutable){'editor-dedicated-two-packaged-ui-clients'}else{'editor-dedicated-two-ui-clients'});startedUtc=[DateTime]::UtcNow.ToString('o')
    backendUrl=$BackendUrl;gamePort=$Port;databaseName='fablewood';phases=[ordered]@{}
    scope=@('One PC, loopback only','Normal in-game registration, authentication, movement, potion and leave UI','Existing saves are not uploaded or injected','API reader sessions only inspect the two new accounts','API restart reuses the same database; PostgreSQL is not restarted here')
    visualReviewPerformed=$false;privateContextFolder=$privateFolder;initialBackendStartupPerformed=$false
}
if($PartyReadyCancellation){$summary['partyReadyCancellationEnabled']=$true}
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
    if($Scenario -ceq 'unit4-legacy-services'){Assert-Online (-not [string]::IsNullOrWhiteSpace($PackagedClientExecutable)) 'services_requires_packaged_clients'}
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
    $crashSecond=$Scenario -cnotin @('party-entry','party-isolation','unit4-legacy-services')
    Write-OnlineJson $contextPath @{mode='register';scenario=$Scenario;offlineFailure=$true;crashRole=$(if($crashSecond){'client2'}else{''});accounts=$accounts}
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
    $reports=Wait-OnlinePhase 'register' $server $clients $crashSecond
    $drained=Wait-OnlineDrain 'register' $server $clients $reports
    if($TownTravelScenario -ceq 'unit2-legacy-anchors'){
        # 원본 B의 interruption checkpoint에서 sidecar를 읽으며 재접속 B와 섞지 않는다.
        $originalPeer=Read-OnlineJson (Join-Path $runFolder 'register\client2-crash-checkpoint.json')
        $summary['unit2TownTravel']=Assert-OnlineUnit2 $drained.server $reports.client1 $originalPeer
    }
    $freshCanonical=$drained.canonical
    $shots=@();$shots+=@(Assert-OnlineShots $reports.client1 $clients.client1.phase 7);$shots+=@(Assert-OnlineShots $reports.client2 $clients.client2.phase 5)
    $summary.phases.register=[ordered]@{status='passed';connectionFailureObservedAtStep5=$true;serverNetworkMode=$serverInitial.networkMode;serverLocalControllers=$serverInitial.localControllerCount;serverObservedPlayerIds=$drained.serverObservedPlayerIds;serverObservedIdsMatchCheckpoints=$true;forcedCloseRole=$(if($crashSecond){'client2'}else{$null});serverDrained=$true;comparisons=$drained.comparisons;screenshots=$shots;reports=@((Join-Path $runFolder 'register\client1.json'),(Join-Path $runFolder $(if($crashSecond){'register\client2-crash-checkpoint.json'}else{'register\client2.json'})));canonical=(Join-Path $runFolder 'register\canonical.json')}
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
    if($Scenario -ceq 'unit4-legacy-services'){
        # 기존 register→안전 이탈→같은 DB API 재시작→두 계정 login을 그대로 사용한다.
        $clients=@($ownedProcesses|Where-Object role -IN @('client1','client2'))
        $servers=@($ownedProcesses|Where-Object role -CEQ 'server')
        Assert-Online ($ownedProcesses.Count -eq 6 -and $clients.Count -eq 4 -and $servers.Count -eq 2 -and
            @($ownedProcesses|Where-Object {$_.forcedForScenario -or $_.cleanupForced -or $null -eq $_.exitCode -or $_.exitCode -ne 0}).Count -eq 0) 'services_exact_four_clean_packaged_starts_missing'
        Assert-Online ($summary.unit4LegacyServices.status -ceq 'running' -and $summary.phases.register.status -ceq 'passed' -and
            $summary.phases.login.resumeWalletItemsSummaryExact -eq $true -and $summary.backendRestart.sameDatabaseIdentity -eq $true -and
            $summary.backendRestart.healthy -eq $true) 'services_safe_leave_restart_relogin_not_verified'
        $timeline=@()
        foreach($record in $ownedProcesses){
            Require-OnlineFields $record @('startedUtc','exitedUtc') 'services_actual_process_exit_time_missing'
            $start=Online-UtcTicks $record.startedUtc;$finish=Online-UtcTicks $record.exitedUtc
            Assert-Online ($finish -ge $start) 'services_process_lifetime_invalid'
            if($record.role -cne 'server'){
                Assert-Online ($record.executable -ieq $clientExecutable) 'services_client_not_packaged_executable'
                $timeline+=@([pscustomobject]@{ticks=$start;delta=1},[pscustomobject]@{ticks=$finish;delta=-1})
            }
        }
        foreach($phase in @('register','login')){
            Assert-Online ((Equal-Online @($ownedProcesses|Where-Object phase -CEQ $phase|ForEach-Object role|Sort-Object) @('client1','client2','server'))) 'services_phase_process_roles_invalid'
        }
        $active=0;$peak=0
        foreach($point in @($timeline|Sort-Object ticks,delta)){$active+=$point.delta;$peak=[math]::Max($peak,$active)}
        Assert-Online ($active -eq 0 -and $peak -eq 2 -and $timeline.Count -eq 8) 'services_client_concurrency_not_two'
        $first=@($servers|Where-Object phase -CEQ 'register')[0];$second=@($servers|Where-Object phase -CEQ 'login')[0]
        Assert-Online ((Online-UtcTicks $first.exitedUtc) -le (Online-UtcTicks $second.startedUtc)) 'services_editor_servers_overlapped'
        $summary.unit4LegacyServices.actualPackagedClientStarts=4
        $summary.unit4LegacyServices.actualPackagedClientPeak=2
        $summary.unit4LegacyServices.actualSequentialEditorServers=2
        $summary.unit4LegacyServices.safeLeaveAndApiRestartFullProfileExact=$true
        $summary.unit4LegacyServices['reloginReports']=@('login/client1.json','login/client2.json','login/server.json')
        $summary.unit4LegacyServices.status='passed'
    }
    if($Scenario -ceq 'party-results'){
        $startedClients=@($ownedProcesses|Where-Object role -IN @('client1','client2'))
        $interrupted=@($ownedProcesses|Where-Object forcedForScenario -EQ $true)
        $resumed=@($startedClients|Where-Object {$_.role -ceq 'client2' -and $_.phase -ceq 'register-resume'})
        Assert-Online ($startedClients.Count -eq 5 -and $resumed.Count -eq 1 -and $interrupted.Count -eq 1 -and $interrupted[0].role -ceq 'client2' -and $interrupted[0].phase -ceq 'register' -and $interrupted[0].exitCode -ne $null -and @($ownedProcesses|Where-Object cleanupForced -EQ $true).Count -eq 0) 'results_planned_five_starts_or_one_interruption_mismatch'
        Assert-Online ($summary.partyResults.status -ceq 'running' -and $summary.phases.register.status -ceq 'passed' -and $summary.phases.login.resumeWalletItemsSummaryExact -eq $true -and $summary.backendRestart.sameDatabaseIdentity -eq $true -and $summary.backendRestart.healthy -eq $true) 'results_final_durable_verification_missing'
        $summary.partyResults.plannedClient2InterruptionOnce=$true
        $summary.partyResults.actualPackagedClientStarts=$startedClients.Count
        $summary.partyResults.safeLeaveAndApiRestartFullProfileExact=$true
        $summary.partyResults.status='passed'
    }
    if($TownTravelScenario -ceq 'unit2-legacy-anchors'){
        # 유지한 process handle의 실제 시작/종료 시각으로 동시 수를 계산한다.
        $timeline=@()
        foreach($record in @($ownedProcesses|Where-Object role -IN @('client1','client2'))){
            $handle=$processHandles[[int]$record.pid]
            Assert-Online ($null -ne $handle -and $handle.HasExited -and $record.exitCode -ne $null) 'town_process_exit_evidence_missing'
            $start=Online-UtcTicks $record.startedUtc;$finish=$handle.ExitTime.ToUniversalTime().Ticks
            Assert-Online ($finish -ge $start) 'town_process_timeline_invalid'
            $timeline+=@([pscustomobject]@{ticks=$start;delta=1},[pscustomobject]@{ticks=$finish;delta=-1})
        }
        $active=0;$peak=0
        foreach($entry in @($timeline|Sort-Object ticks,delta)){$active+=$entry.delta;$peak=[math]::Max($peak,$active)}
        Assert-Online ($active -eq 0 -and $peak -eq 2 -and $timeline.Count -eq 10) 'town_client_start_or_peak_mismatch'
        $summary.unit2TownTravel.gates['u2_packaged_client_starts']=[int]($timeline.Count/2)
        $summary.unit2TownTravel.gates['u2_packaged_client_peak']=[int]$peak
        $summary.unit2TownTravel.gates['u2_existing_results_conditions']=($summary.partyResults.status -ceq 'passed')
        $summary.unit2TownTravel.gates['u2_normal_results_reconnect_preserved']=($summary.partyResults.status -ceq 'passed' -and $summary.partyResults.plannedClient2InterruptionOnce -eq $true)
        $summary.unit2TownTravel.gates['u2_api_restart_profile_preserved']=($summary.phases.login.resumeWalletItemsSummaryExact -eq $true -and $summary.backendRestart.sameDatabaseIdentity -eq $true -and $summary.backendRestart.healthy -eq $true)
        Assert-Online ($summary.unit2TownTravel.gates.Count -eq 32 -and $summary.unit2TownTravel.gates.u2_existing_results_conditions -and $summary.unit2TownTravel.gates.u2_normal_results_reconnect_preserved -and $summary.unit2TownTravel.gates.u2_api_restart_profile_preserved) 'town_final_existing_evidence_missing'
        $summary.unit2TownTravel.status='passed'
    }
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
