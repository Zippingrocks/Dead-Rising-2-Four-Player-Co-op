param(
    [ValidateRange(2, 4)]
    [int]$Instances = 4,
    [ValidateSet(2, 4)]
    [int]$SessionPlayers = 4,
    [ValidateRange(20, 3600)]
    [int]$DurationSeconds = 90,
    [ValidateRange(1, 30)]
    [int]$LaunchStaggerSeconds = 10,
    [ValidateRange(1024, 8192)]
    [int]$DesktopHeadroomMB = 2048,
    [bool]$UseIsolatedDesktops = $false,
    [string]$GameRoot = "C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2",
    [switch]$MenuProbe,
    [switch]$LobbyProbe,
    [switch]$CampaignMenuProbe,
    [switch]$CampaignLaunchProbe,
    [switch]$LobbyJoinProbe,
    [switch]$LobbyClientOnlyProbe,
    [switch]$AllowNullRefFrontend,
    [switch]$LobbyNativeHostProbe,
    [switch]$CampaignAdmissionProbe,
    [switch]$ConfirmCampaignAdmission,
    [switch]$DirectHostProbe,
    [switch]$CrashMonitor,
    [switch]$SaveProbe,
    [string]$SaveFixturePath,
    [switch]$ThreadSnapshots,
    [switch]$NativeUpdateStallSnapshots,
    [switch]$SeparateConfirmedJoiners,
    [switch]$StockTransport = $true,
    [switch]$SequentialJoins = $true,
    [switch]$ActorActivationProbe,
    [switch]$ClientTransitionProbe,
    [switch]$HostStateTransferProbe,
    [switch]$NativeFlowProbe,
    [switch]$NfsOwnershipProbe,
    [switch]$JipBroadcastQueueProbe,
    [switch]$PauseAcknowledgementProbe,
    [switch]$LoaderWaitProbe,
    [switch]$PrivateMouseProbe,
    [switch]$MeshListenerProbe,
    [switch]$ClothingCapacityProbe,
    [switch]$ClothingVariantsProbe,
    [switch]$StockSafehouseContent,
    [switch]$StockStreamedAssetsContent,
    [switch]$NetworkSnapshots,
    [switch]$KeepRunning
)

