# DirectShow Preferred Filter 登録スクリプト (RGB24用)
# 管理者権限で実行してください

$ErrorActionPreference = "Stop"

# 設定内容
$RegPath = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\DirectShow\Preferred"
$RGB24_GUID = "{e436eb7d-524f-11ce-9f53-0020af0ba770}"
$Target_CLSID = "{61E38596-D44C-4097-89AF-AABBA85DAA6D}"

Write-Host "DirectShow Preferred Filter 登録を開始します..."
Write-Host "Target: 32-bit (WOW6432Node)"
Write-Host "Media : RGB24 $RGB24_GUID"
Write-Host "Filter: $Target_CLSID"

if (-not (Test-Path $RegPath)) {
    Write-Error "レジストリキーが見つかりません: $RegPath"
    exit 1
}

# 既存の値を確認
$Current = Get-ItemProperty -Path $RegPath -Name $RGB24_GUID -ErrorAction SilentlyContinue
if ($Current) {
    Write-Warning "既存の設定が見つかりました: $($Current.$RGB24_GUID)"
    Write-Host "上書きしますか？ (Y/N)"
    $Confirm = Read-Host
    if ($Confirm -ne 'Y') {
        Write-Host "キャンセルしました。"
        exit
    }
}

try {
    New-ItemProperty -Path $RegPath -Name $RGB24_GUID -Value $Target_CLSID -PropertyType String -Force | Out-Null
    Write-Host "登録が完了しました。" -ForegroundColor Green
    
    # 確認
    Write-Host "現在の設定:"
    Get-ItemProperty -Path $RegPath -Name $RGB24_GUID | Select-Object -Property $RGB24_GUID
}
catch {
    Write-Error "登録に失敗しました。管理者権限で実行しているか確認してください。"
    Write-Error $_
}
