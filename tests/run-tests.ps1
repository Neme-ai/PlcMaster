$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\tools\build.ps1" -CheckOnly
$testDir=Join-Path $projectRoot 'tests'
Push-Location $testDir
try {
    & $qmake control_tests.pro -spec win32-msvc 'CONFIG+=release'
    if($LASTEXITCODE -ne 0){throw 'test qmake failed'}
    & nmake.exe /NOLOGO /f Makefile.Release
    if($LASTEXITCODE -ne 0){throw 'test compile failed'}
    & .\release\control_tests.exe
    if($LASTEXITCODE -ne 0){throw 'tests failed'}
    Get-Content test-results.txt
} finally {Pop-Location}
