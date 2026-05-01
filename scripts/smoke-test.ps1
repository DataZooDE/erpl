#Requires -Version 5.1
<#
.SYNOPSIS
    Post-build ERPL smoke test for Windows.

.DESCRIPTION
    Downloads the official DuckDB CLI for the exact version the extension was built against,
    installs the extension, loads it, and verifies SAP functions are registered.
    No SAP connection required.

.PARAMETER ExtensionPath
    Path to erpl.duckdb_extension. Defaults to build\release\extension\erpl\erpl.duckdb_extension.

.PARAMETER DuckdbVersion
    DuckDB version tag, e.g. "v1.5.1". Falls back to DUCKDB_GIT_VERSION env var, then git submodule.

.PARAMETER CliCacheDir
    Directory to cache downloaded DuckDB CLI binaries. Defaults to $env:TEMP\duckdb-cli-cache.
#>
[CmdletBinding()]
param(
    [string]$ExtensionPath,
    [string]$DuckdbVersion,
    [string]$CliCacheDir = "$env:TEMP\duckdb-cli-cache"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjDir   = Split-Path -Parent $ScriptDir

# ── 1. Resolve extension path ────────────────────────────────────────────────
if (-not $ExtensionPath) {
    $ExtensionPath = Join-Path $ProjDir "build\release\extension\erpl\erpl.duckdb_extension"
}
if (-not (Test-Path $ExtensionPath)) {
    Write-Error "Extension not found: $ExtensionPath"
    exit 1
}

# ── 2. Detect DuckDB version ─────────────────────────────────────────────────
if (-not $DuckdbVersion) { $DuckdbVersion = $env:DUCKDB_GIT_VERSION }
if (-not $DuckdbVersion) {
    try {
        $DuckdbVersion = (& git -C "$ProjDir\duckdb" describe --tags --exact-match 2>$null).Trim()
    } catch {}
}
if (-not $DuckdbVersion) {
    Write-Error "Cannot determine DuckDB version. Set DUCKDB_GIT_VERSION (e.g. v1.5.1)."
    exit 1
}

# ── 3. Download and cache DuckDB CLI ─────────────────────────────────────────
$CliSlug = "windows-amd64"
$CliDir  = Join-Path $CliCacheDir "duckdb-$DuckdbVersion-$CliSlug"
$CliExe  = Join-Path $CliDir "duckdb.exe"

if (-not (Test-Path $CliExe)) {
    New-Item -ItemType Directory -Path $CliDir -Force | Out-Null
    $Url    = "https://github.com/duckdb/duckdb/releases/download/$DuckdbVersion/duckdb_cli-$CliSlug.zip"
    $ZipTmp = Join-Path $env:TEMP "duckdb-cli-$([System.Guid]::NewGuid()).zip"
    Write-Host "[smoke-test] Downloading DuckDB $DuckdbVersion CLI for $CliSlug ..."
    Invoke-WebRequest -Uri $Url -OutFile $ZipTmp -UseBasicParsing
    Expand-Archive -Path $ZipTmp -DestinationPath $CliDir -Force
    Remove-Item $ZipTmp
} else {
    Write-Host "[smoke-test] Using cached CLI: $CliExe"
}

# ── 4. Isolated APPDATA (DuckDB uses APPDATA for extension storage on Windows) ─
$SmokeAppData = Join-Path $env:TEMP "erpl-smoke-$([System.Guid]::NewGuid())"
New-Item -ItemType Directory -Path $SmokeAppData -Force | Out-Null

# Forward slashes for the SQL INSTALL statement
$ExtPathFwd = $ExtensionPath -replace '\\', '/'

$Sql = @"
INSTALL '$ExtPathFwd';
LOAD erpl;
SELECT extension_name, loaded FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%' ORDER BY extension_name;
SELECT count(*) AS sap_function_count FROM duckdb_functions() WHERE function_name LIKE 'sap_%';
SELECT function_name FROM duckdb_functions() WHERE function_name IN ('sap_read_table', 'sap_rfc_invoke', 'sap_show_tables') ORDER BY function_name;
"@

Write-Host ""
Write-Host "=== ERPL Smoke Test ==="
Write-Host "Extension: $ExtensionPath"
Write-Host "DuckDB:    $DuckdbVersion ($CliSlug)"
Write-Host ""

# ── 5. Run DuckDB with isolated APPDATA ──────────────────────────────────────
$PrevAppData = $env:APPDATA
$env:APPDATA = $SmokeAppData
$PrevDisableTelemetry = $env:DATAZOO_DISABLE_TELEMETRY
$env:DATAZOO_DISABLE_TELEMETRY = "1"
$ExitCode = 0
$OutputStr = ""
try {
    $Output = & $CliExe -unsigned -c $Sql 2>&1
    $ExitCode = $LASTEXITCODE
    $OutputStr = ($Output | Out-String)
    Write-Host $OutputStr
} finally {
    $env:APPDATA = $PrevAppData
    if ($null -eq $PrevDisableTelemetry) { Remove-Item Env:DATAZOO_DISABLE_TELEMETRY -ErrorAction SilentlyContinue }
    else { $env:DATAZOO_DISABLE_TELEMETRY = $PrevDisableTelemetry }
    Remove-Item $SmokeAppData -Recurse -Force -ErrorAction SilentlyContinue
}

# ── 6. Validate results ───────────────────────────────────────────────────────
if ($ExitCode -ne 0) {
    Write-Error "DuckDB exited with code $ExitCode"
    exit $ExitCode
}
if ($OutputStr -notmatch 'erpl.*true') {
    Write-Error "erpl extension is not showing as loaded in duckdb_extensions()"
    exit 1
}
if ($OutputStr -notmatch 'sap_read_table') {
    Write-Error "sap_read_table not found in duckdb_functions()"
    exit 1
}
if ($OutputStr -notmatch 'sap_rfc_invoke') {
    Write-Error "sap_rfc_invoke not found in duckdb_functions()"
    exit 1
}

Write-Host ""
Write-Host "=== Smoke test PASSED ==="
