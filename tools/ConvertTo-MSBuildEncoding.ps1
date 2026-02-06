$ErrorActionPreference = "Stop"

$Root = Resolve-Path "$PSScriptRoot\.."
$Src = Join-Path $Root "src"

$Extensions = @("*.cpp", "*.h", "*.hpp", "*.c", "*.rc", "*.def")

Write-Host "Target directory: $Src"

# Check if src exists
if (-not (Test-Path $Src)) {
    Write-Error "Source directory not found: $Src"
}

$Files = Get-ChildItem -Path $Src -Recurse -Include $Extensions

if ($Files.Count -eq 0) {
    Write-Warning "No source files found in $Src"
    exit
}

$UTF8WithBOM = New-Object System.Text.UTF8Encoding($true)

foreach ($File in $Files) {
    Write-Host "Processing $($File.Name)..." -NoNewline

    try {
        $Content = [System.IO.File]::ReadAllText($File.FullName)
        
        # Normalize to CRLF
        # 1. Normalize line endings to LF
        # 2. Replace LF with CRLF
        $NewContent = $Content.Replace("`r`n", "`n").Replace("`r", "`n").Replace("`n", "`r`n")
        
        # Write back with UTF-8 BOM
        [System.IO.File]::WriteAllText($File.FullName, $NewContent, $UTF8WithBOM)
        Write-Host " Done."
    }
    catch {
        Write-Host " Failed: $_" -ForegroundColor Red
    }
}

Write-Host "Conversion complete."
