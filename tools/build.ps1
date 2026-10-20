[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$QtDir = 'C:\Qt\Qt5.12.10\5.12.10\msvc2017_64',
    [string]$VisualStudioDir,
    [switch]$DeployRelease,
    [switch]$Rebuild,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# Qt 5.12 qmake requires an ASCII checkout path on Windows.
if ($projectRoot -match '[^\x00-\x7F]') { throw 'Build from an ASCII checkout path or an ASCII directory junction.' }
$projectFile = Join-Path $projectRoot 'PlcQtDebugTool.pro'
$qmake = Join-Path $QtDir 'bin\qmake.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$cdb = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x64\cdb.exe'

if (!(Test-Path -LiteralPath $qmake)) {
    throw "Qt 5.12.10 MSVC x64 is missing: $qmake. Install it or pass -QtDir."
}
$qtVersion = & $qmake -query QT_VERSION
if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '5.12.10') {
    throw "Expected Qt 5.12.10; found: $qtVersion"
}
$qconfig = Join-Path $QtDir 'mkspecs\qconfig.pri'
if (!(Select-String -LiteralPath $qconfig -Pattern '^QT_ARCH\s*=\s*x86_64\s*$' -Quiet)) {
    throw 'The selected Qt package must target x86_64.'
}
if (!$VisualStudioDir) {
    if (!(Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Build Tools 2019 is missing. Install v142 and Windows SDK 19041 first.'
    }
    $VisualStudioDir = & $vswhere -latest -products '*' -version '[16.0,17.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (!$VisualStudioDir) { throw 'No Visual Studio 2019 C++ installation was found.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path -LiteralPath $vcvars)) { throw "Missing compiler setup: $vcvars" }

# Import the vendor's x64 environment into this PowerShell process only.
# Reject command-shell expansion characters before passing the discovered path to cmd.
if ($vcvars -match '["%\r\n!]') { throw 'Unsupported characters in Visual Studio installation path.' }
$environmentStart = New-Object System.Diagnostics.ProcessStartInfo
$environmentStart.FileName = $env:ComSpec
$environmentStart.Arguments = '/d /u /s /c ""' + $vcvars + '" >nul && set"'
$environmentStart.UseShellExecute = $false
$environmentStart.CreateNoWindow = $true
$environmentStart.RedirectStandardOutput = $true
$environmentStart.StandardOutputEncoding = [Text.Encoding]::Unicode
# Some launchers supply both PATH and Path. Normalize them before vcvars64 runs.
foreach ($key in @($environmentStart.EnvironmentVariables.Keys)) {
    if ($key -ieq 'PATH') { $environmentStart.EnvironmentVariables.Remove($key) }
}
$environmentStart.EnvironmentVariables['Path'] = $env:PATH
$environmentProcess = [Diagnostics.Process]::Start($environmentStart)
$compilerEnvironment = $environmentProcess.StandardOutput.ReadToEnd() -split "`r?`n"
$environmentProcess.WaitForExit()
if ($environmentProcess.ExitCode -ne 0) { throw 'Could not initialize the MSVC x64 environment.' }
$environmentProcess.Dispose()
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'MSVC target architecture is not x64.' }
Get-Command cl.exe, nmake.exe -ErrorAction Stop | Select-Object Name, Source | Format-Table
Write-Host "Qt: $qtVersion ($QtDir)"
Write-Host "MSVC: $env:VCToolsVersion"
Write-Host "Windows SDK: $env:WindowsSDKVersion"
if (!$env:WindowsSdkDir -or $env:WindowsSDKVersion -notmatch '^10\.0\.\d+\.\d+\\?$') {
    throw 'Windows SDK is missing or its installation has not finished.'
}
$windowsHeader = Join-Path $env:WindowsSdkDir ('Include\' + $env:WindowsSDKVersion.TrimEnd('\') + '\um\Windows.h')
if (!(Test-Path -LiteralPath $windowsHeader)) { throw "Windows SDK header is missing: $windowsHeader" }
if (Test-Path -LiteralPath $cdb) { Write-Host "Debugger: $cdb" }
else { Write-Warning 'CDB is missing. Builds can run, but install Debugging Tools for Windows before breakpoint debugging.' }
if ($CheckOnly) { return }
if ($DeployRelease -and $Configuration -eq 'Debug') {
    throw '-DeployRelease requires -Configuration Release or All.'
}

$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configurations) {
    $buildDir = Join-Path $projectRoot ('.build\' + $config.ToLowerInvariant())
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    Push-Location -LiteralPath $buildDir
    try {
        & $qmake $projectFile -spec win32-msvc ("CONFIG+=" + $config.ToLowerInvariant())
        if ($LASTEXITCODE -ne 0) { throw "qmake failed for $config." }
        if ($Rebuild) {
            & nmake.exe /NOLOGO /f ("Makefile." + $config) clean
            if ($LASTEXITCODE -ne 0) { throw "Clean failed for $config." }
        }
        & nmake.exe /NOLOGO /f ("Makefile." + $config)
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $config." }
        $outputDir = Join-Path $buildDir $config.ToLowerInvariant()
        foreach ($name in @('PlcQtDebugTool.exe', 'snap7.dll', 'light.qss')) {
            if (!(Test-Path -LiteralPath (Join-Path $outputDir $name))) {
                throw "Missing build output: $outputDir\$name"
            }
        }
        Write-Host "Built $config`: $outputDir"
    }
    finally { Pop-Location }
}

if ($DeployRelease) {
    $releaseDir = Join-Path $projectRoot '.build\release\release'
    $packageDir = Join-Path $projectRoot ('.build\package-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $packageDir -ErrorAction Stop | Out-Null
    foreach ($name in @('PlcQtDebugTool.exe', 'snap7.dll', 'light.qss')) {
        Copy-Item -LiteralPath (Join-Path $releaseDir $name) -Destination $packageDir
    }
    & (Join-Path $QtDir 'bin\windeployqt.exe') --release --compiler-runtime (Join-Path $packageDir 'PlcQtDebugTool.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    foreach ($name in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'platforms\qwindows.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $packageDir $name))) {
            throw "Missing package dependency: $name"
        }
    }
    Write-Host "Release package: $packageDir"
    Write-Host 'On another PC, install the matching x64 Visual C++ Redistributable before running.'
}