# Hidden local integration harness. Campaign probes use disposable per-instance save storage, not Steam Cloud.
# Process survival, native admission, mesh readiness, and actual gameplay are separate evidence gates.
$ErrorActionPreference = "Stop"
function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
        finally { $sha.Dispose() }
    } finally { $stream.Dispose() }
}
if ($Instances -gt $SessionPlayers) { throw 'Instances must not exceed SessionPlayers' }
if ($NfsOwnershipProbe -and (-not $NativeFlowProbe -or $SessionPlayers -ne 4)) {
    throw 'NFS ownership probe requires four-player native-flow control'
}
if ($JipBroadcastQueueProbe -and (-not $NativeFlowProbe -or $SessionPlayers -ne 4)) {
    throw 'JIP broadcast queue requires four-player native-flow control'
}
if ($PauseAcknowledgementProbe -and (-not $NativeFlowProbe -or $SessionPlayers -ne 4)) {
    throw 'Pause acknowledgement probe requires four-player native-flow control'
}
if (($StockSafehouseContent -or $StockStreamedAssetsContent) -and $KeepRunning) { throw 'Temporary content requires automatic child cleanup' }
if ($NativeFlowProbe -and (-not $HostStateTransferProbe -or -not $ConfirmCampaignAdmission -or $ClientTransitionProbe)) {
    throw 'Native flow control requires transfer observation and confirmed admission, without forced client transition'
}
if ($ConfirmCampaignAdmission -and (-not $CampaignAdmissionProbe -or -not $LobbyNativeHostProbe -or -not $LobbyJoinProbe)) {
    throw 'Explicit admission confirmation requires native lobby joining and the admission probe'
}
if ($SeparateConfirmedJoiners -and -not $ConfirmCampaignAdmission) {
    throw 'Confirmed-joiner separation requires explicit campaign admission confirmation'
}
if ($SaveFixturePath -and -not (Test-Path -LiteralPath $SaveFixturePath -PathType Leaf)) {
    throw "Save fixture does not exist: $SaveFixturePath"
}
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runRoot = Join-Path $workspace ("runtime_logs\coop\four_instance_{0:yyyyMMdd_HHmmss}" -f (Get-Date))
$gameExe = Join-Path $GameRoot "deadrising2.exe"
$renderPath = Join-Path $env:USERPROFILE "Documents\My Games\Dead Rising 2\rendersettings.ini"
$renderBackup = Join-Path $runRoot "rendersettings.original.ini"
$samplesPath = Join-Path $runRoot "resources.csv"
$memoryPath = Join-Path $runRoot "memory.csv"
$outcome = [ordered]@{
    Status = 'starting'; Reason = $null; StartedAt = (Get-Date).ToString('o'); Processes = @()
    Configuration = @{ Instances = $Instances; SessionPlayers = $SessionPlayers; DurationSeconds = $DurationSeconds;
        DesktopHeadroomMB = $DesktopHeadroomMB;
        DirectHostProbe = [bool]$DirectHostProbe; StockTransport = [bool]$StockTransport;
        SequentialJoins = [bool]$SequentialJoins; CrashMonitor = [bool]$CrashMonitor;
        ActorActivationProbe = [bool]$ActorActivationProbe;
        ClientTransitionProbe = [bool]$ClientTransitionProbe;
        HostStateTransferProbe = [bool]$HostStateTransferProbe;
        NativeFlowProbe = [bool]$NativeFlowProbe;
        NfsOwnershipProbe = [bool]$NfsOwnershipProbe;
        JipBroadcastQueueProbe = [bool]$JipBroadcastQueueProbe;
        PauseAcknowledgementProbe = [bool]$PauseAcknowledgementProbe;
        LoaderWaitProbe = [bool]$LoaderWaitProbe;
        PrivateMouseProbe = [bool]$PrivateMouseProbe;
        MeshListenerProbe = [bool]$MeshListenerProbe;
        ClothingCapacityProbe = [bool]$ClothingCapacityProbe;
        ClothingVariantsProbe = [bool]$ClothingVariantsProbe;
        StockSafehouseContent = [bool]$StockSafehouseContent;
        StockStreamedAssetsContent = [bool]$StockStreamedAssetsContent;
        NetworkSnapshots = [bool]$NetworkSnapshots;
        SaveProbe = [bool]$SaveProbe; SaveFixturePath = $SaveFixturePath; ThreadSnapshots = [bool]$ThreadSnapshots;
        NativeUpdateStallSnapshots = [bool]$NativeUpdateStallSnapshots;
        SeparateConfirmedJoiners = [bool]$SeparateConfirmedJoiners;
        MenuProbe = [bool]$MenuProbe; LobbyProbe = [bool]$LobbyProbe;
        CampaignMenuProbe = [bool]$CampaignMenuProbe; CampaignLaunchProbe = [bool]$CampaignLaunchProbe;
        LobbyJoinProbe = [bool]$LobbyJoinProbe; LobbyClientOnlyProbe = [bool]$LobbyClientOnlyProbe;
        AllowNullRefFrontend = [bool]$AllowNullRefFrontend; LobbyNativeHostProbe = [bool]$LobbyNativeHostProbe;
        CampaignAdmissionProbe = [bool]$CampaignAdmissionProbe;
        ConfirmCampaignAdmission = [bool]$ConfirmCampaignAdmission }
    RuntimeSha256 = $null
}
$started = @()
$contentSwap = @()
$networkSnapshotIndex = 0
$monitors = @()
$stallWatchers = @()
$monitorExe = Join-Path $workspace 'tools\dr2-crash-dump-monitor\bin\Release\net8.0\DR2CrashDumpMonitor.exe'
$desktops = @()
$previousSteamAppId = $env:SteamAppId
$previousSteamGameId = $env:SteamGameId
$previousSaveRoot = $env:DR2_COOP_SAVE_ROOT
$previousDumpRoot = $env:DR2_COOP_DUMP_ROOT

Add-Type -Namespace CoopHarness -Name Window -MemberDefinition @'
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
'@

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace CoopHarness {
    public static class Memory {
        [StructLayout(LayoutKind.Sequential)]
        public class Status {
            public uint Length = (uint)Marshal.SizeOf(typeof(Status));
            public uint Load;
            public ulong TotalPhysical, AvailablePhysical, TotalCommit, AvailableCommit;
            public ulong TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GlobalMemoryStatusEx([In, Out] Status status);
        public static Status Read() {
            Status status = new Status();
            if (!GlobalMemoryStatusEx(status))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            return status;
        }
    }
    public static class DesktopProcess {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct STARTUPINFO {
            public int cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }
        [StructLayout(LayoutKind.Sequential)]
        public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devMode, int flags, uint access, IntPtr attributes);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool CloseDesktop(IntPtr desktop);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes,
            IntPtr threadAttributes, bool inheritHandles, uint creationFlags, IntPtr environment,
            string currentDirectory, ref STARTUPINFO startup, out PROCESS_INFORMATION process);
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr handle);

        public static int Start(string executable, string arguments, string currentDirectory, string desktopName,
                                out IntPtr desktop) {
            desktop = CreateDesktopW(desktopName, IntPtr.Zero, IntPtr.Zero, 0, 0x000F01FF, IntPtr.Zero);
            if (desktop == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            STARTUPINFO startup = new STARTUPINFO();
            startup.cb = Marshal.SizeOf(typeof(STARTUPINFO));
            startup.lpDesktop = desktopName;
            startup.dwFlags = 1;
            startup.wShowWindow = 5;
            PROCESS_INFORMATION process;
            StringBuilder command = new StringBuilder("\"" + executable + "\" " + arguments);
            if (!CreateProcessW(executable, command, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero,
                                currentDirectory, ref startup, out process)) {
                int error = Marshal.GetLastWin32Error();
                CloseDesktop(desktop);
                desktop = IntPtr.Zero;
                throw new System.ComponentModel.Win32Exception(error);
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return process.dwProcessId;
        }
    }
}
'@

