$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ToolsDir = Join-Path $ProjectRoot '.tools'
$Version = '0.16.0'
$FolderName = "zig-x86_64-windows-$Version"
$InstallDir = Join-Path $ToolsDir $FolderName
$ZigExe = Join-Path $InstallDir 'zig.exe'
$Archive = Join-Path $ToolsDir "$FolderName.zip"
$Url = "https://ziglang.org/download/$Version/$FolderName.zip"
$ExpectedSha256 = '68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e'

if (Test-Path -LiteralPath $ZigExe) {
    Write-Host "Zig $Version is already available."
    exit 0
}

New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null
Write-Host "Downloading the official Zig $Version Windows toolchain..."
$Curl = Get-Command 'curl.exe' -ErrorAction SilentlyContinue
if ($Curl) {
    & $Curl.Source '--fail' '--location' '--retry' '4' '--continue-at' '-' `
        '--output' $Archive $Url
    if ($LASTEXITCODE -ne 0) { throw "Zig download failed with exit code $LASTEXITCODE" }
} else {
    Invoke-WebRequest -Uri $Url -OutFile $Archive
}
$ActualSha256 = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualSha256 -ne $ExpectedSha256) {
    throw "Zig archive checksum mismatch: $ActualSha256"
}
Expand-Archive -LiteralPath $Archive -DestinationPath $ToolsDir -Force
if (-not (Test-Path -LiteralPath $ZigExe)) {
    throw "Zig extraction did not create $ZigExe"
}
Remove-Item -LiteralPath $Archive -Force
Write-Host "Installed Zig $Version in $InstallDir"

