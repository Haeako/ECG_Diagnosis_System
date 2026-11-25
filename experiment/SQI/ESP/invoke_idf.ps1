param([Parameter(ValueFromRemainingArguments=$true)][string[]]$IdfArguments)
$ErrorActionPreference = 'Continue'
$idfDirectory = 'C:\esp\v6.0.1\esp-idf'
$idfTools = 'C:\Espressif\tools'
$idfEnvironment = Join-Path $idfTools 'python\v6.0.1\venv'
$env:IDF_PATH = $idfDirectory
$env:IDF_TOOLS_PATH = $idfTools
$env:IDF_PYTHON_ENV_PATH = $idfEnvironment
$env:ESP_ROM_ELF_DIR = Join-Path $idfTools 'esp-rom-elfs\20241011'
$env:IDF_TARGET = 'esp32'
$env:ESP_IDF_VERSION = '6.0.1'
$env:CMAKE_GENERATOR = 'Ninja'
$env:PATH = (Join-Path $idfTools 'cmake\4.0.3\bin') + ';' +
    (Join-Path $idfTools 'ninja\1.12.1') + ';' +
    (Join-Path $idfTools 'xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin') + ';' +
    (Join-Path $idfEnvironment 'Scripts') + ';' + $env:PATH
& (Join-Path $idfEnvironment 'Scripts\python.exe') (Join-Path $idfDirectory 'tools\idf.py') @IdfArguments
exit $LASTEXITCODE