function Stop-HarnessProcesses {
    foreach ($process in $started) {
        $process.Refresh()
        if ($process.HasExited) { continue }
        # Stop-Process uses FFFFFFFF, indistinguishable from abnormal teardown to the debugger.
        # Record our action and use an explicit normal code only for children we still own.
        $record = [ordered]@{ Timestamp = (Get-Date).ToString('o'); Pid = $process.Id; ExitCode = 0; Requested = $true }
        $record | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $runRoot 'cleanup.jsonl')
        if (-not [CoopHarness.DesktopProcess]::TerminateProcess($process.Handle, 0)) {
            $process.Refresh()
            if (-not $process.HasExited) { Write-Warning "Could not stop harness child $($process.Id)" }
        }
    }
}

function Assert-HarnessProcessesAlive {
    foreach ($process in $started) {
        $process.Refresh()
        if ($process.HasExited) {
            $outcome.Status = 'process-exited'
            $outcome.Reason = "Harness child $($process.Id) exited early with code $($process.ExitCode)"
            throw $outcome.Reason
        }
    }
}

function Test-MemoryBudget([int]$remainingInstances = 0) {
    $memory = [CoopHarness.Memory]::Read()
    $commitMB = [math]::Round($memory.AvailableCommit / 1MB)
    $physicalMB = [math]::Round($memory.AvailablePhysical / 1MB)
    [pscustomobject]@{
        Timestamp = (Get-Date).ToString('o')
        AvailableCommitMB = $commitMB
        AvailablePhysicalMB = $physicalMB
        RemainingInstances = $remainingInstances
    } | Export-Csv -LiteralPath $memoryPath -NoTypeInformation -Append
    # Recent boot probes use ~600 MB private memory each. Keep desktop headroom as well.
    $requiredMB = $DesktopHeadroomMB + 700 * $remainingInstances
    if ($commitMB -lt $requiredMB -or $physicalMB -lt $requiredMB) {
        $outcome.Status = 'resource-aborted'
        $outcome.Reason = "Memory guard: ${commitMB} MB commit / ${physicalMB} MB physical free; ${requiredMB} MB required"
        return $false
    }
    return $true
}

function Pulse-Key([int]$instance, [int]$scanCode) {
    $path = Join-Path $GameRoot "coop_input.$instance.txt"
    [IO.File]::WriteAllText($path, [string]$scanCode)
    Start-Sleep -Milliseconds 280
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 280
}

function Read-InstanceTrace([int]$instance) {
    $name = if ($instance -eq 0) { 'coop_net.log' } else { "coop_net.$instance.log" }
    $path = Join-Path $GameRoot $name
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream)
    try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $header = [regex]::Match($text, '(?m)^.*co-op runtime loaded:.* instance=(\d+) requestedPlayers=\d+ pid=(\d+)\r?$')
    if (-not $header.Success -or [int]$header.Groups[1].Value -ne $instance -or
        [int]$header.Groups[2].Value -ne $started[$instance].Id) { return '' }
    return $text
}

function Wait-InstanceTrace([int]$instance, [string]$pattern, [int]$requiredMatches = 1,
                            [int]$timeoutSeconds = 20) {
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    do {
        Assert-HarnessProcessesAlive
        if (-not (Test-MemoryBudget)) { throw $outcome.Reason }
        if ([regex]::Matches((Read-InstanceTrace $instance), $pattern).Count -ge $requiredMatches) { return }
        Start-Sleep -Milliseconds 200
    } while ($deadline.Elapsed.TotalSeconds -lt $timeoutSeconds)
    throw "Instance $instance did not produce expected trace within ${timeoutSeconds}s: $pattern"
}

function Pulse-AllKeys([int]$scanCode, [int]$holdMilliseconds = 1800) {
    Pulse-InstanceKeys @(0..($Instances - 1)) $scanCode $holdMilliseconds
}

