param(
    [ValidateSet("ncaq", "Niccori250k")]
    [string]$Source = "Niccori250k"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\nico_pixiv"
$OutFile = Join-Path $OutDir "dic-nico-intersection-pixiv-google.txt"

# Daily builds must never resolve a moving branch.
$LockFile = Join-Path $PSScriptRoot "locks\daily_sources.json"
$Lock = Get-Content -LiteralPath $LockFile -Encoding UTF8 -Raw | ConvertFrom-Json
if ($Source -ne "Niccori250k") {
    throw "Only Niccori250k is supported by the reviewed daily lock. Update lock via separate review."
}
$Repository = [string]$Lock.nico_pixiv.repository
$Revision = [string]$Lock.nico_pixiv.revision
$ExpectedHash = ([string]$Lock.nico_pixiv.sha256).ToUpperInvariant()
$ExpectedBytes = [long]$Lock.nico_pixiv.bytes
if ($Repository -ne 'Niccori250k/dic-nico-intersection-pixiv' -or $Revision -notmatch '^[0-9a-f]{40}$') {
    throw "Invalid pinned Nico/Pixiv repository or revision"
}
$Url = "https://raw.githubusercontent.com/$Repository/$Revision/$($Lock.nico_pixiv.path)"

New-Item -ItemType Directory -Force $OutDir | Out-Null
$TempFile = Join-Path $OutDir ("dic-nico-intersection-pixiv-google.txt." + [guid]::NewGuid().ToString("N") + ".tmp")

Write-Host "Source: $Source"
Write-Host "Upstream: SHA256-locked commit"
Write-Host "Resolved revision: $Revision"
Write-Host "Downloading: $Url"
Write-Host "Output: $OutFile"

try {
    Invoke-WebRequest -Uri $Url -OutFile $TempFile -UseBasicParsing
    $Bytes = (Get-Item -LiteralPath $TempFile).Length
    if ($Bytes -lt 5000000) {
        throw "Unexpectedly small nico/pixiv dictionary: $Bytes bytes"
    }
    if ($Bytes -ne $ExpectedBytes) {
        throw "Nico/Pixiv pinned byte count mismatch: expected $ExpectedBytes got $Bytes"
    }
    # Reject truncated or unrecognized content before replacing last known good data.
    $Header = @(Get-Content -LiteralPath $TempFile -Encoding UTF8 -TotalCount 8)
    if (-not (@($Header | Where-Object { $_ -eq '# name: dic-nico-intersection-pixiv' }).Count -gt 0)) {
        throw 'Downloaded file does not look like dic-nico-intersection-pixiv.'
    }
    $ActualHash = (Get-FileHash -LiteralPath $TempFile -Algorithm SHA256).Hash
    if ($ActualHash -ne $ExpectedHash) {
        throw "Nico/Pixiv pinned SHA256 mismatch: expected $ExpectedHash got $ActualHash"
    }
    $LineCount = (Get-Content -LiteralPath $TempFile -Encoding UTF8 | Measure-Object -Line).Lines
    if ($LineCount -lt 80000 -or $LineCount -gt 250000) {
        throw "Unexpected dictionary line count: $LineCount"
    }
    Move-Item -LiteralPath $TempFile -Destination $OutFile -Force
} finally {
    if (Test-Path -LiteralPath $TempFile) {
        Remove-Item -LiteralPath $TempFile -Force
    }
}

# Record the actual revision and hash for build traceability without freezing
# subsequent imports. This file is under the Git-ignored generated directory.
$Provenance = [ordered]@{
    source = $Source
    repository = $Repository
    upstream_ref = $Revision
    resolved_revision = $Revision
    download_url = $Url
    sha256 = $ActualHash
    bytes = $Bytes
    lines = $LineCount
    retrieved_at_utc = [DateTime]::UtcNow.ToString('o')
}
$ManifestPath = Join-Path $OutDir 'dic-nico-intersection-pixiv-source.json'
$ManifestText = $Provenance | ConvertTo-Json -Depth 3
[System.IO.File]::WriteAllText($ManifestPath, $ManifestText + "`n", (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host 'Downloaded and checked:'
Write-Host "  lines: $LineCount"
Write-Host "  bytes: $Bytes"
Write-Host "  sha256: $ActualHash"
Write-Host "  revision: $Revision"
Write-Host "  source-manifest: $ManifestPath"
