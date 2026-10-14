# SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
# SPDX-License-Identifier: MIT
# Administrative boundary for the optional LocalSystem keyboard/mouse service.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Install', 'Apply', 'Start', 'Stop', 'Status', 'Uninstall')]
    [string]$Action,
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [string]$SourceSettings
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding

function Assert-LocalPath([string]$Path, [switch]$MustExist) {
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Substring(2).Contains(':') -or
        $Path -match '[\x00-\x1f"<>|?*]' -or $Path -match '(^|[\\/])\.{1,2}([\\/]|$)' -or
        $Path -match '[. ]([\\/]|$)') { throw "Unsafe local path: $Path" }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($full.Length -eq 2) { $full += '\' }
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($full))
    if ($drive.DriveType -ne [IO.DriveType]::Fixed) { throw "A fixed local disk is required: $full" }
    $probe = [IO.Path]::GetPathRoot($full)
    $parts = @('') + @($full.Substring($probe.Length).Split([char[]]'\/') | Where-Object { $_ })
    foreach ($part in $parts) {
        if ($part) { $probe = Join-Path $probe $part }
        if (Test-Path -LiteralPath $probe) {
            $item = Get-Item -Force -LiteralPath $probe
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse points are not allowed: $probe" }
        } elseif ($MustExist -and $probe -eq $full) { throw "Path does not exist: $full" }
    }
    return $full
}

function Assert-ProtectedAcl([string]$Path, [switch]$ParentOnly) {
    $acl = Get-Acl -LiteralPath $Path
    $descriptor = New-Object Security.AccessControl.RawSecurityDescriptor($acl.GetSecurityDescriptorBinaryForm(), 0)
    if ($null -eq $descriptor.DiscretionaryAcl) { throw "Unprotected null DACL: $Path" }
    $trusted = @('S-1-5-18', 'S-1-5-32-544')
    if ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -notin $trusted) {
        throw "Directory or file is not owned by Administrators/SYSTEM: $Path"
    }
    # A parent may allow creating siblings. DELETE_CHILD, WRITE_DAC and WRITE_OWNER
    # would permit replacing the protected child and are never accepted.
    [long]$mask = if ($ParentOnly) { 0x100C0040 } else { 0x500D0156 }
    foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
        if ($rule.AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
            ($rule.PropagationFlags -band [Security.AccessControl.PropagationFlags]::InheritOnly)) { continue }
        if ($rule.IdentityReference.Value -notin $trusted -and ([long]$rule.FileSystemRights -band $mask)) {
            throw "Unprivileged write/replace permission on protected path: $Path"
        }
    }
}

