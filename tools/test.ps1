$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools required.' }
$out = Join-Path $root 'test-output'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$batch = Join-Path $out 'test.cmd'
@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /EHsc /std:c++17 /utf-8 /I"$root\common" "$root\tests\control_tests.cpp" "$root\common\core.cpp" /Fo"$out\\" /Fe"$out\core.exe"
if errorlevel 1 exit /b 1
"$out\core.exe" "$out\telemetry.bin"
"@ -replace "`r?`n", "`r`n" | Set-Content -LiteralPath $batch -Encoding utf8NoBOM
& cmd /c $batch
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
& node "$root\tests\converter.mjs" "$out\telemetry.bin"
if ($LASTEXITCODE -ne 0) { throw 'Converter tests failed.' }
