# SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
# SPDX-License-Identifier: MIT
# Run with Windows PowerShell; these checks never install or start a service.
$ErrorActionPreference = 'Stop'
$manager = Join-Path $PSScriptRoot '../../../deploy/windows/full-control-manager.ps1'
if (-not (Test-Path -LiteralPath $manager)) { throw 'Full-control configuration importer is missing' }
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($manager, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($function in $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    Invoke-Expression $function.Extent.Text
}
function Assert-Equal($actual, $expected, $description) {
    if ($actual -cne $expected) { throw "$description (expected '$expected', actual '$actual')" }
}
function Assert-Rejected([scriptblock]$action, $description) {
    $rejected = $false
    try { & $action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw $description }
}
$inputSettings = @{
    'core/coreMode' = '1'; 'core/computerName' = 'test-client'; 'client/remoteHost' = '192.0.2.10'
    'security/tlsEnabled' = 'false'; 'security/checkPeerFingerprints' = 'false'
    'daemon/configFile' = 'C:/Users/Public/evil.conf'; 'log/file' = 'C:/Windows/evil.log'
    'core/enableEnterCommand' = 'true'; 'core/enterCommand' = 'calc.exe'
    'core/fileTransferEnabled' = 'true'; 'core/fileTransferCachePath' = 'F:/UserCache'
    'unknown/surprise' = 'payload'; 'server/externalConfig' = 'false'
}
$snapshot = ConvertTo-ControlSettings $inputSettings 'F:\Deskflow FileCopy Control'
Assert-Equal $snapshot['security/tlsEnabled'] 'true' 'TLS must remain enabled'
Assert-Equal $snapshot['security/checkPeerFingerprints'] 'true' 'Peer authentication must remain enabled'
Assert-Equal $snapshot['core/enableEnterCommand'] 'false' 'Screen commands must be disabled'
Assert-Equal $snapshot.ContainsKey('core/enterCommand') $false 'Screen command text must not be imported'
Assert-Equal $snapshot.ContainsKey('unknown/surprise') $false 'Unknown keys must not be imported'
Assert-Equal $snapshot['log/file'] 'F:/Deskflow FileCopy Control/logs/core.log' 'Log path must be fixed'
Assert-Equal $snapshot['daemon/configFile'] 'F:/Deskflow FileCopy Control/settings/Deskflow.conf' 'Daemon config must be fixed'
Assert-Equal $snapshot['core/fileTransferCachePath'] 'F:/UserCache' 'User-agent cache preference must survive'
$inputSettings['server/externalConfig'] = 'true'
Assert-Rejected { ConvertTo-ControlSettings $inputSettings 'F:\Deskflow FileCopy Control' } 'External config must be rejected'
foreach ($path in @('\\server\share\app', '\\?\C:\app', 'C:relative', 'C:\app:stream', 'C:\app\..\other')) {
    Assert-Rejected { Assert-LocalPath $path } "Unsafe path accepted: $path"
}
Assert-Equal (Assert-LocalPath (Get-Item -LiteralPath $PSScriptRoot).FullName -MustExist) (Get-Item -LiteralPath $PSScriptRoot).FullName 'Ordinary existing local paths must remain usable'
Assert-ProtectedAcl ([IO.Path]::GetPathRoot($PSScriptRoot)) -ParentOnly

# Exercise the actual -File entry point through the same strict UTF-8 pipe used
# by the GUI, without creating files or asking for administrative privileges.
$missingDirectory = [IO.Path]::GetPathRoot($PSScriptRoot) + '__DeskflowMissing_' + [Guid]::NewGuid().ToString('N') + [char]0x4e2d + [char]0x6587
$startInfo = New-Object Diagnostics.ProcessStartInfo
$startInfo.FileName = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$startInfo.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $manager + '" -Action Status -SourceDirectory "' + $missingDirectory + '"'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.StandardOutputEncoding = New-Object Text.UTF8Encoding($false, $true)
$startInfo.StandardErrorEncoding = New-Object Text.UTF8Encoding($false, $true)
$statusProcess = [Diagnostics.Process]::Start($startInfo)
try {
    $statusText = $statusProcess.StandardOutput.ReadToEnd()
    if (-not $statusProcess.WaitForExit(5000)) { throw 'Status did not finish in time' }
    Assert-Equal $statusProcess.ExitCode 1 'Invalid source must return a failed status'
    $statusJson = $statusText | ConvertFrom-Json
    Assert-Equal $statusJson.error.Contains($missingDirectory) $true 'UTF-8 status must preserve non-ASCII paths'
} finally { $statusProcess.Dispose() }

# Replace only the service/registry reads; exercise the real ownership decision.
$registration = [pscustomobject]@{ ImagePath = '"F:\Deskflow FileCopy Control\deskflow-daemon.exe" --full-control --settings "F:\Deskflow FileCopy Control\settings\Deskflow.conf"'; ObjectName = 'LocalSystem' }
function Get-Service {
    $controller = [pscustomobject]@{ Status = 'Running' }
    $controller | Add-Member -MemberType ScriptMethod -Name Dispose -Value { }
    return $controller
}
function Get-ItemProperty { return $registration }
$expectedCommand = $registration.ImagePath
Assert-Equal (Get-OwnedService $expectedCommand).State 'Running' 'Owned service must be recognized'
$registration.ImagePath = '"F:\Other Deskflow\deskflow-daemon.exe"'
Assert-Rejected { Get-OwnedService $expectedCommand } 'Original Deskflow service must not be replaced'
$registration.ImagePath = $expectedCommand
$registration.ObjectName = 'LocalService'
Assert-Rejected { Get-OwnedService $expectedCommand } 'Service identity must match the component'

# This also verifies that opaque QSettings values are kept verbatim, while duplicate
# keys cannot override a previously reviewed setting in the administrative importer.
$sample = "[core]`ncoreMode=1`nfileTransferCachePath=F:/Cache`n[security]`ntlsEnabled=false`n"
function Read-BoundedText { return $sample }
$parsed = Read-IniSettings 'fixture'
Assert-Equal $parsed['core/coreMode'] '1' 'INI grouping must survive import'
Assert-Equal $parsed['core/fileTransferCachePath'] 'F:/Cache' 'INI values must survive import'
$sample = "[core]`ncoreMode=1`ncoreMode=2`n"
Assert-Rejected { Read-IniSettings 'fixture' } 'Duplicate security-relevant keys must be rejected'
$sample = "[core]`ncoreMode=1`ncontinued line`n"
Assert-Rejected { Read-IniSettings 'fixture' } 'Unsupported INI syntax must fail closed'

# A failed first install may remove only the exact directory it created. Registry
# and filesystem calls below are inert; the production cleanup guards decide.
$freshToken = '11111111111111111111111111111111'
$freshTarget = 'F:\Deskflow FileCopy Control'
$freshStage = 'F:\.DeskflowFileCopyControl-install-' + $freshToken
$deleted = $null
$existingService = $null
$sample = $freshToken
function Assert-LocalPath { param($Path, [switch]$MustExist) return $Path }
function Assert-Component { }
function Get-OwnedService { return $existingService }
function Remove-Item { param($LiteralPath, [switch]$Recurse, [switch]$Force) $script:deleted = $LiteralPath }
Remove-FreshComponent $freshStage $freshTarget $freshToken 'expected-service-command'
Assert-Equal $deleted $freshStage 'Failed preparation must clean its own staging directory'
$deleted = $null
Remove-FreshComponent $freshTarget $freshTarget $freshToken 'expected-service-command'
Assert-Equal $deleted $freshTarget 'Failed newly published component must be recoverable'
$sample = 'different-install-token'
Assert-Rejected { Remove-FreshComponent $freshTarget $freshTarget $freshToken 'expected-service-command' } 'Existing component ownership must not be inferred from its path'
$sample = $freshToken
Assert-Rejected { Remove-FreshComponent 'F:\Other App' $freshTarget $freshToken 'expected-service-command' } 'Unrelated directory must not be removed'
$existingService = [pscustomobject]@{ State = 'Running' }
Assert-Rejected { Remove-FreshComponent $freshTarget $freshTarget $freshToken 'expected-service-command' } 'A registered component must never be removed by failure cleanup'
Write-Output 'FullControlManagerTests: passed'
