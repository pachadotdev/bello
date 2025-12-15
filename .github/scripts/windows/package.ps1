param()

# Create staging directory
$out = Join-Path $Env:GITHUB_WORKSPACE 'out'
Write-Host "Staging directory: $out"

if (Test-Path $out) { 
    Write-Host "Removing existing staging directory..."
    Remove-Item -Recurse -Force $out 
}
New-Item -ItemType Directory -Path $out | Out-Null
Write-Host "Created staging directory: $out"

# Copy built executable (robust search across common VS/CMake output layouts)
Write-Host "Looking for bello executable in build directory..."
$buildPath = Join-Path $Env:GITHUB_WORKSPACE 'build'
Write-Host "Build path: $buildPath"

if (Test-Path $buildPath) {
    Write-Host "Build directory contents (sample):"
    Get-ChildItem $buildPath -Recurse -Depth 2 | Select-Object FullName | ForEach-Object { Write-Host "  $($_.FullName)" }
} else {
    Write-Host "Build directory does not exist!"
}

# Try several strategies to locate the built executable
$exe = $null

# 1) Direct search for bello.exe
$exe = Get-ChildItem -Path $buildPath -Filter 'bello.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

# 2) Fallback: any exe with 'bello' in the name
if (-not $exe) {
    $exe = Get-ChildItem -Path $buildPath -Include '*bello*.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
}

# 3) Fallback: search common Release/Debug subfolders
if (-not $exe) {
    $exe = Get-ChildItem -Path (Join-Path $buildPath 'Release') -Filter '*.exe' -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'bello' } | Select-Object -First 1
}

if ($exe) {
    Write-Host "Found executable at: $($exe.FullName)"
    $exeDir = Split-Path $exe.FullName

    # Copy all files from the executable directory to staging so DLLs and helpers are included
    Write-Host "Copying runtime files from: $exeDir to staging: $out"
    Copy-Item -Path (Join-Path $exeDir '*') -Destination $out -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Copied runtime files to staging directory"
} else {
    Write-Host "ERROR: Bello executable not found in build tree."
    Write-Host "Searched path: $buildPath"
    exit 1
}

# Copy duckdb runtime files (if present)
$duckdbPath = Join-Path $Env:GITHUB_WORKSPACE 'duckdb'
if (Test-Path $duckdbPath) { 
    Write-Host "Copying duckdb directory..."
    Copy-Item -Path $duckdbPath -Destination (Join-Path $out 'duckdb') -Recurse -Force 
    Write-Host "Copied duckdb files to staging directory"
} else {
    Write-Host "No duckdb directory found"
}

# Show staging directory contents
Write-Host "Staging directory contents:"
Get-ChildItem $out -Recurse | Select-Object FullName | ForEach-Object { Write-Host "  $($_.FullName)" }

# NSIS installer creation with simplified, robust approach
$installerExe = Join-Path $Env:GITHUB_WORKSPACE 'bello-1.0.0-windows-x64.exe'

