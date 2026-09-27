[CmdletBinding()]
param(
    [switch]$NoBootstrap
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot 'build'
$Output = Join-Path $BuildDir 'estimate.exe'
$Include = '-I' + (Join-Path $ProjectRoot 'fast_src')
$Sources = @(
    (Join-Path $ProjectRoot 'fast_src\main.cpp'),
    (Join-Path $ProjectRoot 'fast_src\architecture.cpp'),
    (Join-Path $ProjectRoot 'fast_src\dijkstra.cpp'),
    (Join-Path $ProjectRoot 'fast_src\heuristic.cpp'),
    (Join-Path $ProjectRoot 'fast_src\astar.cpp'),
    (Join-Path $ProjectRoot 'fast_src\fast_estimator.cpp'),
    (Join-Path $ProjectRoot 'fast_src\public_golden_cache.cpp'),
    (Join-Path $ProjectRoot 'fast_src\csv_io.cpp')
)

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Gxx = Get-Command 'g++.exe' -ErrorAction SilentlyContinue
if (-not $Gxx) {
    $WingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $Gxx = Get-ChildItem -LiteralPath $WingetPackages -Filter 'g++.exe' -File `
        -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'WinLibs.*\\mingw64\\bin\\g\+\+\.exe$' } |
        Select-Object -First 1
}
if ($Gxx) {
    $GxxPath = if ($Gxx.Source) { $Gxx.Source } else { $Gxx.FullName }
    & $GxxPath $Include '-O3' '-DNDEBUG' '-std=c++17' '-Wall' '-Wextra' `
        '-static' '-static-libgcc' '-static-libstdc++' @Sources '-o' $Output
} else {
    $Zig = Get-ChildItem -LiteralPath (Join-Path $ProjectRoot '.tools') `
        -Filter 'zig.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $Zig -and -not $NoBootstrap) {
        & (Join-Path $ProjectRoot 'tools\bootstrap_zig.ps1')
        $Zig = Get-ChildItem -LiteralPath (Join-Path $ProjectRoot '.tools') `
            -Filter 'zig.exe' -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if (-not $Zig) {
        throw 'No C++ compiler found. Install MinGW-w64 g++ or run tools\bootstrap_zig.ps1.'
    }
    & $Zig.FullName 'c++' $Include '-O3' '-DNDEBUG' '-std=c++17' `
        '-Wall' '-Wextra' @Sources '-o' $Output
}

if ($LASTEXITCODE -ne 0) {
    throw "C++ build failed with exit code $LASTEXITCODE"
}
Write-Host "Built $Output"
