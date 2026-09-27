[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildScript = Join-Path $ProjectRoot 'build_windows.ps1'
$TestScript = Join-Path $ProjectRoot 'tests\test_windows.ps1'
$BuiltExe = Join-Path $ProjectRoot 'build\estimate.exe'
$ExampleSource = Join-Path $ProjectRoot 'submission_example'
$Examples = Join-Path $ProjectRoot 'examples'
$DistRoot = Join-Path $ProjectRoot 'dist'
$PackageDir = Join-Path $DistRoot 'ziguang_fast_estimator_windows_x64'
$ZipPath = Join-Path $DistRoot 'ziguang_fast_estimator_windows_x64.zip'

& $BuildScript
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }
& $TestScript
if ($LASTEXITCODE -ne 0) { throw "tests failed with exit code $LASTEXITCODE" }

$ExpectedPackageDir = [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'dist\ziguang_fast_estimator_windows_x64'))
$ResolvedPackageDir = [IO.Path]::GetFullPath($PackageDir)
if ($ResolvedPackageDir -ne $ExpectedPackageDir) {
    throw "refusing to replace unexpected package directory: $ResolvedPackageDir"
}
if (Test-Path -LiteralPath $ResolvedPackageDir) {
    Remove-Item -LiteralPath $ResolvedPackageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedPackageDir -Force | Out-Null

Copy-Item -LiteralPath $BuiltExe -Destination (Join-Path $ResolvedPackageDir 'estimate.exe')
Copy-Item -LiteralPath (Join-Path $ExampleSource 'README.txt') -Destination $ResolvedPackageDir
Copy-Item -LiteralPath (Join-Path $ExampleSource 'run_sample.bat') -Destination $ResolvedPackageDir
Copy-Item -LiteralPath (Join-Path $Examples 'delay_estimate_request.csv') -Destination $ResolvedPackageDir
Copy-Item -LiteralPath (Join-Path $Examples 'delay_estimate_result.expected.csv') -Destination $ResolvedPackageDir

Push-Location $ResolvedPackageDir
try {
    & .\estimate.exe -in .\delay_estimate_request.csv -out .\delay_estimate_result.csv
    if ($LASTEXITCODE -ne 0) { throw "packaged executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Path $DistRoot -Force | Out-Null
Compress-Archive -LiteralPath $ResolvedPackageDir -DestinationPath $ZipPath -Force
Write-Host "Created submission example: $ZipPath"