# Check if makensis is available
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if ($makensis) {
    Write-Host "makensis found at: $($makensis.Source)"
    Write-Host "Creating NSIS installer at: $installerExe"
    
    # Verify files exist before creating NSIS script
    if (-not (Test-Path $out)) {
        Write-Host "ERROR: Staging directory does not exist: $out"
        exit 1
    }
    
    $stagingFiles = Get-ChildItem $out -Recurse
    if ($stagingFiles.Count -eq 0) {
        Write-Host "ERROR: No files in staging directory"
        exit 1
    }
    
    Write-Host "Found $($stagingFiles.Count) files in staging directory"
    
    # Convert paths to forward slashes for NSIS and make them absolute
    $outPathForNSIS = (Resolve-Path $out).Path -replace '\\', '/'
    $installerExeForNSIS = $installerExe -replace '\\', '/'
    
    Write-Host "Source files path (resolved): $outPathForNSIS"
    Write-Host "Installer output: $installerExeForNSIS"
    
    # Verify source path exists
    if (-not (Test-Path $outPathForNSIS.Replace('/', '\'))) {
        Write-Host "ERROR: Source path does not exist: $outPathForNSIS"
        exit 1
    }
    
    # Create a robust NSIS script
    $nsiContent = @"
; NSIS Installer for Bello
!verbose 4

Name "Bello"
OutFile "$installerExeForNSIS"
InstallDir `$PROGRAMFILES64\Bello
RequestExecutionLevel admin

; Use better compression
SetCompress auto
SetCompressor /SOLID lzma

; Default installation section
Section "MainSection" SEC01
    ; Set output path to installation directory
    SetOutPath "`$INSTDIR"
    
    ; Install all files from the staging directory
    File /r "$outPathForNSIS/*"
    
    ; Create Start Menu folder
    SetShellVarContext all
    CreateDirectory "`$SMPROGRAMS\Bello"
    
    ; Create shortcuts
    CreateShortcut "`$SMPROGRAMS\Bello\Bello.lnk" "`$INSTDIR\bello.exe" "" "`$INSTDIR\bello.exe" 0
    CreateShortcut "`$DESKTOP\Bello.lnk" "`$INSTDIR\bello.exe" "" "`$INSTDIR\bello.exe" 0
    
    ; Write registry information for Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "DisplayName" "Bello"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "UninstallString" "`$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "DisplayIcon" "`$INSTDIR\bello.exe,0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "Publisher" "Bello Project"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "DisplayVersion" "1.0.0"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello" "NoRepair" 1
    
    ; Create uninstaller
    WriteUninstaller "`$INSTDIR\Uninstall.exe"
    
    ; Create uninstaller shortcut
    CreateShortcut "`$SMPROGRAMS\Bello\Uninstall.lnk" "`$INSTDIR\Uninstall.exe" "" "`$INSTDIR\Uninstall.exe" 0
SectionEnd

; Uninstaller section
Section "Uninstall"
    ; Set shell context to all users
    SetShellVarContext all
    
    ; Remove all files (be careful with this!)
    Delete "`$INSTDIR\bello.exe"
    Delete "`$INSTDIR\Uninstall.exe"
    
    ; Remove DuckDB folder if it exists
    RMDir /r "`$INSTDIR\duckdb"
    
    ; Remove installation directory if empty
    RMDir "`$INSTDIR"
    
    ; Remove shortcuts
    Delete "`$SMPROGRAMS\Bello\Bello.lnk"
    Delete "`$SMPROGRAMS\Bello\Uninstall.lnk"
    RMDir "`$SMPROGRAMS\Bello"
    Delete "`$DESKTOP\Bello.lnk"
    
    ; Remove registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bello"
SectionEnd
"@
    
    # Create temporary NSIS script
    $nsi = Join-Path $Env:GITHUB_WORKSPACE 'bello_installer.nsi'
    
    # Write the NSIS script with proper encoding
    try {
        $nsiContent | Out-File -FilePath $nsi -Encoding UTF8 -NoNewline
        Write-Host "NSIS script written to: $nsi"
        
        # Show script for debugging
        Write-Host "NSIS script content:"
        Write-Host "----------------------------------------"
        Get-Content $nsi | ForEach-Object { Write-Host $_ }
        Write-Host "----------------------------------------"
        
        # Run makensis with detailed output
        Write-Host "Running makensis..."
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = "makensis"
        $startInfo.Arguments = "/V4 `"$nsi`""
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $startInfo.CreateNoWindow = $true
        
        $process = [System.Diagnostics.Process]::Start($startInfo)
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        
        Write-Host "NSIS Output (stdout):"
        if ($stdout) { $stdout | Write-Host }
        
        if ($stderr) {
            Write-Host "NSIS Errors (stderr):"
            $stderr | Write-Host
        }
        
        Write-Host "NSIS exit code: $exitCode"
        
        # Clean up temporary script
        Remove-Item $nsi -Force -ErrorAction SilentlyContinue
        
        if ($exitCode -eq 0 -and (Test-Path $installerExe)) {
            Write-Host "✓ Successfully created installer: $installerExe"
            $installerSize = (Get-Item $installerExe).Length / 1MB
            Write-Host "Installer size: $([math]::Round($installerSize, 2)) MB"
            Write-Host "SUCCESS: NSIS installer created successfully!"
        } else {
            Write-Host "❌ NSIS installer creation failed (exit code: $exitCode)"
            if (-not (Test-Path $installerExe)) {
                Write-Host "Installer file was not created"
            }
            exit 1
        }
    }
    catch {
        Write-Host "❌ Error creating NSIS script: $_"
        Remove-Item $nsi -Force -ErrorAction SilentlyContinue
        exit 1
    }
} else {
    Write-Host "❌ makensis not found - cannot create installer"
    exit 1
}
