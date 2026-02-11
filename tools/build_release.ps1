# release_build.ps1
# Builds release package for LR2BGAFilter

param (
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $RootDir "dist"
$PackageName = "LR2BGAFilter-v$Version"
$PackageDir = Join-Path $DistDir $PackageName
$ZipPath = Join-Path $DistDir "$PackageName.zip"

Write-Host "Building Release Package v$Version..." -ForegroundColor Cyan

# 1. Setup Directories
if (Test-Path $PackageDir) { Remove-Item -Recurse -Force $PackageDir }
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

# 2. Build C++ Filter (using MSBuild)
Write-Host "Building C++ Filter..." -ForegroundColor Yellow
$MsBuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
if (-not $MsBuild) { throw "MSBuild not found." }

& $MsBuild "$RootDir\LR2BGAFilter.sln" /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /v:m
if ($LASTEXITCODE -ne 0) { throw "C++ Build Failed." }

# 3. Build C# Installer
Write-Host "Building C# Installer..." -ForegroundColor Yellow
& $MsBuild "$RootDir\tools\Installer\Installer.sln" /t:Rebuild /p:Configuration=Release /p:Platform="Any CPU" /v:m
if ($LASTEXITCODE -ne 0) { throw "C# Build Failed." }

# 4. Copy Files
Write-Host "Copying files..." -ForegroundColor Yellow

$FilesToCopy = @(
    @{ Src = "$RootDir\bin\Win32\Release\LR2BGAFilter.ax"; Dest = "." },
    @{ Src = "$RootDir\bin\Win32\Release\LR2BGAFilterConfigurationTool.bat"; Dest = "." },
    @{ Src = "$RootDir\tools\Installer\Installer\bin\Release\net472\win-x86\Installer.exe"; Dest = "." },
    @{ Src = "$RootDir\README.md"; Dest = "." },
    @{ Src = "$RootDir\README.ja.md"; Dest = "." },
    @{ Src = "$RootDir\README.ko.md"; Dest = "." },
    @{ Src = "$RootDir\docs\*"; Dest = "docs" }
)

foreach ($File in $FilesToCopy) {
    if (-not (Test-Path $File.Src)) { throw "Missing file: $($File.Src)" }
    $DestPath = Join-Path $PackageDir $File.Dest
    if ($File.Dest -ne "." -and -not (Test-Path $DestPath)) { New-Item -ItemType Directory -Force -Path $DestPath | Out-Null }
    Copy-Item -Recurse -Force $File.Src $DestPath
}

# 5. Create ZIP
Write-Host "Creating ZIP archive..." -ForegroundColor Yellow
Compress-Archive -Path "$PackageDir\*" -DestinationPath $ZipPath

Write-Host "Done! Package created at: $ZipPath" -ForegroundColor Green
