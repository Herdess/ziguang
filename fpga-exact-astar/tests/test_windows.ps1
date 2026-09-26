$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $ProjectRoot 'build\exact_astar.exe'
$Input = Join-Path $ProjectRoot 'tests\golden_sample.csv'
$Output = Join-Path $ProjectRoot 'build\verify_result.csv'

if (-not (Test-Path -LiteralPath $Exe)) {
    & (Join-Path $ProjectRoot 'build_windows.ps1')
}

& $Exe --solver verify --input $Input --output $Output --limit 8
if ($LASTEXITCODE -ne 0) { throw "verify mode failed with exit code $LASTEXITCODE" }

$Expected = @(Import-Csv -LiteralPath $Input | Select-Object -First 8)
$Actual = @(Import-Csv -LiteralPath $Output)
if ($Actual.Count -ne $Expected.Count) {
    throw "row count mismatch: expected $($Expected.Count), got $($Actual.Count)"
}
for ($Index = 0; $Index -lt $Expected.Count; ++$Index) {
    if ($Expected[$Index].From -ne $Actual[$Index].From -or
        $Expected[$Index].To -ne $Actual[$Index].To -or
        [int]$Expected[$Index].delay -ne [int]$Actual[$Index].delay) {
        throw "Golden mismatch at row $($Index + 1)"
    }
}
Write-Host 'PASS: Exact A*, bidirectional Dijkstra, and 8-row Golden CSV are identical.'

