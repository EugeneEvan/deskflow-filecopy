# SPDX-FileCopyrightText: Copyright (C) 2026 Deskflow FileCopy contributors
# SPDX-License-Identifier: MIT
<#
.SYNOPSIS
Build a clean Windows x64 portable ZIP and per-user installer from Release output.
.DESCRIPTION
Requires PowerShell 5.1+, Inno Setup 6.7+, an x64 MSVC dumpbin, and the matching
vcpkg runtime/Qt plug-ins and MSVC CRT. Does not install dependencies, build C++,
reuse a live profile, sign binaries, or alter a running Deskflow installation.
OutputDirectory must be outside the source tree. Each run creates a fresh stage.
.EXAMPLE
./deploy/windows/package-filecopy.ps1 -BuildDirectory F:/deskflow-build-filecopy `
  -DependenciesDirectory F:/vcpkg/installed/x64-windows `
  -CrtDirectory F:/deskflow-tools/msvc/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT `
  -Dumpbin F:/deskflow-tools/msvc/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe `
  -Iscc F:/deskflow-tools/innosetup-6.7.3/ISCC.exe -OutputDirectory F:/deskflow-releases `
  -Version 0.3.4 -ProjectUrl https://github.com/EugeneEvan/deskflow-filecopy
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$DependenciesDirectory,
    [Parameter(Mandatory = $true)][string]$CrtDirectory,
    [Parameter(Mandatory = $true)][string]$Dumpbin,
    [Parameter(Mandatory = $true)][string]$Iscc,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+(?:[.-][A-Za-z0-9.-]+)?$')][string]$Version,
    [Parameter(Mandatory = $true)][ValidatePattern('^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$ProjectUrl,
    [string]$IconPath,
    [switch]$TestBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($output.Equals($repository, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be outside the source repository.'
}
if (-not $IconPath) { $IconPath = Join-Path $repository 'src\apps\res\deskflow.ico' }
foreach ($required in @($BuildDirectory, $DependenciesDirectory, $CrtDirectory, $Dumpbin, $Iscc, $IconPath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required packaging input is missing: $required" }
}

$baseName = 'deskflow-filecopy-' + $Version + '-windows-x64'
if ($TestBuild) { $baseName += '-packaging-test' }
$zipPath = Join-Path $output ($baseName + '-portable.zip')
$setupPath = Join-Path $output ($baseName + '-setup.exe')
$manifestPath = Join-Path $output ($baseName + '-manifest.json')
foreach ($target in @($zipPath, $setupPath, $manifestPath)) {
    if (Test-Path -LiteralPath $target) { throw "Output already exists; choose a new version or output directory: $target" }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$stagingRoot = Join-Path $output ('.package-' + [Guid]::NewGuid().ToString('N'))
$payload = Join-Path $stagingRoot $baseName
New-Item -ItemType Directory -Path $payload -Force | Out-Null

function Copy-PackageFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Package file is missing: $Source" }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
}
function Write-Utf8([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

foreach ($name in @('deskflow.exe', 'deskflow-core.exe', 'deskflow-daemon.exe')) {
    Copy-PackageFile (Join-Path $BuildDirectory ('bin\' + $name)) (Join-Path $payload $name)
}
Copy-PackageFile $IconPath (Join-Path $payload 'deskflow-filecopy.ico')
$pluginRoot = Join-Path $DependenciesDirectory 'Qt6\plugins'
Copy-PackageFile (Join-Path $pluginRoot 'platforms\qwindows.dll') (Join-Path $payload 'plugins\platforms\qwindows.dll')
foreach ($folder in @('styles', 'iconengines', 'imageformats', 'networkinformation', 'tls')) {
    $sourceDirectory = Join-Path $pluginRoot $folder
    if (Test-Path -LiteralPath $sourceDirectory) {
        foreach ($file in Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.dll' -File) {
            Copy-PackageFile $file.FullName (Join-Path $payload ('plugins\' + $folder + '\' + $file.Name))
        }
    }
}

$dllSources = @{}
foreach ($directory in @((Join-Path $DependenciesDirectory 'bin'), $CrtDirectory)) {
    foreach ($dll in Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File) { $dllSources[$dll.Name] = $dll.FullName }
}
foreach ($dll in Get-ChildItem -LiteralPath $CrtDirectory -Filter '*.dll' -File) {
    Copy-PackageFile $dll.FullName (Join-Path $payload $dll.Name)
}
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object Extension -in @('.dll', '.exe') | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$systemDependencies = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (-not $seen.Add($binary)) { continue }
    $headers = & $Dumpbin /HEADERS $binary 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $headers -notmatch '8664 machine \(x64\)') { throw "Expected an x64 PE binary: $binary" }
    $imports = & $Dumpbin /DEPENDENTS $binary 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $binary" }
    foreach ($match in [regex]::Matches($imports, '(?im)^\s+([A-Za-z0-9_.-]+\.dll)\s*$')) {
        $name = $match.Groups[1].Value
        if ($dllSources.ContainsKey($name)) {
            $destination = Join-Path $payload $name
            if (-not (Test-Path -LiteralPath $destination)) { Copy-PackageFile $dllSources[$name] $destination }
            $queue.Enqueue($destination)
        } elseif ($name -match '^(api-ms-|ext-ms-)' -or (Test-Path -LiteralPath (Join-Path $env:SystemRoot ('System32\' + $name)))) {
            $null = $systemDependencies.Add($name)
        } else { throw "Missing runtime dependency $name imported by $binary" }
    }
}
foreach ($name in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll', 'vcruntime140.dll', 'msvcp140.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $payload $name))) { throw "Mandatory runtime is absent: $name" }
}

foreach ($file in Get-ChildItem -LiteralPath (Join-Path $BuildDirectory 'translations') -Filter '*.qm' -File) {
    Copy-PackageFile $file.FullName (Join-Path $payload ('translations\' + $file.Name))
}
foreach ($name in @('qt_zh_CN.qm', 'qtbase_zh_CN.qm', 'qt_en.qm', 'qtbase_en.qm')) {
    $source = Join-Path $DependenciesDirectory ('translations\' + $name)
    if (Test-Path -LiteralPath $source) { Copy-PackageFile $source (Join-Path $payload ('translations\' + $name)) }
}
Copy-PackageFile (Join-Path $repository 'LICENSE') (Join-Path $payload 'LICENSE')
Copy-Item -LiteralPath (Join-Path $repository 'LICENSES') -Destination $payload -Recurse
foreach ($license in Get-ChildItem -Path (Join-Path $DependenciesDirectory 'share\*\copyright') -File) {
    Copy-PackageFile $license.FullName (Join-Path $payload ('third-party-licenses\' + $license.Directory.Name + '.txt'))
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $BuildDirectory 'docs') -Recurse -File) {
    if ($file.Extension -notin @('.md', '.html', '.css', '.png', '.svg', '.jpg', '.js')) { continue }
    $relative = $file.FullName.Substring((Join-Path $BuildDirectory 'docs').TrimEnd('\').Length + 1)
    Copy-PackageFile $file.FullName (Join-Path $payload ('docs\' + $relative))
}
Write-Utf8 (Join-Path $payload 'qt.conf') "[Paths]`nPrefix=.`nPlugins=plugins`nTranslations=translations`n"
New-Item -ItemType Directory -Path (Join-Path $payload 'settings') -Force | Out-Null
Write-Utf8 (Join-Path $payload 'settings\Deskflow.conf') "[core]`nprocessMode=1`nfileTransferEnabled=false`n`n[gui]`nstartCoreWithGui=false`nenableUpdateCheck=false`n`n[security]`ntlsEnabled=true`ncheckPeerFingerprints=true`n"
Write-Utf8 (Join-Path $payload 'deskflow-filecopy.package') "Deskflow FileCopy`n$Version`n$ProjectUrl`n"
$notes = @"
Deskflow FileCopy $Version - Windows x64

