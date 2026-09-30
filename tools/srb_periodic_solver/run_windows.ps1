param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$ModelFile
)

$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Executable = Join-Path $Here 'build_windows\estimate.exe'
if (-not (Test-Path -LiteralPath $Executable)) {
    & (Join-Path $Here 'build_windows.ps1')
}
if (-not (Test-Path -LiteralPath $ModelFile)) {
    throw "Model not found: $ModelFile"
}

& $Executable '-in' $InputFile '-out' $OutputFile '--model' $ModelFile
if ($LASTEXITCODE -ne 0) {
    throw "Estimator failed with exit code $LASTEXITCODE"
}
