param()

New-Item -ItemType Directory -Path (Join-Path $Env:GITHUB_WORKSPACE 'build') -Force | Out-Null

# Find the DuckDB library file
$duckdbDir = Join-Path $Env:GITHUB_WORKSPACE 'duckdb'
$duckdbLib = $null

# Try different possible library names (prioritize .lib files for linking)
$possibleLibs = @("duckdb.lib", "libduckdb.lib", "duckdb_static.lib", "libduckdb_static.lib")
foreach ($lib in $possibleLibs) {
    $libPath = Join-Path $duckdbDir $lib
    if (Test-Path $libPath) {
        $duckdbLib = $libPath
        Write-Host "Found DuckDB library: $duckdbLib"
        break
    }
}

if (-not $duckdbLib) {
    Write-Host "Warning: Could not find DuckDB library file. Available files in duckdb directory:"
    Get-ChildItem $duckdbDir -ErrorAction SilentlyContinue
    # Use a fallback path - try .lib first
    $duckdbLib = Join-Path $duckdbDir "duckdb.lib"
}

Write-Host "Configuring CMake with Qt6_DIR=$Env:Qt6_DIR"
Write-Host "DuckDB include dir: $duckdbDir"
Write-Host "DuckDB library: $duckdbLib"

# Use Visual Studio generator explicitly to support platform specification
cmake -S $Env:GITHUB_WORKSPACE -B (Join-Path $Env:GITHUB_WORKSPACE 'build') -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DQt6_DIR="$Env:Qt6_DIR" -DDUCKDB_INCLUDE_DIRS="$duckdbDir" -DDUCKDB_LIBRARIES="$duckdbLib"
