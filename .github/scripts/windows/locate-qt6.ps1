param()

$candidates = @('C:\Qt','C:\Program Files\Qt','C:\tools\Qt','C:\tools\qt')
$found = $null
foreach ($base in $candidates) {
  if (Test-Path $base) {
    $cfg = Get-ChildItem -Path $base -Filter 'Qt6Config.cmake' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cfg) { $found = $cfg.Directory.FullName; break }
  }
}
if (-not $found) {
  Write-Host 'Qt6Config.cmake not found in common locations. Searching C:\ (may take a while)...'
  $cfg = Get-ChildItem -Path 'C:\' -Filter 'Qt6Config.cmake' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($cfg) { $found = $cfg.Directory.FullName }
}
if (-not $found) { Write-Error 'Could not locate Qt6Config.cmake. Ensure Qt is installed or set Qt6_DIR manually.'; exit 1 }
Write-Host "Found Qt6_DIR: $found"
Add-Content -Path $Env:GITHUB_ENV -Value "Qt6_DIR=$found"
