param()

# Create staging directory (same as NSIS packaging script)
$out = Join-Path $Env:GITHUB_WORKSPACE 'out'
Write-Host "Staging directory: $out"

if (Test-Path $out) { 
    Write-Host "Removing existing staging directory..."
    Remove-Item -Recurse -Force $out 
}
New-Item -ItemType Directory -Path $out | Out-Null
Write-Host "Created staging directory: $out"

# Locate built executable (robust search)
$buildPath = Join-Path $Env:GITHUB_WORKSPACE 'build'
Write-Host "Build path: $buildPath"

if (-not (Test-Path $buildPath)) {
    Write-Host "ERROR: Build directory not found: $buildPath"
    exit 1
}

$exe = Get-ChildItem -Path $buildPath -Filter 'bello.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) {
    $exe = Get-ChildItem -Path $buildPath -Include '*bello*.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $exe) {
    $exe = Get-ChildItem -Path (Join-Path $buildPath 'Release') -Filter '*.exe' -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'bello' } | Select-Object -First 1
}

if (-not $exe) {
    Write-Host "ERROR: Bello executable not found in build tree"
    exit 1
}

Write-Host "Found executable at: $($exe.FullName)"
$exeDir = Split-Path $exe.FullName

Write-Host "Copying runtime files from: $exeDir to staging: $out"
Copy-Item -Path (Join-Path $exeDir '*') -Destination $out -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Copied runtime files to staging directory"

# Prepare Inno Setup script by replacing placeholder with staging path
$issTemplate = Join-Path $Env:GITHUB_WORKSPACE '.github\scripts\windows\bello_installer.iss'
if (-not (Test-Path $issTemplate)) {
    Write-Host "ERROR: Inno Setup template not found: $issTemplate"
    exit 1
}

$resolvedOut = (Resolve-Path $out).Path
# Inno preprocessor expects backslashes; ensure correct quoting
$issContent = Get-Content $issTemplate -Raw
$issContent = $issContent -replace '\{#SourcePath\}', ($resolvedOut -replace '\\','\\')

$tempIss = Join-Path $Env:GITHUB_WORKSPACE 'bello_installer.iss'
$issContent | Out-File -FilePath $tempIss -Encoding ASCII
Write-Host "Written temporary Inno Setup script to: $tempIss"

# Ensure Inno Setup compiler exists
$isccPath = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (-not (Test-Path $isccPath)) {
    Write-Host "ISCC not found at default path: $isccPath"
    $isccPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue
}

if (-not $isccPath) {
    Write-Host "ERROR: Inno Setup compiler (ISCC.exe) not found. Ensure InnoSetup is installed."
    exit 1
}

Write-Host "Using ISCC: $isccPath"

# Run ISCC to build installer
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $isccPath
$startInfo.Arguments = "`"$tempIss`""
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.CreateNoWindow = $true

$process = [System.Diagnostics.Process]::Start($startInfo)
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()
$exitCode = $process.ExitCode

if ($stdout) { Write-Host $stdout }
if ($stderr) { Write-Host $stderr }

Remove-Item $tempIss -Force -ErrorAction SilentlyContinue

if ($exitCode -eq 0) {
    Write-Host "✓ Inno Setup reported success"

    # Try to locate the produced installer (ISCC typically writes into an 'Output' folder)
    $expectedNamePattern = 'bello-1.0.0-windows-x64*.exe'
    $foundInstaller = Get-ChildItem -Path (Join-Path (Split-Path $tempIss) 'Output') -Filter $expectedNamePattern -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

    if (-not $foundInstaller) {
        Write-Host "Installer not found in default Output folder; searching workspace for created EXE..."
        $foundInstaller = Get-ChildItem -Path $Env:GITHUB_WORKSPACE -Filter $expectedNamePattern -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    }

    if ($foundInstaller) {
        $destInstaller = Join-Path $Env:GITHUB_WORKSPACE 'bello-1.0.0-windows-x64.exe'
        Write-Host "Found installer at: $($foundInstaller.FullName). Copying to: $destInstaller"
        Copy-Item -Path $foundInstaller.FullName -Destination $destInstaller -Force
        Write-Host "✓ Installer copied to workspace root: $destInstaller"
    } else {
        Write-Host "WARNING: Could not locate the built installer automatically. Check ISCC output above for location."
    }
} else {
    Write-Host "❌ Inno Setup failed with exit code: $exitCode"
    exit 1
}