function Pulse-InstanceKeys([int[]]$targetInstances, [int]$scanCode, [int]$holdMilliseconds = 1800) {
    $down = "(?m)script input: DIK=$scanCode down\r?$"
    $up = "(?m)script input: DIK=$scanCode up\r?$"
    $counts = @{}
    try {
        foreach ($instance in $targetInstances) {
            $trace = Read-InstanceTrace $instance
            $counts[$instance] = @(([regex]::Matches($trace, $down).Count + 1), ([regex]::Matches($trace, $up).Count + 1))
            [IO.File]::WriteAllText((Join-Path $GameRoot "coop_input.$instance.txt"), [string]$scanCode)
        }
        foreach ($instance in $targetInstances) { Wait-InstanceTrace $instance $down $counts[$instance][0] 5 }
        Start-Sleep -Milliseconds $holdMilliseconds
    } finally {
        foreach ($instance in $targetInstances) {
            Remove-Item -LiteralPath (Join-Path $GameRoot "coop_input.$instance.txt") -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($instance in $targetInstances) {
        try {
            Wait-InstanceTrace $instance $up $counts[$instance][1] 20
        } catch {
            Write-Warning "Instance $instance did not acknowledge frontend key release for DIK=$scanCode after command removal: $($_.Exception.Message)"
        }
    }
    Start-Sleep -Seconds 2
}

function Request-Captures {
    Assert-HarnessProcessesAlive
    if (-not (Test-MemoryBudget)) { throw $outcome.Reason }
    foreach ($instance in 0..($Instances - 1)) {
        [IO.File]::WriteAllText((Join-Path $GameRoot "coop_capture.$instance.flag"), "capture")
    }
    Start-Sleep -Seconds 2
    if ($NetworkSnapshots) {
        for ($instance = 0; $instance -lt $started.Count; $instance++) {
            $path = Join-Path $runRoot "network-snapshot.$instance.$networkSnapshotIndex.json"
            $snapshot = & python (Join-Path $PSScriptRoot 'snapshot_connection_mesh.py') --pid $started[$instance].Id 2> "$path.stderr.txt"
            if ($LASTEXITCODE -eq 0) {
                $snapshot | Set-Content -LiteralPath $path
            } else {
                Write-Warning "Read-only network snapshot failed for instance $instance; see $path.stderr.txt"
            }
        }
        $path = Join-Path $runRoot "campaign-players.$networkSnapshotIndex.json"
        $players = & python (Join-Path $PSScriptRoot 'snapshot_campaign_players.py') --pid $started.Id 2> "$path.stderr.txt"
        if ($LASTEXITCODE -eq 0) {
            $players | Set-Content -LiteralPath $path
        } else {
            Write-Warning "Read-only campaign player snapshot failed; see $path.stderr.txt"
        }
        $script:networkSnapshotIndex++
    }
}

function Stage-StockSafehouse {
    if (-not $StockSafehouseContent -and -not $StockStreamedAssetsContent) { return }
    $originalRoot = Join-Path $workspace 'backups\dead_rising_2_pc\environment\safehouse'
    $liveRoot = Join-Path $GameRoot 'data\models\environment\safehouse'
    $backupRoot = Join-Path $runRoot 'content-before\safehouse'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $names = @('safehouse.big', 'safehouse_after.big', 'safehouse_breach.big', 'safehouse_laptop.big',
        'safehouse_persistent.big', 'safehouse_poker.big', 'zonelist.big', 'zonelist_safehouse_poker.big')
    $files = @(if ($StockSafehouseContent) { foreach ($name in $names) {
        [pscustomobject]@{Live=(Join-Path $liveRoot $name);Original=(Join-Path $originalRoot $name);
            Backup=(Join-Path $backupRoot $name)}
    } })
    if ($StockStreamedAssetsContent) {
        $files += [pscustomobject]@{Live=(Join-Path $GameRoot 'data\streamedassets.big');
            Original=(Join-Path $workspace 'backups\dead_rising_2_pc\streamedassets\streamedassets.big.original');
            Backup=(Join-Path $runRoot 'content-before\streamedassets.big')}
    }
    # Preserve and hash the complete set before changing any installed archive.
    $script:contentSwap = @(foreach ($file in $files) {
        $live = $file.Live
        $original = $file.Original
        $backup = $file.Backup
        $before = Get-Sha256 $live
        $stock = Get-Sha256 $original
        Copy-Item -LiteralPath $live -Destination $backup
        if ((Get-Sha256 $backup) -ne $before) { throw "Content backup mismatch: $live" }
        [pscustomobject]@{ Live = $live; Original = $original; Backup = $backup;
            BeforeSha256 = $before; StockSha256 = $stock; Installed = $false; Restored = $false }
    })
    $contentSwap | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'content-swap.json')
    foreach ($entry in $contentSwap) {
        try {
            Copy-Item -LiteralPath $entry.Original -Destination $entry.Live -Force
            if ((Get-Sha256 $entry.Live) -ne $entry.StockSha256) {
                throw "Staged content mismatch: $($entry.Live)"
            }
            $entry.Installed = $true
        } catch {
            Copy-Item -LiteralPath $entry.Backup -Destination $entry.Live -Force
            throw
        }
        $contentSwap | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'content-swap.json')
    }
}

function Restore-TestContent {
    foreach ($entry in $contentSwap) {
        if (-not $entry.Installed) { continue }
        try {
            if ((Get-Sha256 $entry.Live) -ne $entry.StockSha256) {
                throw 'Installed file changed during the test; preserving it instead of overwriting another edit'
            }
            if ((Get-Sha256 $entry.Backup) -ne $entry.BeforeSha256) {
                throw 'Pre-test backup no longer matches its recorded hash'
            }
            Copy-Item -LiteralPath $entry.Backup -Destination $entry.Live -Force
            $entry.Restored = (Get-Sha256 $entry.Live) -eq $entry.BeforeSha256
            if (-not $entry.Restored) { throw 'Restored content hash mismatch' }
        } catch {
            $outcome.Status = 'content-restore-failed'
            $outcome.Reason = "Content restore: $($entry.Live): $($_.Exception.Message)"
            Write-Warning $outcome.Reason
        }
    }
    if ($contentSwap.Count) {
        $contentSwap | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'content-swap.json')
    }
}

