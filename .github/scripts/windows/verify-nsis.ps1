param()

Write-Host "Checking NSIS installation..."

# First, try to find makensis in current PATH
$makensis = Get-Command makensis -ErrorAction SilentlyContinue

if (-not $makensis) {
    Write-Host "makensis not found in current PATH, searching for NSIS installation..."
    
    # Common NSIS installation paths
    $possiblePaths = @(
        "${env:ProgramFiles}\NSIS\makensis.exe",
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "C:\Program Files\NSIS\makensis.exe",
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "C:\ProgramData\chocolatey\lib\nsis\tools\makensis.exe",
        "C:\tools\NSIS\makensis.exe"
    )
    
    Write-Host "Searching for NSIS in common locations..."
    foreach ($path in $possiblePaths) {
        Write-Host "Checking: $path"
        if (Test-Path $path) {
            Write-Host "✓ Found NSIS at: $path"
            $nsisDir = Split-Path $path
            $env:PATH = "$nsisDir;$env:PATH"
            Write-Host "Added NSIS directory to PATH: $nsisDir"
            break
        }
    }
    
    # Try again after potentially adding to PATH
    $makensis = Get-Command makensis -ErrorAction SilentlyContinue
}

if ($makensis) {
    Write-Host "✓ makensis found at: $($makensis.Source)"
    
    # Get version
    try {
        $version = & makensis /VERSION 2>&1
        Write-Host "NSIS version: $version"
    } catch {
        Write-Host "Could not get NSIS version: $_"
    }
    
    Write-Host "Testing NSIS with simple script..."
    
    # Test NSIS with a minimal script
    $testScript = @'
Name "Test"
OutFile "test.exe"
Section
    DetailPrint "NSIS is working!"
SectionEnd
'@
    
    $testNsi = "test.nsi"
    try {
        $testScript | Out-File -FilePath $testNsi -Encoding UTF8
        $testResult = & makensis $testNsi 2>&1
        Write-Host "NSIS test result: $testResult"
        
        if (Test-Path "test.exe") {
            Write-Host "✓ NSIS test successful - test.exe created"
            Remove-Item "test.exe" -ErrorAction SilentlyContinue
        } else {
            Write-Host "⚠ NSIS test may have failed - no test.exe created"
        }
        
        Remove-Item $testNsi -ErrorAction SilentlyContinue
    } catch {
        Write-Host "❌ NSIS test failed: $_"
        Remove-Item $testNsi -ErrorAction SilentlyContinue
        exit 1
    }
    
    Write-Host "✓ NSIS is working correctly"
} else {
    Write-Host "❌ NSIS/makensis still not found after searching"
    Write-Host "Current PATH:"
    $env:PATH -split ';' | ForEach-Object { Write-Host "  $_" }
    
    # Try to find any NSIS-related files for debugging
    Write-Host "Searching for any NSIS files on system..."
    try {
        $nsisFiles = Get-ChildItem -Path "C:\" -Recurse -Filter "*nsis*" -ErrorAction SilentlyContinue | Select-Object -First 10
        if ($nsisFiles) {
            Write-Host "Found NSIS-related files:"
            $nsisFiles | ForEach-Object { Write-Host "  $($_.FullName)" }
        } else {
            Write-Host "No NSIS files found"
        }
    } catch {
        Write-Host "Could not search for NSIS files: $_"
    }
    
    exit 1
}