This independent project is based on Deskflow (https://github.com/deskflow/deskflow).
Source code, build instructions and releases: $ProjectUrl
License: GPL-2.0-only with the existing OpenSSL exception. See LICENSE and LICENSES.

File and folder clipboard transfer currently supports Windows to Windows ONLY.
Install this release on both PCs and enable file transfer in Settings on both sides.
Copy files in Explorer, wait until the receiving computer reports Ready, then paste.
Files use a receiver cache; large files must finish transfer before paste.
The main window shows bytes, file counts, recent transfer speed and estimated time.
Speed and time are estimates and are hidden during preparation, verification or stalls.
Cut/move, resume, drag and drop, and file transfer on macOS/Linux are not supported.

Install into a writable folder of your choice (for example D:\Apps\Deskflow FileCopy).
The installer uses a separate product identity and preserves existing settings.
An optional task starts the app when the current user signs in; it is off by default.
The installer remembers this choice on upgrade and removes its shortcut on uninstall.
Complete configuration and connect once before using login startup. The app remembers
whether its core was running when closed and restores that saved state when launched.
It does not install a service, launch the app after installation, or close a running process.
Close other Deskflow versions yourself before starting this version. Desktop mode
cannot control the sign-in screen or secure UAC desktop. Allow Deskflow in Windows
Firewall on your trusted private network when Windows requests it.

The portable ZIP uses the same files; extract it before running deskflow.exe.
Settings and TLS identity are stored under the application's settings directory.
File-transfer caches default to the Windows local application data directory.
Preferences > General > Manage file cache can select a dedicated local folder,
set its quota (1-1024 GiB, default 20 GiB), inspect usage, and clean managed batches.
Save preferences and reconnect to apply the new location and quota. Existing caches
are not moved or deleted when the location changes. Network paths, drive roots and
reparse points are not supported. Cleanup preserves current clipboard references,
refuses to run during reception, and leaves legacy caches and unrelated files alone.
Uninstall preserves settings and received files. Remove them yourself when no longer needed.

Qt, OpenSSL, their runtime dependencies and the matching Microsoft Visual C++ runtime
are included locally. Third-party notices are under third-party-licenses.
This community build is unsigned; Windows may show an unknown publisher warning.
"@
Write-Utf8 (Join-Path $payload 'INSTALL-NOTES.txt') $notes
Write-Utf8 (Join-Path $payload 'THIRD-PARTY-NOTICES.txt') @"
Deskflow FileCopy is an independent derivative of Deskflow.
Upstream source: https://github.com/deskflow/deskflow
Corresponding project source and release tags: $ProjectUrl

The program dynamically links Qt and OpenSSL. Redistribution terms and full
third-party notices supplied by the dependency packages are in third-party-licenses.
Deskflow's licenses and OpenSSL linking exception are in LICENSE and LICENSES.
Microsoft Visual C++ runtime DLLs are the matching x64 redistributable runtime;
they remain subject to Microsoft's redistribution terms.
Dependency source repositories and version information are recorded by vcpkg;
see the repository's build instructions and vcpkg baseline to reproduce this build.
No third-party license is replaced by this notice.
"@
$files = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($payload.Length + 1).Replace('\', '/')
    if ($_.Extension -in @('.pem', '.key', '.pfx', '.log') -or $relative -match '(?i)(fingerprint|trustedservers|trustedclients)') {
        throw "Private or machine-specific file in staging: $relative"
    }
    [ordered]@{ path = $relative; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$compilerArgs = @('/Qp', ('/DPayloadDir=' + $payload), ('/DPackageVersion=' + $Version), ('/DProjectUrl=' + $ProjectUrl), ('/DOutputDir=' + $output), ('/DPackageBaseName=' + $baseName))
if ($TestBuild) { $compilerArgs += '/DTesting=1' }
& $Iscc @compilerArgs (Join-Path $PSScriptRoot 'filecopy.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE; stage retained at $payload" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($payload, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $true)
$artifacts = @($setupPath, $zipPath) | ForEach-Object {
    $file = Get-Item -LiteralPath $_
    [ordered]@{ name = $file.Name; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest = [ordered]@{ version = $Version; project = $ProjectUrl; createdUtc = [DateTime]::UtcNow.ToString('o'); testBuild = [bool]$TestBuild; payload = $payload; checkedPeFiles = $seen.Count; systemDependencies = @($systemDependencies | Sort-Object); artifacts = @($artifacts); files = $files }
Write-Utf8 $manifestPath ($manifest | ConvertTo-Json -Depth 6)
$artifacts | ForEach-Object { Write-Output (($_['sha256']) + '  ' + ($_['name'])) }
Write-Output "Manifest: $manifestPath"
Write-Output "Clean staging retained for verification: $payload"
