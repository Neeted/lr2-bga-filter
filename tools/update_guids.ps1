# Update-Guids.ps1
# DirectShowフィルタのGUIDをローカルで生成した新しい値に更新するスクリプト
# AI生成コードによるGUID重複リスク（学習元データとの衝突）を回避するために使用します。
#
# 対象ファイル: LR2NullAudioRenderer.h のみ
# (LR2BGAFilter.h は既に更新済みのため対象外)

# GUID生成関数
function New-GuidText {
    param (
        [string]$GuidName
    )
    $g = [Guid]::NewGuid()
    
    # registry format: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    $s = $g.ToString()
    $parts = $s.Split('-')
    
    # Format for DEFINE_GUID macro
    $p1 = "0x" + $parts[0]
    $p2 = "0x" + $parts[1]
    $p3 = "0x" + $parts[2]
    
    $b = $parts[3] + $parts[4]
    $bArr = @()
    for ($i = 0; $i -lt $b.Length; $i += 2) {
        $bArr += "0x" + $b.Substring($i, 2)
    }
    
    $line1 = "DEFINE_GUID($GuidName, $p1, $p2, $p3, $($bArr[0]), $($bArr[1]), $($bArr[2]),"
    $line2 = "            $($bArr[3]), $($bArr[4]), $($bArr[5]), $($bArr[6]), $($bArr[7]));"
    
    return "$line1`r`n$line2"
}

# --- LR2NullAudioRenderer.h のGUID更新 ---
# $AudioRendererFile = Join-Path $PSScriptRoot "..\src\LR2NullAudioRenderer.h"
# if (-not (Test-Path $AudioRendererFile)) {
#     Write-Error "Audio Renderer file not found: $AudioRendererFile"
#     exit 1
# }

# Write-Host "Updating GUIDs in $AudioRendererFile..."
# $AudioContent = Get-Content $AudioRendererFile -Raw -Encoding UTF8

# $AudioGuidNames = @("CLSID_LR2NullAudioRenderer")
# foreach ($Name in $AudioGuidNames) {
#     Write-Host "  Generating new GUID for $Name..."
#     $NewDef = New-GuidText -GuidName $Name
#     $Pattern = "DEFINE_GUID\($Name,[\s\S]*?\);"
    
#     if ($AudioContent -match $Pattern) {
#         $AudioContent = $AudioContent -replace $Pattern, $NewDef
#     }
#     else {
#         Write-Warning "Definition for $Name not found!"
#     }
# }

# Set-Content $AudioRendererFile -Value $AudioContent -Encoding utf8bom
# Write-Host "GUIDs updated successfully."
