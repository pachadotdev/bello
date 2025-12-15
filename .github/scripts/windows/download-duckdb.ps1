param()

$duckDir = Join-Path $Env:GITHUB_WORKSPACE 'duckdb'
if (-Not (Test-Path $duckDir) -or (Get-ChildItem $duckDir -Recurse -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
  Write-Host "Downloading DuckDB to: $duckDir"
  New-Item -ItemType Directory -Path $duckDir | Out-Null
  # Use the C++ development package which includes import libraries
  $url = 'https://github.com/duckdb/duckdb/releases/download/v1.4.3/libduckdb-windows-amd64.zip'
  $tmp = Join-Path $Env:GITHUB_WORKSPACE 'duckdb.zip'
  
  Write-Host "Downloading from: $url"
  Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
  Expand-Archive -Path $tmp -DestinationPath $duckDir -Force
  Remove-Item $tmp -Force
  
  Write-Host "Downloaded files:"
  Get-ChildItem $duckDir -Recurse
} else {
  Write-Host 'duckdb already present; skipping download'
}
