$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Compiler = (Get-Command 'g++.exe' -ErrorAction Stop).Source
$BuildDir = Join-Path $Here 'build_windows'
$Output = Join-Path $BuildDir 'estimate.exe'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

& $Compiler `
    '-O3' '-DNDEBUG' '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' `
    '-DGRID_EXPECTED_SOURCES=208' `
    (Join-Path $Here 'src\solver_main.cpp') `
    (Join-Path $Here 'src\grid_model.cpp') `
    (Join-Path $Here 'src\architecture.cpp') `
    '-lpsapi' '-o' $Output
if ($LASTEXITCODE -ne 0) {
    throw "C++ build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $Output"
