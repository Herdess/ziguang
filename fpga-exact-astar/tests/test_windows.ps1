$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $ProjectRoot 'build\estimate.exe'
$Input = Join-Path $ProjectRoot 'examples\delay_estimate_request.csv'
$ExpectedFile = Join-Path $ProjectRoot 'examples\delay_estimate_result.expected.csv'
$Output = Join-Path $ProjectRoot 'build\delay_estimate_result.csv'
$RepeatOutput = Join-Path $ProjectRoot 'build\delay_estimate_result.repeat.csv'
$VerifyOutput = Join-Path $ProjectRoot 'build\verify_result.csv'

if (-not (Test-Path -LiteralPath $Exe)) {
    & (Join-Path $ProjectRoot 'build_windows.ps1')
}

& $Exe -in $Input -out $Output
if ($LASTEXITCODE -ne 0) { throw "competition interface failed with exit code $LASTEXITCODE" }

$Header = Get-Content -LiteralPath $Output -TotalCount 1
if ($Header -ne 'From,To,delay') {
    throw "output header mismatch: expected From,To,delay, got $Header"
}

$InputRows = @(Import-Csv -LiteralPath $Input)
$Actual = @(Import-Csv -LiteralPath $Output)
if ($Actual.Count -ne $InputRows.Count) {
    throw "row count mismatch: expected $($InputRows.Count), got $($Actual.Count)"
}
for ($Index = 0; $Index -lt $InputRows.Count; ++$Index) {
    if ($InputRows[$Index].From -ne $Actual[$Index].From -or
        $InputRows[$Index].To -ne $Actual[$Index].To -or
        [int]$Actual[$Index].delay -lt 0) {
        throw "default solver schema/order mismatch at row $($Index + 1)"
    }
}

& $Exe -in $Input -out $RepeatOutput
if ($LASTEXITCODE -ne 0) { throw "repeat run failed with exit code $LASTEXITCODE" }
$FirstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Output).Hash
$SecondHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $RepeatOutput).Hash
if ($FirstHash -ne $SecondHash) {
    throw 'determinism check failed: repeated output differs'
}

& $Exe --solver verify -in $Input -out $VerifyOutput
if ($LASTEXITCODE -ne 0) { throw "A*/Dijkstra verification failed with exit code $LASTEXITCODE" }
$Expected = @(Import-Csv -LiteralPath $ExpectedFile)
$Verified = @(Import-Csv -LiteralPath $VerifyOutput)
if ($Verified.Count -ne $Expected.Count) {
    throw "Exact row count mismatch: expected $($Expected.Count), got $($Verified.Count)"
}
for ($Index = 0; $Index -lt $Expected.Count; ++$Index) {
    if ($Expected[$Index].From -ne $Verified[$Index].From -or
        $Expected[$Index].To -ne $Verified[$Index].To -or
        [int]$Expected[$Index].delay -ne [int]$Verified[$Index].delay) {
        throw "Exact Golden mismatch at row $($Index + 1)"
    }
}

Write-Host 'PASS: fast competition CLI/schema/order/determinism and Exact A*/Dijkstra Golden agreement.'
