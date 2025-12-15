param()

Write-Host "Checking for created installer..."
$installer = Join-Path $Env:GITHUB_WORKSPACE 'bello-1.0.0-windows-x64.exe'

if (Test-Path $installer) {
    $size = (Get-Item $installer).Length / 1MB
    Write-Host "✓ Installer created: $installer ($([math]::Round($size, 2)) MB)"
    Write-Host "SUCCESS: Windows installer (.exe) created successfully!"
} else {
    Write-Host "❌ No installer found!"
    Write-Host "ERROR: Failed to create installer"
    exit 1
}