if (Get-Process deadrising2 -ErrorAction SilentlyContinue) { throw "deadrising2.exe is already running" }
foreach ($instance in 0..($Instances - 1)) {
    foreach ($name in @("coop_input.$instance.txt", "coop_mouse.$instance.txt")) {
        if (Test-Path -LiteralPath (Join-Path $GameRoot $name)) { throw "Stale input command must be preserved/reviewed before launch: $name" }
    }
}
if (-not (Test-Path -LiteralPath $gameExe)) { throw "Missing game executable: $gameExe" }
$outcome.RuntimeSha256 = Get-Sha256 (Join-Path $GameRoot 'dinput8.dll')
if (($CrashMonitor -or $ThreadSnapshots -or $NativeUpdateStallSnapshots) -and
    -not (Test-Path -LiteralPath $monitorExe)) { throw "Missing crash monitor: $monitorExe" }
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
if (Test-Path -LiteralPath $renderPath) { Copy-Item -LiteralPath $renderPath -Destination $renderBackup }

$lowRender = @"
// Temporary four-instance harness settings
RenderWidth = 640
RenderHeight = 360
FULLSCREEN = FULLSCREEN_DISABLED
VSync = false
Refresh = 60
MSAA = 0
Gamma = 1.0
COMBINED_BLUR = COMBINED_BLUR_DISABLED
ZOMBIE_COUNTS = ZOMBIE_COUNTS_NORMAL
SHADOW_QUALITY = SHADOW_QUALITY_LOW
TEXTURE_FILTERING = TF_BILINEAR
"@

