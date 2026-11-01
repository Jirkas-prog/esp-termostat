param(
  [ValidateSet('tdisplay','c3','c6')][string]$Board = 'tdisplay',
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.3.1',
  [string]$ToolsPath = 'C:\Espressif',
  [string]$PythonEnv = 'C:\Espressif\python_env\idf5.3_py3.11_env'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$python = Join-Path $PythonEnv 'Scripts\python.exe'
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnv
$env:PATH = "$PythonEnv\Scripts;$env:PATH"
$env:PYTHONUTF8 = '1'
. (Join-Path $IdfPath 'export.ps1')
$target = @{tdisplay='esp32'; c3='esp32c3'; c6='esp32c6'}[$Board]
# Dedicated ASCII build path; no source changes in the reference projects.
$stage = Join-Path $ToolsPath "thermostat-40\$Board"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
foreach ($name in @('CMakeLists.txt','sdkconfig.defaults','sdkconfig.defaults.esp32c6','sdkconfig.defaults.esp32c3','partitions.csv','main')) {
  Copy-Item -LiteralPath (Join-Path "$project\firmware" $name) -Destination $stage -Recurse -Force
}
Copy-Item -LiteralPath "$project\common" -Destination $stage -Recurse -Force
& $python (Join-Path $IdfPath 'tools\idf.py') -C $stage -B "$stage\build" "-DIDF_TARGET=$target" build
if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
$out = Join-Path $project "build\$Board"
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($name in @('esp_termostat.bin','esp_termostat.elf','esp_termostat.map','bootloader\bootloader.bin','partition_table\partition-table.bin','flasher_args.json')) {
  $destination = Join-Path $out $name
  New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
  Copy-Item -LiteralPath "$stage\build\$name" -Destination $destination -Force
}
Copy-Item -LiteralPath "$stage\dependencies.lock" -Destination "$out\dependencies.lock" -Force
& $python "$PSScriptRoot\package.py" $out $Board $target
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
