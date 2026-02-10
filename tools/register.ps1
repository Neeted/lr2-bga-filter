# register.ps1
# LR2 BGA Filter - Register/Unregister Script
# Please run as Administrator

param(
    [int]$Merit = 0xFFF00000,  # Default: MERIT_PREFERRED
    [switch]$Unregister        # Unregister only
)

$ErrorActionPreference = "Stop"

# Path Logic
# 1. Current Directory (Flat Layout)
$FilterPath = Join-Path $PSScriptRoot "LR2BGAFilter.ax"

if (-not (Test-Path $FilterPath)) {
    # 2. Parent Directory (Tools Layout)
    $FilterPath = Join-Path $PSScriptRoot "..\LR2BGAFilter.ax"
}

if (-not (Test-Path $FilterPath)) {
    # 3. Dev Environment (Relative from tools)
    $DebugPath = Join-Path $PSScriptRoot "..\bin\Win32\Debug\LR2BGAFilter.ax"
    if (Test-Path $DebugPath) {
        $FilterPath = $DebugPath
    }
    else {
        $FilterPath = Join-Path $PSScriptRoot "..\bin\Win32\Release\LR2BGAFilter.ax"
    }
}
$FilterGuid = "{61E38596-D44C-4097-89AF-AABBA85DAA6D}"

# DirectShow Filter Category GUID
$LegacyAmFilterCategory = "{083863F1-70DE-11d0-BD40-00A0C911CE86}"

# Admin Check
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")
if (-not $isAdmin) {
    Write-Host "Error: Run as Administrator required." -ForegroundColor Red
    Write-Host "Right-click -> Run as Administrator" -ForegroundColor Yellow
    exit 1
}

# File Existence Check
if (-not (Test-Path $FilterPath)) {
    Write-Host "Error: Filter not found: $FilterPath" -ForegroundColor Red
    Write-Host "Please build the project first." -ForegroundColor Yellow
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " LR2 BGA Filter Registration" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Unregister
Write-Host "[1/3] Unregistering existing filter..." -ForegroundColor Yellow
$result = Start-Process -FilePath "regsvr32" -ArgumentList "/u /s `"$FilterPath`"" -Wait -PassThru
if ($result.ExitCode -eq 0) {
    Write-Host "      Unregistered (or not registered)." -ForegroundColor Green
}
else {
    Write-Host "      Unregister skipped." -ForegroundColor Gray
}

if ($Unregister) {
    Write-Host ""
    Write-Host "Unregistration Complete." -ForegroundColor Green
    exit 0
}

# Register
Write-Host "[2/3] Registering filter..." -ForegroundColor Yellow
$result = Start-Process -FilePath "regsvr32" -ArgumentList "/s `"$FilterPath`"" -Wait -PassThru
if ($result.ExitCode -ne 0) {
    Write-Host "      Registration Failed (ExitCode: $($result.ExitCode))" -ForegroundColor Red
    exit 1
}
Write-Host "      Registered successfully." -ForegroundColor Green

# Set Merit (Modify FilterData binary)
Write-Host "[3/3] Setting Merit..." -ForegroundColor Yellow

$meritSet = $false

# Find Filter Category Instance paths
$instancePaths = @(
    "Registry::HKEY_CLASSES_ROOT\CLSID\$LegacyAmFilterCategory\Instance\$FilterGuid",
    "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$LegacyAmFilterCategory\Instance\$FilterGuid",
    "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\$LegacyAmFilterCategory\Instance\$FilterGuid"
)

foreach ($path in $instancePaths) {
    if (Test-Path $path) {
        try {
            # Read FilterData
            $filterData = (Get-ItemProperty -Path $path -Name "FilterData").FilterData
            
            if ($filterData -and $filterData.Length -ge 8) {
                # FilterData Structure:
                # Offset 0-3: Version (usually 0x00000002)
                # Offset 4-7: Merit (little-endian DWORD)
                
                # Set Merit (Little-Endian)
                $filterData[4] = [byte]($Merit -band 0xFF)
                $filterData[5] = [byte](($Merit -shr 8) -band 0xFF)
                $filterData[6] = [byte](($Merit -shr 16) -band 0xFF)
                $filterData[7] = [byte](($Merit -shr 24) -band 0xFF)
                
                # Write back FilterData
                Set-ItemProperty -Path $path -Name "FilterData" -Value $filterData -Type Binary
                
                Write-Host "      Merit set: 0x$($Merit.ToString('X8'))" -ForegroundColor Green
                $meritSet = $true
                break
            }
            else {
                Write-Host "      Warning: Invalid FilterData" -ForegroundColor Yellow
            }
        }
        catch {
            Write-Host "      Warning: Failed to process $path : $_" -ForegroundColor Yellow
        }
    }
}

if (-not $meritSet) {
    Write-Host "      Warning: Could not set Merit value." -ForegroundColor Yellow
    Write-Host "      Please configure manually (e.g. using GraphStudioNext)." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Registration Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "フィルター: $FilterPath"
Write-Host "GUID: $FilterGuid"
Write-Host "Merit: 0x$($Merit.ToString('X8'))"
Write-Host ""