function New-ProtectedDirectory([string]$Path, [switch]$Private) {
    $security = New-Object Security.AccessControl.DirectorySecurity
    $security.SetAccessRuleProtection($true, $false)
    $admins = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'
    $security.SetOwner($admins)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $rule = New-Object Security.AccessControl.FileSystemAccessRule(
            (New-Object Security.Principal.SecurityIdentifier $sid), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $security.AddAccessRule($rule)
    }
    if (-not $Private) {
        $security.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule(
            (New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-545'), 'ReadAndExecute', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        [IO.Directory]::CreateDirectory($Path, $security) | Out-Null
    } else {
        Assert-LocalPath $Path -MustExist | Out-Null
        Assert-ProtectedAcl $Path
        Set-Acl -LiteralPath $Path -AclObject $security
    }
    Assert-LocalPath $Path -MustExist | Out-Null
    Assert-ProtectedAcl $Path
}

function Read-BoundedText([string]$Path, [long]$Limit = 1048576) {
    $Path = Assert-LocalPath $Path -MustExist
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -gt $Limit) { throw "File exceeds permitted size: $Path" }
        $reader = New-Object IO.StreamReader($stream, (New-Object Text.UTF8Encoding($false, $true)), $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Read-IniSettings([string]$Path) {
    $result = @{}
    $group = ''
    foreach ($line in (Read-BoundedText $Path) -split '\r?\n') {
        if ($line -match '^\s*(;|#|$)') { continue }
        if ($line -match '^\[([A-Za-z0-9_%.-]+)\]\s*$') { $group = $Matches[1]; continue }
        if ($line -notmatch '^([A-Za-z0-9_\\%.-]+)=(.*)$' -or -not $group) {
            throw 'Unsupported settings syntax; save settings with Deskflow before applying'
        }
        $key = $group + '/' + $Matches[1]
        if ($result.ContainsKey($key)) { throw "Duplicate settings key: $key" }
        $result[$key] = $Matches[2]
    }
    return $result
}

function ConvertTo-ControlSettings([hashtable]$InputSettings, [string]$Directory) {
    if ($InputSettings['core/coreMode'] -notin @('1', '2')) { throw 'Select a configured client or server first' }
    if ($InputSettings['server/externalConfig'] -in @('true', '1')) {
        throw 'Full control requires the built-in server configuration; external configuration is not imported'
    }
    $allowed = @{
        core = @('coreMode', 'computerName', 'interface', 'port', 'preventSleep', 'useHooks', 'language', 'fileTransferEnabled', 'fileTransferCachePath', 'fileTransferCacheLimitGiB')
        client = @('remoteHost', 'dynamicConnectionInterval', 'invertYScroll', 'invertXScroll', 'yScrollScale', 'xScrollScale', 'languageSync')
        server = @('clipboardSize', 'defaultLockToComputerState', 'disableLockToComputer', 'enableClipboard', 'enableHeartbeat', 'enableSwitchDelay', 'enableSwitchDoubleTap', 'heartbeat', 'protocol', 'relativeMouseMoves', 'switchDelay', 'switchDoubleTap', 'win32KeepForeground')
        log = @('level')
    }
    $output = @{}
    foreach ($key in $InputSettings.Keys) {
        $parts = $key.Split('/', 2)
        if (($allowed.ContainsKey($parts[0]) -and $parts[1] -cin $allowed[$parts[0]]) -or
            ($parts[0] -cmatch '^screen_[A-Za-z0-9_%.-]+$' -and $parts[1] -ceq 'aliases')) {
            $output[$key] = $InputSettings[$key]
        }
    }
    $base = $Directory.Replace('\', '/')
    $output['core/processMode'] = '0'
    $output['core/enableEnterCommand'] = 'false'
    $output['core/enableExitCommand'] = 'false'
    $output['daemon/elevate'] = 'true'
    $output['daemon/configFile'] = "$base/settings/Deskflow.conf"
    $output['daemon/logFile'] = "$base/logs/daemon.log"
    $output['daemon/logLevel'] = 'INFO'
    $output['log/toFile'] = 'true'
    $output['log/file'] = "$base/logs/core.log"
    $output['security/tlsEnabled'] = 'true'
    $output['security/checkPeerFingerprints'] = 'true'
    $output['security/certificate'] = "$base/settings/tls/deskflow.pem"
    $output['server/externalConfig'] = 'false'
    $output['server/externalConfigFile'] = "$base/settings/deskflow-server.conf"
    return $output
}

function Write-IniSettings([hashtable]$Settings, [string]$Path) {
    $lines = New-Object 'Collections.Generic.List[string]'
    $group = ''
    foreach ($key in ($Settings.Keys | Sort-Object)) {
        $parts = $key.Split('/', 2)
        if ($group -cne $parts[0]) {
            $group = $parts[0]
            $lines.Add("[$group]")
        }
        $lines.Add($parts[1] + '=' + $Settings[$key])
    }
    [IO.File]::WriteAllLines($Path, $lines, (New-Object Text.UTF8Encoding($false)))
}

function Assert-Component([string]$Directory) {
    Assert-LocalPath $Directory -MustExist | Out-Null
    Assert-ProtectedAcl ([IO.Path]::GetPathRoot($Directory)) -ParentOnly
    Assert-ProtectedAcl $Directory
    $marker = Join-Path $Directory 'deskflow-full-control.component'
    Assert-ProtectedAcl $marker
    if ((Read-BoundedText $marker 256).Trim() -cne 'Deskflow FileCopy Full Control v1') {
        throw "Unrecognized component directory: $Directory"
    }
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($Directory)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in component: $($item.FullName)" }
            Assert-ProtectedAcl $item.FullName
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}

function Get-OwnedService([string]$ExpectedCommand) {
    # ServiceController and the public service registry key are readable without
    # administrative WMI access, including on machines with restricted CIM.
    $controller = Get-Service -Name Deskflow -ErrorAction SilentlyContinue
    if (-not $controller) { return $null }
    try {
        $registration = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\Deskflow'
        if ($registration.ImagePath -ine $ExpectedCommand -or $registration.ObjectName -notin @('LocalSystem', 'NT AUTHORITY\SYSTEM')) {
            throw 'The Deskflow service belongs to another installation; it will not be modified'
        }
        return [pscustomobject]@{ State = $controller.Status.ToString() }
    } finally { $controller.Dispose() }
}

function Stop-ControlService {
    $service = Get-Service -Name Deskflow -ErrorAction SilentlyContinue
    if (-not $service) { return }
    try {
        if ($service.Status -ne 'Stopped') {
            Stop-Service -InputObject $service -ErrorAction Stop
            $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
        }
    } finally { $service.Dispose() }
}

function Start-ControlService {
    $service = Get-Service -Name Deskflow -ErrorAction Stop
    try {
        if ($service.Status -ne 'Running') {
            Start-Service -InputObject $service -ErrorAction Stop
            $service.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
        }
    } finally { $service.Dispose() }
}

function Invoke-ServiceCommand([string[]]$Arguments) {
    $message = & "$env:SystemRoot\System32\sc.exe" @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Service operation failed ($LASTEXITCODE): $message" }
}

function Copy-Runtime([string]$Source, [string]$Stage) {
    $marker = Read-BoundedText (Join-Path $Source 'deskflow-filecopy.package') 4096
    if (($marker -split '\r?\n')[0] -cne 'Deskflow FileCopy') { throw 'The source is not a Deskflow FileCopy package' }
    $files = New-Object 'Collections.Generic.List[string]'
    foreach ($name in @('deskflow-daemon.exe', 'deskflow-core.exe', 'deskflow-file-agent.exe')) { $files.Add($name) }
    foreach ($file in Get-ChildItem -LiteralPath $Source -Filter '*.dll' -File) { $files.Add($file.Name) }
    foreach ($relative in @('plugins\platforms\qwindows.dll', 'plugins\tls\qopensslbackend.dll', 'plugins\tls\qschannelbackend.dll', 'plugins\tls\qcertonlybackend.dll', 'plugins\networkinformation\qnetworklistmanager.dll')) {
        if (Test-Path -LiteralPath (Join-Path $Source $relative)) { $files.Add($relative) }
    }
    foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll')) {
        if ($required -notin $files) { throw "Missing required runtime: $required" }
    }
    foreach ($relative in $files) {
        $path = Assert-LocalPath (Join-Path $Source $relative) -MustExist
        $destination = Join-Path $Stage $relative
        $parent = [IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
        $inputFile = [IO.File]::Open($path, 'Open', 'Read', 'Read')
        try {
            if ($inputFile.Length -lt 64 -or $inputFile.Length -gt 268435456 -or $inputFile.ReadByte() -ne 77 -or $inputFile.ReadByte() -ne 90) {
                throw "Not a supported Windows runtime file: $relative"
            }
            $inputFile.Position = 0
            $outputFile = [IO.File]::Open($destination, 'CreateNew', 'Write', 'None')
            try { $inputFile.CopyTo($outputFile) } finally { $outputFile.Dispose() }
        } finally { $inputFile.Dispose() }
    }
    [IO.File]::WriteAllText((Join-Path $Stage 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=plugins`n", (New-Object Text.UTF8Encoding($false)))
    $files.Add('qt.conf')
    $managerText = Read-BoundedText (Join-Path $Source 'full-control-manager.ps1')
    [IO.File]::WriteAllText((Join-Path $Stage 'full-control-manager.ps1'), $managerText, (New-Object Text.UTF8Encoding($false)))
    $files.Add('full-control-manager.ps1')
    [IO.File]::WriteAllText((Join-Path $Stage 'deskflow-filecopy.package'), $marker, (New-Object Text.UTF8Encoding($false)))
    $files.Add('deskflow-filecopy.package')
    return $files.ToArray()
}

function New-SettingsSnapshot([string]$Source, [string]$Directory, [string]$Stage, [string]$ExistingDirectory = $Directory) {
    $sourceValues = Read-IniSettings $Source
    $values = ConvertTo-ControlSettings $sourceValues $Directory
    $sourceBase = [IO.Path]::GetDirectoryName($Source)
    New-ProtectedDirectory $Stage
    $tls = Join-Path $Stage 'tls'
    New-ProtectedDirectory $tls -Private
    if ($values['core/coreMode'] -eq '2') {
        $serverConfig = Read-BoundedText (Join-Path $sourceBase 'deskflow-server.conf')
        # The core parser supports these four data-only sections; reject include/command
        # extensions rather than ever interpreting a user-provided program or path.
        foreach ($line in $serverConfig -split '\r?\n') {
            if ($line -match '^\s*section:\s*(\S+)' -and $Matches[1] -cnotin @('screens', 'links', 'aliases', 'options')) {
                throw 'Unsupported server configuration section'
            }
            if ($line -match '(?i)\b(include|execute|exec|run|enterCommand|exitCommand)\s*[:=(]') {
                throw 'Commands and includes are not permitted in the protected server configuration'
            }
        }
        [IO.File]::WriteAllText((Join-Path $Stage 'deskflow-server.conf'), $serverConfig, (New-Object Text.UTF8Encoding($false)))
    }
    $protectedPem = Join-Path $ExistingDirectory 'settings\tls\deskflow.pem'
    $sourcePem = Join-Path $sourceBase 'tls\deskflow.pem'
    if (Test-Path -LiteralPath $protectedPem) { $pem = Read-BoundedText $protectedPem 131072 } else {
        if ($sourceValues.ContainsKey('security/certificate') -and $sourceValues['security/certificate']) {
            $configured = $sourceValues['security/certificate'].Trim('"').Replace('\\', '\').Replace('/', '\')
            if ($configured -ine $sourcePem) { throw 'Use the default TLS identity under the source settings/tls directory before enabling full control' }
        }
        $pem = Read-BoundedText $sourcePem 131072
    }
    if ($pem -notmatch '-----BEGIN CERTIFICATE-----' -or $pem -notmatch '-----BEGIN (RSA |EC |)PRIVATE KEY-----') {
        throw 'The TLS identity must contain both a certificate and a private key'
    }
    [IO.File]::WriteAllText((Join-Path $tls 'deskflow.pem'), $pem, (New-Object Text.UTF8Encoding($false)))
    foreach ($name in @('trusted-servers', 'trusted-clients')) {
        $pins = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($path in @((Join-Path $ExistingDirectory "settings\tls\$name"), (Join-Path $sourceBase "tls\$name"))) {
            if (-not (Test-Path -LiteralPath $path)) { continue }
            foreach ($line in (Read-BoundedText $path 65536) -split '\r?\n') {
                if (-not $line.Trim()) { continue }
                if ($line -cnotmatch '^v2:sha(1:[a-fA-F0-9]{40}|256:[a-fA-F0-9]{64})$' -and $line -notmatch '^([a-fA-F0-9]{2}:){19}[a-fA-F0-9]{2}$') {
                    throw "Invalid TLS fingerprint in $name"
                }
                $pins.Add($line.ToLowerInvariant()) | Out-Null
            }
        }
        [IO.File]::WriteAllLines((Join-Path $tls $name), [string[]]@($pins), (New-Object Text.UTF8Encoding($false)))
    }
    $requiredPins = if ($values['core/coreMode'] -eq '1') { 'trusted-servers' } else { 'trusted-clients' }
    if ((Get-Item -LiteralPath (Join-Path $tls $requiredPins)).Length -eq 0) {
        throw 'Trust the peer fingerprint in Desktop mode before enabling full control'
    }
    Write-IniSettings $values (Join-Path $Stage 'Deskflow.conf')
}

function Remove-Transaction([string]$Path, [string]$Directory) {
    $full = Assert-LocalPath $Path -MustExist
    if ([IO.Path]::GetDirectoryName($full) -ine $Directory -or [IO.Path]::GetFileName($full) -notmatch '^\.transaction-[a-f0-9]{32}$') {
        throw 'Refusing to remove a path outside the component transaction directory'
    }
    Assert-Component $Directory
    Remove-Item -LiteralPath $full -Recurse -Force
}

function Remove-FreshComponent([string]$Path, [string]$Target, [string]$Token, [string]$ExpectedCommand) {
    $full = Assert-LocalPath $Path -MustExist
    $targetFull = Assert-LocalPath $Target
    if ($Token -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid first-install ownership token' }
    $staging = Join-Path ([IO.Path]::GetPathRoot($targetFull)) ('.DeskflowFileCopyControl-install-' + $Token)
    if ($full -ine $targetFull -and $full -ine $staging) { throw 'Refusing to remove a directory not created by this install' }
    if (Get-OwnedService $ExpectedCommand) { throw 'Refusing to remove a registered full-control component' }
    Assert-Component $full
    if ((Read-BoundedText (Join-Path $full '.install-owner') 128).Trim() -cne $Token) {
        throw 'First-install cleanup ownership token does not match'
    }
    Remove-Item -LiteralPath $full -Recurse -Force
}

$directory = ''
$settings = ''
$mutex = $null
$locked = $false
$freshDirectory = ''
$freshToken = ''
$installCommitted = $false
try {
    $source = Assert-LocalPath $SourceDirectory -MustExist
    if (-not (Get-Item -LiteralPath $source).PSIsContainer) { throw 'SourceDirectory must be a directory' }
    $directory = Join-Path ([IO.Path]::GetPathRoot($source)) 'Deskflow FileCopy Control'
    $settings = Join-Path $directory 'settings\Deskflow.conf'
    $command = '"' + (Join-Path $directory 'deskflow-daemon.exe') + '" --full-control --settings "' + $settings + '"'
    $service = Get-OwnedService $command
    if ($Action -eq 'Status') {
        if (Test-Path -LiteralPath $directory) {
            # TLS is deliberately unreadable to normal users. Validate the public
            # component marker/root, not private descendants, for status queries.
            Assert-ProtectedAcl ([IO.Path]::GetPathRoot($directory)) -ParentOnly
            Assert-LocalPath $directory -MustExist | Out-Null
            Assert-ProtectedAcl $directory
            if ((Read-BoundedText (Join-Path $directory 'deskflow-full-control.component') 256).Trim() -cne 'Deskflow FileCopy Full Control v1') {
                throw 'Unrecognized component directory'
            }
        } elseif ($service) { throw 'The component service exists but its protected directory is missing' }
        [ordered]@{ installed = [bool]$service; running = [bool]($service -and $service.State -eq 'Running'); directory = $directory; settings = $settings } | ConvertTo-Json -Compress
        exit 0
    }
    $principal = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator elevation is required for this operation' }
    $mutex = New-Object Threading.Mutex($false, 'Global\DeskflowFileCopyFullControlManager')
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { throw 'Another full-control operation is in progress' }
    $service = Get-OwnedService $command
    if (Test-Path -LiteralPath $directory) { Assert-Component $directory } elseif ($Action -ne 'Install') {
        if ($Action -in @('Stop', 'Uninstall') -and -not $service) { exit 0 }
        throw 'The full-control component is not installed'
    }
    if ($Action -in @('Stop', 'Uninstall')) {
        Stop-ControlService
        if ($Action -eq 'Uninstall' -and $service) { Invoke-ServiceCommand @('delete', 'Deskflow') }
        exit 0
    }
    if ($Action -eq 'Start') {
        if (-not $service) { throw 'The full-control service is not installed' }
        Start-ControlService
        exit 0
    }
    if (-not $SourceSettings) { throw 'SourceSettings is required when installing or applying configuration' }
    $SourceSettings = Assert-LocalPath $SourceSettings -MustExist
    if ($Action -eq 'Apply' -and -not $service) { throw 'The full-control service is not installed' }
    $componentDirectory = $directory
    if (-not (Test-Path -LiteralPath $directory)) {
        Assert-ProtectedAcl ([IO.Path]::GetPathRoot($directory)) -ParentOnly
        # Do not publish the fixed root until a complete, validated runtime and
        # TLS/settings snapshot exist. A failed first install stays retryable.
        $freshToken = [Guid]::NewGuid().ToString('N')
        $componentDirectory = Join-Path ([IO.Path]::GetPathRoot($directory)) ('.DeskflowFileCopyControl-install-' + $freshToken)
        if (Test-Path -LiteralPath $componentDirectory) { throw 'First-install staging path already exists' }
        New-ProtectedDirectory $componentDirectory
        $freshDirectory = $componentDirectory
        [IO.File]::WriteAllText((Join-Path $componentDirectory 'deskflow-full-control.component'), 'Deskflow FileCopy Full Control v1')
        [IO.File]::WriteAllText((Join-Path $componentDirectory '.install-owner'), $freshToken)
    }
    New-ProtectedDirectory (Join-Path $componentDirectory 'logs')
    $transactionName = '.transaction-' + [Guid]::NewGuid().ToString('N')
    $transaction = Join-Path $componentDirectory $transactionName
    New-ProtectedDirectory $transaction
    $runtime = Join-Path $transaction 'runtime'
    New-ProtectedDirectory $runtime
    $backup = Join-Path $transaction 'backup'
    New-ProtectedDirectory $backup
    $hadService = [bool]$service
    $wasRunning = [bool]($service -and $service.State -eq 'Running')
    $replaced = New-Object 'Collections.Generic.List[string]'
    $settingsMoved = $false
    $newSettings = $false
    $serviceCreated = $false
    $serviceTouched = $false
    $failure = $null
    try {
        $runtimeFiles = @()
        if ($Action -eq 'Install') { $runtimeFiles = @(Copy-Runtime $source $runtime) }
        New-SettingsSnapshot $SourceSettings $directory (Join-Path $transaction 'settings') $componentDirectory
        Assert-Component $componentDirectory
        $serviceTouched = $true
        Stop-ControlService
        foreach ($relative in $runtimeFiles) {
            $target = Join-Path $componentDirectory $relative
            $old = Join-Path $backup $relative
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($old)) | Out-Null
            if (Test-Path -LiteralPath $target) { Move-Item -LiteralPath $target -Destination $old }
            $replaced.Add($relative)
            Move-Item -LiteralPath (Join-Path $runtime $relative) -Destination $target
        }
        $settingsDirectory = Join-Path $componentDirectory 'settings'
        if (Test-Path -LiteralPath $settingsDirectory) {
            Move-Item -LiteralPath $settingsDirectory -Destination (Join-Path $backup 'settings')
            $settingsMoved = $true
        }
        Move-Item -LiteralPath (Join-Path $transaction 'settings') -Destination $settingsDirectory
        $newSettings = $true
        Assert-Component $componentDirectory
        if ($freshDirectory) {
            if (Test-Path -LiteralPath $directory) { throw 'The target directory appeared during first-install preparation' }
            # Same-volume rename publishes all executable files, the management
            # script and settings together; no incomplete fixed root is visible.
            # Directory.Move fails atomically if another directory appears at
            # the destination; Move-Item would instead nest inside that directory.
            [IO.Directory]::Move($componentDirectory, $directory)
            $componentDirectory = $directory
            $freshDirectory = $directory
            $transaction = Join-Path $directory $transactionName
            $backup = Join-Path $transaction 'backup'
        }
        if (-not $hadService) {
            $createdService = New-Service -Name Deskflow -DisplayName 'Deskflow FileCopy Full Control' -BinaryPathName $command -StartupType Automatic
            $createdService.Dispose()
            $serviceCreated = $true
            # Ordinary users can query status; only Administrators/SYSTEM can control it.
            Invoke-ServiceCommand @('sdset', 'Deskflow', 'D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;BU)')
        }
        if ($Action -eq 'Install' -or $wasRunning) { Start-ControlService }
        $installCommitted = $true
    } catch {
        $failure = $_
        if (-not $serviceTouched) { throw $failure }
        try {
            Stop-ControlService
            if ($serviceCreated) { Invoke-ServiceCommand @('delete', 'Deskflow') }
            if (-not $freshDirectory) {
                if ($newSettings) { Move-Item -LiteralPath (Join-Path $componentDirectory 'settings') -Destination (Join-Path $transaction 'failed-settings') }
                if ($settingsMoved) { Move-Item -LiteralPath (Join-Path $backup 'settings') -Destination (Join-Path $componentDirectory 'settings') }
                foreach ($relative in $replaced) {
                    $target = Join-Path $componentDirectory $relative
                    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
                    $old = Join-Path $backup $relative
                    if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination $target }
                }
            }
            if ($wasRunning) { Start-ControlService }
        } catch {
            # Never knowingly leave a partially replaced component running.
            Stop-ControlService
            throw "Operation failed: $failure. Recovery also failed: $_. Protected backup: $transaction"
        }
        throw $failure
    } finally {
        # Keep recovery material on failure. It is administrator-only where sensitive.
        if (-not $failure -and (Test-Path -LiteralPath $transaction)) { Remove-Transaction $transaction $componentDirectory }
    }
    if ($freshDirectory) { Remove-Item -LiteralPath (Join-Path $freshDirectory '.install-owner') -Force }
    exit 0
} catch {
    $operationFailure = $_
    if ($freshDirectory -and -not $installCommitted) {
        try { Remove-FreshComponent $freshDirectory $directory $freshToken $command } catch {
            [Console]::Error.WriteLine("First-install cleanup failed; protected recovery directory: $freshDirectory. $($_.Exception.Message)")
        }
    }
    if ($Action -eq 'Status') {
        [ordered]@{ installed = $false; running = $false; directory = $directory; settings = $settings; error = $operationFailure.Exception.Message } | ConvertTo-Json -Compress
    } else { [Console]::Error.WriteLine($operationFailure.Exception.Message) }
    exit 1
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