try {
    if (-not (Test-MemoryBudget $Instances)) { throw $outcome.Reason }
    Stage-StockSafehouse
    "Timestamp,Instance,Pid,Alive,WorkingSetMB,PrivateMB,TotalCPUSeconds,Handles" | Set-Content -LiteralPath $samplesPath
    [IO.File]::WriteAllText($renderPath, $lowRender)
    # Steam's developer-launch environment prevents RestartAppIfNecessary from replacing our process and dropping
    # its harness arguments. These values are inherited only by the children and restored below.
    $env:SteamAppId = "45740"
    $env:SteamGameId = "45740"
    foreach ($instance in 0..($Instances - 1)) {
        if (-not (Test-MemoryBudget ($Instances - $instance))) { throw $outcome.Reason }
        $fps = if ($MenuProbe -or $LobbyProbe -or $CampaignMenuProbe -or $CampaignLaunchProbe) { 60 } elseif ($instance -eq 0) { 30 } else { 15 }
        $arguments = "-campaign=vanilla_dr2 -cooptrace -coopinstance=$instance -coopplayers=$SessionPlayers -cooptestinstances=$Instances -coopfps=$fps -coopsilent"
        $env:DR2_COOP_SAVE_ROOT = Join-Path $runRoot "save.$instance"
        $env:DR2_COOP_DUMP_ROOT = $runRoot
        New-Item -ItemType Directory -Path $env:DR2_COOP_SAVE_ROOT | Out-Null
        if ($SaveFixturePath) {
            $privateSaveName = "C4P{0}SAVE.DR2S" -f $instance
            Copy-Item -LiteralPath $SaveFixturePath -Destination (Join-Path $env:DR2_COOP_SAVE_ROOT $privateSaveName)
        }
        if ($SaveProbe) { $arguments += ' -coopsaveprobe' }
        if (($MenuProbe -or $LobbyProbe -or $CampaignMenuProbe -or $CampaignLaunchProbe) -and -not $AllowNullRefFrontend) {
            $arguments += ' -coophardwarerequired'
        }
        if ($DirectHostProbe) {
            $arguments += if ($instance -eq 0) { " -coopdirecthost" } else { " -coopdirectclient" }
        }
        if ($LobbyNativeHostProbe -and $instance -eq 0) { $arguments += ' -coopdirecthost' }
        if ($StockTransport) { $arguments += ' -coopstocktransport' }
        if ($SequentialJoins) { $arguments += ' -coopsequentialjoins' }
        if ($ActorActivationProbe) { $arguments += ' -coopactoractivate' }
        if ($ClientTransitionProbe) { $arguments += ' -coopclienttransition' }
        if ($HostStateTransferProbe) { $arguments += ' -coophoststatetransfer' }
        if ($NativeFlowProbe) { $arguments += ' -coopnativeflowprobe' }
        if ($NfsOwnershipProbe) { $arguments += ' -coopnfsownershipprobe' }
        if ($JipBroadcastQueueProbe) { $arguments += ' -coopjipbroadcastqueue' }
        if ($PauseAcknowledgementProbe) { $arguments += ' -cooppauseackprobe' }
        if ($LoaderWaitProbe) { $arguments += ' -cooploaderwaitprobe' }
        if ($PrivateMouseProbe) { $arguments += ' -coopprivatemouse' }
        if ($MeshListenerProbe) { $arguments += ' -coopmeshlistenerprobe' }
        if ($ClothingCapacityProbe) { $arguments += ' -coopclothingcapacityprobe' }
        if ($ClothingVariantsProbe) { $arguments += ' -coopclothingvariants' }
        if ($LobbyProbe) { $arguments += ' -cooplobbyfrontend' }
        if ($UseIsolatedDesktops) {
            $arguments += " -coopdesktop"
            [IntPtr]$desktop = [IntPtr]::Zero
            $childPid = [CoopHarness.DesktopProcess]::Start($gameExe, $arguments, $GameRoot,
                "DR2CoopHarness_$PID`_$instance", [ref]$desktop)
            $desktops += $desktop
            $process = [Diagnostics.Process]::GetProcessById($childPid)
        } else {
            $process = Start-Process -FilePath $gameExe -ArgumentList $arguments -WorkingDirectory $GameRoot `
                -WindowStyle Hidden -PassThru
        }
        $started += $process
        if ($CrashMonitor) {
            $monitorRoot = Join-Path $runRoot "debugger.$instance"
            New-Item -ItemType Directory -Path $monitorRoot | Out-Null
            $monitorArgs = '--pid {0} --out-dir "{1}" --max-dumps 1' -f $process.Id, $monitorRoot
            $monitors += Start-Process -FilePath $monitorExe -ArgumentList $monitorArgs -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput (Join-Path $monitorRoot 'stdout.txt') -RedirectStandardError (Join-Path $monitorRoot 'stderr.txt')
        }
        Start-Sleep -Seconds $LaunchStaggerSeconds
        Assert-HarnessProcessesAlive
        try {
            $process.PriorityClass = if ($instance -eq 0) { "BelowNormal" } else { "Idle" }
            # Give each process four logical processors. This is a test throttle, not a shipping requirement.
            $process.ProcessorAffinity = [IntPtr]([int64]0xF -shl ($instance * 4))
        } catch {
            Write-Warning "Could not apply resource limits to instance $instance`: $($_.Exception.Message)"
        }
    }

    if ($MenuProbe -or $LobbyProbe -or $CampaignMenuProbe -or $CampaignLaunchProbe) {
        # The runtime converts the rating-logo screen's own first named event into its native
        # animation_done transition. Observe the following asynchronous title load without injecting
        # input; early key pulses obscure its true completion time on background clients.
        Start-Sleep -Seconds 15
        Request-Captures
        foreach ($sample in 1..3) {
            Start-Sleep -Seconds 15
            Request-Captures
        }
        Pulse-AllKeys 28
        Start-Sleep -Seconds 8
        Request-Captures
        if ($CampaignMenuProbe) {
            # Advance only the host from the main menu. Joiners remain available for the already-established
            # native session while this probe records the first campaign route screen.
            Pulse-InstanceKeys @(0) 28
            Start-Sleep -Seconds 8
            Request-Captures
            if ($CampaignLaunchProbe) {
                # Confirm only the host's currently selected incoming-call row, then preserve both the immediate
                # response and the delayed transition. Save I/O remains confined to this run's disposable root.
                Pulse-InstanceKeys @(0) 28
                Start-Sleep -Seconds 8
                Request-Captures
                Start-Sleep -Seconds 12
                Request-Captures
            }
        }
        if ($LobbyProbe) {
            # LobbyProbe validates DR2's real browser/callback path on every surviving client.
            # A dedicated host/client orchestration mode will split Player 1 out after this gate passes.
            $joiners = if ($CampaignLaunchProbe -or $LobbyClientOnlyProbe) {
                @(1..($Instances - 1))
            } else {
                @(0..($Instances - 1))
            }
            Pulse-InstanceKeys $joiners 208
            Pulse-InstanceKeys $joiners 28
            Start-Sleep -Seconds 8
            Request-Captures
            # Force DR2 to consume the first Enter release through its buffered-input path,
            # then return to Join Online Game before the final confirm.
            Pulse-InstanceKeys $joiners 208
            Pulse-InstanceKeys $joiners 200
            Pulse-InstanceKeys $joiners 28
            Start-Sleep -Seconds 8
            Request-Captures
            # The first confirm enters Join Co-op Game and raises DR2's native client-save warning.
            # Dismiss that modal only after a captured observation proves it was actually reached.
            Pulse-InstanceKeys $joiners 28
            Start-Sleep -Seconds 8
            Request-Captures
            if ($LobbyJoinProbe) {
                # The warning dismissal lands on the local gamer-profile/save-slot picker. Select the first
                # disposable slot, then preserve the immediate and delayed lobby/join transitions. Native host
                # admission is serialized because cLocalServer keeps a real busy flag while each join completes.
                if ($LobbyNativeHostProbe) {
                    foreach ($joiner in $joiners) {
                        Pulse-InstanceKeys @($joiner) 28
                        Start-Sleep -Seconds 15
                        Request-Captures
                        # A key press is not a join. Require this process's frontend callback before accepting it.
                        Wait-InstanceTrace $joiner ("(?m)loopback lobby: Player {0} joined lobby\r?$" -f ($joiner + 1))
                        if ($CampaignAdmissionProbe) {
                            # A successful lobby/P2P join raises the host's stock Incoming co-op call prompt.
                            # C opens the native YES/NO admission dialog; it does not accept the caller.
                            Pulse-InstanceKeys @(0) 46 100
                            Start-Sleep -Seconds 12
                            Request-Captures
                            if ($ConfirmCampaignAdmission) {
                                [pscustomobject]@{ Timestamp=(Get-Date).ToString('o'); Instance=0;
                                    Caller=$joiner; ScanCode=28; Action='Confirm native admission YES after C and capture' } |
                                    ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $runRoot 'admission-input.jsonl')
                                Pulse-InstanceKeys @(0) 28 100
                                Start-Sleep -Seconds 8
                                # All four D3D9 devices can still be settling after the final admission.
                                # The native confirmed-member trace is the gate here; avoid a simultaneous
                                # frame readback when a native-update watchdog run will take later evidence.
                                if (-not $NativeUpdateStallSnapshots) { Request-Captures }
                            }
                        }
                        if ($ConfirmCampaignAdmission) {
                            $peer = '{0:X16}' -f ([uint64]0x0110000170000000 + $joiner)
                            Wait-InstanceTrace 0 ("client table: slot=\d+ peer=$peer index=\d+ confirmed=1 leaving=0")
                        }
                    }
                    if ($SeparateConfirmedJoiners) {
                        # Membership confirmation precedes local actor activation. Wait for every owner to
                        # leave single-player mode, then disperse the three joiners before their capsules can
                        # settle into the same safehouse collision pocket. This remains native private input.
                        $separationKeys = @(31, 32, 17)
                        foreach ($joiner in @($joiners | Sort-Object -Descending)) {
                            Wait-InstanceTrace $joiner ("campaign actors:.*localSlot=$joiner singlePlayer=0 gameType=1") 1 60
                            $scanCode = $separationKeys[$joiner - 1]
                            [pscustomobject]@{ Timestamp=(Get-Date).ToString('o'); Instance=$joiner;
                                Caller=$joiner; ScanCode=$scanCode; Action='Separate activated joiner after complete admission' } |
                                ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $runRoot 'admission-input.jsonl')
                            Pulse-InstanceKeys @($joiner) $scanCode 1200
                        }
                    }
                } else {
                    Pulse-InstanceKeys $joiners 28
                    Start-Sleep -Seconds 8
                    Request-Captures
                    Start-Sleep -Seconds 12
                    Request-Captures
                }
            }
        }
    }

    if ($ThreadSnapshots) {
        foreach ($instance in 0..($started.Count - 1)) {
            $process = $started[$instance]
            if ($process.HasExited) { continue }
            $path = Join-Path $runRoot "threads.$instance.json"
            $snapshotArgs = '--pid {0} --threads-path "{1}"' -f $process.Id, $path
            $snapshot = Start-Process -FilePath $monitorExe -ArgumentList $snapshotArgs -WindowStyle Hidden -PassThru -Wait `
                -RedirectStandardOutput (Join-Path $runRoot "threads.$instance.stdout.txt") `
                -RedirectStandardError (Join-Path $runRoot "threads.$instance.stderr.txt")
            if ($snapshot.ExitCode -ne 0) { throw "Thread snapshot failed for instance $instance" }
        }
    }
    if ($NativeUpdateStallSnapshots) {
        $watcherScript = Join-Path $PSScriptRoot 'watch_native_update_stall.py'
        if (-not (Test-Path -LiteralPath $watcherScript)) { throw "Missing stall watcher: $watcherScript" }
        $hostProcess = $started[0]
        $watcherArgs = '"{0}" --pid {1} --log "{2}" --output "{3}" --monitor "{4}" --timeout-seconds {5}' -f
            $watcherScript, $hostProcess.Id, (Join-Path $GameRoot 'coop_net.log'),
            (Join-Path $runRoot 'threads-native-update-stall.0.json'), $monitorExe, $DurationSeconds
        $stallWatchers += Start-Process -FilePath (Get-Command python).Source -ArgumentList $watcherArgs `
            -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $runRoot 'native-update-stall-watcher.stdout.txt') `
            -RedirectStandardError (Join-Path $runRoot 'native-update-stall-watcher.stderr.txt')
    }
    $outcome.Status = 'observing'
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-MemoryBudget)) { break }
        $exited = $false
        for ($instance = 0; $instance -lt $started.Count; $instance++) {
            $process = $started[$instance]
            $process.Refresh()
            if ($process.HasExited) {
                $exited = $true
                $line = '"{0:o}",{1},{2},false,0,0,0,0' -f (Get-Date), $instance, $process.Id
            } else {
                $line = '"{0:o}",{1},{2},true,{3:N1},{4:N1},{5:N2},{6}' -f (Get-Date), $instance, $process.Id,
                    ($process.WorkingSet64 / 1MB), ($process.PrivateMemorySize64 / 1MB), $process.TotalProcessorTime.TotalSeconds,
                    $process.HandleCount
            }
            Add-Content -LiteralPath $samplesPath -Value $line
        }
        if ($exited) {
            $outcome.Status = 'process-exited'
            $outcome.Reason = 'At least one harness process exited before the observation deadline'
            break
        }
        Start-Sleep -Seconds 2
    }
    if ($outcome.Status -eq 'observing') {
        if ($NetworkSnapshots) { Request-Captures }
        $outcome.Status = 'observation-completed'
    }
} catch {
    if ($outcome.Status -notin @('resource-aborted', 'process-exited')) {
        $outcome.Status = 'harness-error'
        $outcome.Reason = $_.Exception.Message
    }
    Write-Warning $outcome.Reason
} finally {
    $env:SteamAppId = $previousSteamAppId
    $env:SteamGameId = $previousSteamGameId
    $env:DR2_COOP_SAVE_ROOT = $previousSaveRoot
    $env:DR2_COOP_DUMP_ROOT = $previousDumpRoot
    $outcome.Processes = @($started | ForEach-Object {
        $_.Refresh()
        [pscustomobject]@{ Pid = $_.Id; ExitedBeforeCleanup = $_.HasExited;
            ExitCode = if ($_.HasExited) { $_.ExitCode } else { $null } }
    })
    $outcome | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'outcome.json')
    foreach ($watcher in $stallWatchers) {
        $watcher.Refresh()
        if (-not $watcher.HasExited -and -not $watcher.WaitForExit(2000)) {
            Stop-Process -Id $watcher.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if (-not $KeepRunning -or $outcome.Status -ne 'observation-completed') { Stop-HarnessProcesses }
    foreach ($monitor in $monitors) {
        # Full-memory dumps can take minutes under simultaneous four-process I/O.
        # This affects debugger cleanup only, never a native gameplay timeout.
        if (-not $monitor.WaitForExit(180000)) {
            Write-Warning "Crash monitor $($monitor.Id) did not finish within 180 seconds; any dump may be incomplete"
            $outcome['CrashEvidenceIncomplete'] = $true
            Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($CrashMonitor) {
        try {
            $dumpCheck = & python (Join-Path $PSScriptRoot 'validate_minidump.py') --run-root $runRoot
            if ($LASTEXITCODE -ne 0) { throw 'Minidump checker failed' }
            $dumpCheck | Set-Content -LiteralPath (Join-Path $runRoot 'dump-validation.json')
            if (($dumpCheck | ConvertFrom-Json).incomplete) { $outcome['CrashEvidenceIncomplete'] = $true }
        } catch {
            $outcome['CrashEvidenceIncomplete'] = $true
            Write-Warning 'Dump validation failed; retained files are not verified crash evidence'
        }
    }
    foreach ($desktop in $desktops) {
        if ($desktop -ne [IntPtr]::Zero) { [CoopHarness.DesktopProcess]::CloseDesktop($desktop) | Out-Null }
    }
    foreach ($instance in 0..($Instances - 1)) {
        Remove-Item -LiteralPath (Join-Path $GameRoot "coop_input.$instance.txt") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $GameRoot "coop_capture.$instance.flag") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $GameRoot "coop_mouse.$instance.txt") -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
    for ($instance = 0; $instance -lt $started.Count; $instance++) {
        $name = if ($instance -eq 0) { "coop_net.log" } else { "coop_net.$instance.log" }
        $source = Join-Path $GameRoot $name
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $runRoot }
        $runtimeName = "case_zero_runtime.$instance.log"
        $runtimeSource = Join-Path $GameRoot $runtimeName
        if (Test-Path -LiteralPath $runtimeSource) { Copy-Item -LiteralPath $runtimeSource -Destination $runRoot }
        # Captures are written directly into this unique D: run directory by the runtime.
    }
    if (Test-Path -LiteralPath $renderBackup) {
        Copy-Item -LiteralPath $renderBackup -Destination $renderPath -Force
    }
    Restore-TestContent
    $outcome | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'outcome.json')
}

