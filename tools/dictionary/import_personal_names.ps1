param(
  [switch]$SkipDownload
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\personal_names"
$Bz2 = Join-Path $OutDir "mozcdic-ut-personal-names.txt.bz2"
$Txt = Join-Path $OutDir "mozcdic-ut-personal-names.txt"
$LockFile = Join-Path $PSScriptRoot "locks\daily_sources.json"
$Lock = Get-Content -LiteralPath $LockFile -Encoding UTF8 -Raw | ConvertFrom-Json
$Repository = [string]$Lock.personal_names.repository
$Revision = [string]$Lock.personal_names.revision
$ExpectedHash = ([string]$Lock.personal_names.sha256).ToUpperInvariant()
$ExpectedBytes = [long]$Lock.personal_names.bytes
if ($Repository -ne 'utuhiro78/mozcdic-ut-personal-names' -or $Revision -notmatch '^[0-9a-f]{40}$') {
    throw "Invalid pinned personal-names repository or revision"
}
$Url = "https://raw.githubusercontent.com/$Repository/$Revision/$($Lock.personal_names.path)"

New-Item -ItemType Directory -Force $OutDir | Out-Null

if (-not $SkipDownload) {
  Write-Host "Downloading SHA256-locked personal names dictionary..."
  Write-Host "  $Url"
  $Tmp = $Bz2 + "." + [guid]::NewGuid().ToString('N') + ".tmp"
  try {
    Invoke-WebRequest -Uri $Url -OutFile $Tmp -UseBasicParsing
    if ((Get-Item -LiteralPath $Tmp).Length -ne $ExpectedBytes) { throw "Pinned personal names byte count mismatch" }
    if ((Get-FileHash -LiteralPath $Tmp -Algorithm SHA256).Hash -ne $ExpectedHash) {
      throw "Pinned personal names SHA256 mismatch"
    }
    Move-Item -LiteralPath $Tmp -Destination $Bz2 -Force
  } finally {
    if (Test-Path -LiteralPath $Tmp) { Remove-Item -LiteralPath $Tmp -Force }
  }
} else {
  Write-Host "Skipping download; verifying pinned archive."
}

if (-not (Test-Path -LiteralPath $Bz2 -PathType Leaf)) {
  throw "bz2 file does not exist: $Bz2"
}
if ((Get-Item -LiteralPath $Bz2).Length -ne $ExpectedBytes -or
    (Get-FileHash -LiteralPath $Bz2 -Algorithm SHA256).Hash -ne $ExpectedHash) {
  throw "Pinned personal names archive differs from lock; refusing to unpack"
}

$env:MOZKEY_PERSONAL_NAMES_BZ2 = (Resolve-Path $Bz2).Path
$env:MOZKEY_PERSONAL_NAMES_TXT = $Txt

$PythonCode = "import bz2, os, pathlib; src = pathlib.Path(os.environ['MOZKEY_PERSONAL_NAMES_BZ2']); dst = pathlib.Path(os.environ['MOZKEY_PERSONAL_NAMES_TXT']); dst.write_bytes(bz2.decompress(src.read_bytes())); print(f'wrote: {dst}'); print(f'bytes: {dst.stat().st_size}')"

python -c $PythonCode

if ($LASTEXITCODE -ne 0) {
  throw "failed to decompress mozcdic-ut personal names dictionary."
}

Write-Host ""
Write-Host "Personal names dictionary is ready:"
Write-Host "  $Txt"