$rows = if (Test-Path -LiteralPath $samplesPath) { Import-Csv -LiteralPath $samplesPath } else { @() }
$summary = foreach ($instance in 0..($Instances - 1)) {
    $live = @($rows | Where-Object { [int]$_.Instance -eq $instance -and $_.Alive -eq 'true' })
    [pscustomobject]@{
        Instance = $instance
        StayedAlive = $live.Count -gt 0 -and @($rows | Where-Object { [int]$_.Instance -eq $instance } | Select-Object -Last 1).Alive -eq 'true'
        PeakWorkingSetMB = if ($live) { [math]::Round(($live.WorkingSetMB | Measure-Object -Maximum).Maximum, 1) } else { 0 }
        PeakPrivateMB = if ($live) { [math]::Round(($live.PrivateMB | Measure-Object -Maximum).Maximum, 1) } else { 0 }
    }
}
$summary | Format-Table -AutoSize
$peakWorkingSetTotal = ($summary.PeakWorkingSetMB | Measure-Object -Sum).Sum
$peakPrivateTotal = ($summary.PeakPrivateMB | Measure-Object -Sum).Sum
"Approximate peak totals: working set {0:N1} MB, private memory {1:N1} MB" -f $peakWorkingSetTotal, $peakPrivateTotal
"Evidence: $runRoot"
& python (Join-Path $PSScriptRoot 'report_harness.py') $runRoot --instances $Instances --write
if ($LASTEXITCODE -ne 0) { throw 'Network evidence summary failed' }
