<#
.SYNOPSIS
    Windowsレジストリに設定されているDirectShowの「優先フィルター (Preferred Filters)」を一覧表示します。
.DESCRIPTION
    システムが最も優先的に使用するDirectShowフィルター（CodecTweakToolなどで管理されるもの）を格納するレジストリキーを読み取ります。
    メディアサブタイプのGUIDを人間が読める名前やFourCCに解決し、フィルターのCLSIDを登録名に解決して表示します。
    32ビット (WOW6432Node) と 64ビット の両方のレジストリを確認します。
.EXAMPLE
    .\Get-DirectShowPreferredFilters.ps1 | Out-GridView
    スクリプトを実行し、結果をグリッドビューで表示します。
.EXAMPLE
    .\Get-DirectShowPreferredFilters.ps1 | Format-Table -AutoSize
    テーブル形式で見やすく整形。
.OUTPUTS
    [PSCustomObject[]]
    
    以下のプロパティを持つカスタムオブジェクトの配列を返します:
    - Architecture         (String): "32-bit" または "64-bit"
    - MediaSubtypeName     (String): フォーマット名
    - FourCC               (String): FourCCコード (存在する場合)
    - PreferredFilterName  (String): 優先フィルター名
    - Status               (String): 状態 (Forced Filter / Merit Fallback)
    - MediaSubtypeGUID     (String): サブタイプのGUID
    - PreferredFilterCLSID (String): フィルターのCLSID
#>

# =============================================================================
# Windows Media Subtype GUID Dictionary (参考:https://gix.github.io/media-types/)
# =============================================================================
$MediaSubtypes = @{
    # -------------------------------------------------------------------------
    # Major Types (メジャータイプ)
    # -------------------------------------------------------------------------
    "{73646976-0000-0010-8000-00AA00389B71}" = "MFMediaType_Video ('vids')"
    "{73647561-0000-0010-8000-00AA00389B71}" = "MFMediaType_Audio ('auds')"
    "{73627573-0000-0010-8000-00AA00389B71}" = "MFMediaType_Subtitle ('sbus')"
    "{E436EB83-524F-11CE-9F53-0020AF0BA770}" = "MFMediaType_Stream"
    "{72178C23-E45B-11D5-BC2A-00B0D0F3F4AB}" = "MFMediaType_Image"
    "{73747874-0000-0010-8000-00AA00389B71}" = "MFMediaType_Text ('txts')"

    # -------------------------------------------------------------------------
    # Uncompressed Video: RGB Formats
    # -------------------------------------------------------------------------
    # DirectShow (Legacy)
    "{E436EB78-524F-11CE-9F53-0020AF0BA770}" = "RGB1 (Palette)"
    "{E436EB79-524F-11CE-9F53-0020AF0BA770}" = "RGB4 (Palette)"
    "{E436EB7A-524F-11CE-9F53-0020AF0BA770}" = "RGB8 (Palette)"
    "{E436EB7B-524F-11CE-9F53-0020AF0BA770}" = "RGB565 (16-bit)"
    "{E436EB7C-524F-11CE-9F53-0020AF0BA770}" = "RGB555 (15-bit)"
    "{E436EB7D-524F-11CE-9F53-0020AF0BA770}" = "RGB24 (BGR)"
    "{E436EB7E-524F-11CE-9F53-0020AF0BA770}" = "RGB32 (BGRX)"
    "{773C9AC0-3274-11D0-B724-00AA006C1A01}" = "ARGB32 (BGRA)"
    
    # Media Foundation (D3DFORMAT Based)
    "{00000014-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_RGB24 (D3DFMT_R8G8B8)"
    "{00000015-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_ARGB32 (D3DFMT_A8R8G8B8)"
    "{00000016-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_RGB32 (D3DFMT_X8R8G8B8)"
    "{00000017-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_RGB565 (D3DFMT_R5G6B5)"
    "{00000018-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_RGB555 (D3DFMT_X1R5G5B5)"
    "{0000002A-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_A2R10G10B10 (10-bit RGB)"
    "{0000006F-0000-0010-8000-00AA00389B71}" = "MFVideoFormat_A16B16G16R16F (Float RGB)"

    # -------------------------------------------------------------------------
    # Uncompressed Video: YUV Formats (8-bit)
    # -------------------------------------------------------------------------
    # Planar
    "{3231564E-0000-0010-8000-00AA00389B71}" = "NV12 (4:2:0 Bi-Planar Y-UV)"
    "{3132564E-0000-0010-8000-00AA00389B71}" = "NV21 (4:2:0 Bi-Planar Y-VU)"
    "{32315659-0000-0010-8000-00AA00389B71}" = "YV12 (4:2:0 Planar Y-V-U)"
    "{30323449-0000-0010-8000-00AA00389B71}" = "I420 (4:2:0 Planar Y-U-V)"
    "{56555949-0000-0010-8000-00AA00389B71}" = "IYUV (Same as I420)"
    "{31434D49-0000-0010-8000-00AA00389B71}" = "IMC1"
    "{32434D49-0000-0010-8000-00AA00389B71}" = "IMC2"
    "{33434D49-0000-0010-8000-00AA00389B71}" = "IMC3"
    "{34434D49-0000-0010-8000-00AA00389B71}" = "IMC4"
    
    # Packed
    "{32595559-0000-0010-8000-00AA00389B71}" = "YUY2 (4:2:2 Packed YUYV)"
    "{59565955-0000-0010-8000-00AA00389B71}" = "UYVY (4:2:2 Packed UYVY)"
    "{56555941-0000-0010-8000-00AA00389B71}" = "AYUV (4:4:4 Packed with Alpha)"
    "{59565956-0000-0010-8000-00AA00389B71}" = "VYUY"
    "{55595659-0000-0010-8000-00AA00389B71}" = "YVYU"
    "{34344941-0000-0010-8000-00AA00389B71}" = "AI44 (Palettized)"

    # -------------------------------------------------------------------------
    # Uncompressed Video: High Bit-Depth YUV (10-bit, 16-bit)
    # -------------------------------------------------------------------------
    "{30313050-0000-0010-8000-00AA00389B71}" = "P010 (10-bit 4:2:0 Bi-Planar)"
    "{36313050-0000-0010-8000-00AA00389B71}" = "P016 (16-bit 4:2:0 Bi-Planar)"
    "{30313250-0000-0010-8000-00AA00389B71}" = "P210 (10-bit 4:2:2 Bi-Planar)"
    "{36313250-0000-0010-8000-00AA00389B71}" = "P216 (16-bit 4:2:2 Bi-Planar)"
    "{30313279-0000-0010-8000-00AA00389B71}" = "Y210 (10-bit 4:2:2 Packed)"
    "{36313279-0000-0010-8000-00AA00389B71}" = "Y216 (16-bit 4:2:2 Packed)"
    "{30313276-0000-0010-8000-00AA00389B71}" = "v210 (10-bit 4:2:2 Packed)"
    "{30313456-0000-0010-8000-00AA00389B71}" = "v410 (10-bit 4:4:4 Packed)"
    "{30313459-0000-0010-8000-00AA00389B71}" = "Y410 (10-bit 4:4:4 Packed)"
    "{36313459-0000-0010-8000-00AA00389B71}" = "Y416 (16-bit 4:4:4 Packed)"

    # -------------------------------------------------------------------------
    # Compressed Video: H.264 / AVC
    # -------------------------------------------------------------------------
    "{34363248-0000-0010-8000-00AA00389B71}" = "H264 (Standard)"
    "{34363268-0000-0010-8000-00AA00389B71}" = "h264 (lowercase)"
    "{34363258-0000-0010-8000-00AA00389B71}" = "X264"
    "{34363278-0000-0010-8000-00AA00389B71}" = "x264"
    "{31435641-0000-0010-8000-00AA00389B71}" = "AVC1 (No Start Codes, MP4)"
    "{31637661-0000-0010-8000-00AA00389B71}" = "avc1 (lowercase)"
    "{3F40F4F0-5622-4FF8-B6D8-A17A584BEE5E}" = "MFVideoFormat_H264_ES"

    # -------------------------------------------------------------------------
    # Compressed Video: Other
    # -------------------------------------------------------------------------
    "{58564944-0000-0010-8000-00AA00389B71}" = "DivX 4 (OpenDivX) (Project Mayo)"
    "{30355844-0000-0010-8000-00AA00389B71}" = "DivX 5"
    "{30357864-0000-0010-8000-00AA00389B71}" = "DivX 5"
    "{35363248-0000-0010-8000-00AA00389B71}" = "HEVC / H.265 Video"
    "{78766964-0000-0010-8000-00AA00389B71}" = "DivX"
    "{20637664-0000-0010-8000-00AA00389B71}" = "DVC/DV Video"
    "{64687664-0000-0010-8000-00AA00389B71}" = "HD-DVCR (1125-60 or 1250-50)"
    "{64737664-0000-0010-8000-00AA00389B71}" = "SDL-DVCR (525-60 or 625-50)"
    "{6c737664-0000-0010-8000-00AA00389B71}" = "SD-DVCR (525-60 or 625-50)"
    "{44495658-0000-0010-8000-00AA00389B71}" = "XviD"
    "{64697678-0000-0010-8000-00AA00389B71}" = "XviD"
    
    # -------------------------------------------------------------------------
    # Compressed Video: H.265 / HEVC
    # -------------------------------------------------------------------------
    "{43564548-0000-0010-8000-00AA00389B71}" = "HEVC (H.265)"
    "{53564548-0000-0010-8000-00AA00389B71}" = "HEVS (HEVC ES)"
    "{31435648-0000-0010-8000-00AA00389B71}" = "HVC1 (HEVC in MP4)"
    "{30314D48-0000-0010-8000-00AA00389B71}" = "HM10"

    # -------------------------------------------------------------------------
    # Compressed Video: Apple ProRes
    # -------------------------------------------------------------------------
    "{6E637061-0000-0010-8000-00AA00389B71}" = "ProRes 422 (apcn)"
    "{68637061-0000-0010-8000-00AA00389B71}" = "ProRes 422 HQ (apch)"
    "{73637061-0000-0010-8000-00AA00389B71}" = "ProRes 422 LT (apcs)"
    "{6F637061-0000-0010-8000-00AA00389B71}" = "ProRes 422 Proxy (apco)"
    "{68347061-0000-0010-8000-00AA00389B71}" = "ProRes 4444 (ap4h)"
    
    # -------------------------------------------------------------------------
    # Compressed Video: Other Standards (VPx, AV1, WMV, MPEG)
    # -------------------------------------------------------------------------
    "{30395056-0000-0010-8000-00AA00389B71}" = "VP90 (Google VP9)"
    "{30385056-0000-0010-8000-00AA00389B71}" = "VP80 (Google VP8)"
    "{31305641-0000-0010-8000-00AA00389B71}" = "AV01 (AOM AV1)"
    "{33564D57-0000-0010-8000-00AA00389B71}" = "WMV3 (Windows Media Video 9)"
    "{32564D57-0000-0010-8000-00AA00389B71}" = "WMV2 (Windows Media Video 8)"
    "{31564D57-0000-0010-8000-00AA00389B71}" = "WMV1 (Windows Media Video 7)"
    "{3153534D-0000-0010-8000-00AA00389B71}" = "Windows Media Video 7 Screen"
    "{41564D57-0000-0010-8000-00AA00389B71}" = "Windows Media Video 9, Advanced Profile (non-VC-1-compliant)"
    "{50564D57-0000-0010-8000-00AA00389B71}" = "Windows Media Video 9.1 Image"
    "{52564D57-0000-0010-8000-00AA00389B71}" = "Windows Media Video 9 Image v2 (WMVR)"
    "{32505657-0000-0010-8000-00AA00389B71}" = "Windows Media Video 9 Image v2 (WVP2)"
    "{31435657-0000-0010-8000-00AA00389B71}" = "WVC1 (VC-1)"
    "{3253534D-0000-0010-8000-00AA00389B71}" = "MSS2 (WMV9 Screen)"
    "{47504A4D-0000-0010-8000-00AA00389B71}" = "MJPG (Motion JPEG)"
    "{E06D8026-DB46-11CF-B4D1-00805F6CBBEA}" = "MPEG2_VIDEO"
    "{E436EB81-524F-11CE-9F53-0020AF0BA770}" = "MPEG1_Payload"
    "{3253344D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Advanced Simple Profile"
    "{3273346D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Advanced Simple Profile"
    "{3234504D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 2"
    "{3234706D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 2"
    "{3334504D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 3"
    "{3334706D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 3"
    "{5334504D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Simple Profile"
    "{7334706D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Simple Profile"
    "{5634504D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Part 2"
    "{7634706D-0000-0010-8000-00AA00389B71}" = "MPEG-4 Part 2"
    "{3447504D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 1"
    "{3467706D-0000-0010-8000-00AA00389B71}" = "Microsoft MPEG-4 version 1"

    # -------------------------------------------------------------------------
    # Audio Formats
    # -------------------------------------------------------------------------
    # Uncompressed
    "{00000001-0000-0010-8000-00AA00389B71}" = "PCM (Integer)"
    "{00000003-0000-0010-8000-00AA00389B71}" = "IEEE_FLOAT"
    "{00000006-0000-0010-8000-00AA00389B71}" = "ALAW"
    "{00000007-0000-0010-8000-00AA00389B71}" = "MULAW"
    "{000000FE-0000-0010-8000-00AA00389B71}" = "WAVE_FORMAT_EXTENSIBLE"

    # AAC / MPEG Audio
    "{00001610-0000-0010-8000-00AA00389B71}" = "AAC (MPEG_HEAAC / MF Standard)"
    "{00001600-0000-0010-8000-00AA00389B71}" = "AAC_ADTS"
    "{000000FF-0000-0010-8000-00AA00389B71}" = "AAC_RAW (WAVE_FORMAT_RAW_AAC1)"
    "{00000055-0000-0010-8000-00AA00389B71}" = "MP3 (MPEG Layer 3)"
    "{00000050-0000-0010-8000-00AA00389B71}" = "MPEG Layer 1/2"
    "{e436eb80-524f-11ce-9f53-0020af0ba770}" = "MPEG-1 audio packet"	
    "{e06d802b-db46-11cf-b4d1-00805f6cbbea}" = "MPEG-1/MPEG-2 Audio Layer II"	
    "{00001602-0000-0010-8000-00aa00389b71}" = "MPEG-4 audio transport stream with a synchronization layer (LOAS) and a multiplex layer (LATM)"	

    # Dolby / DTS
    "{E06D802C-DB46-11CF-B4D1-00805F6CBBEA}" = "Dolby Digital (AC3)"
    "{A7FB87AF-2D02-42FB-A4D4-05CD93843BDD}" = "Dolby Digital Plus (EAC3)"
    "{EB27CEC4-163E-4CA3-8B74-8E25F91B517E}" = "Dolby TrueHD"
    "{00000092-0000-0010-8000-00AA00389B71}" = "Dolby AC3 SPDIF"
    "{00000008-0000-0010-8000-00AA00389B71}" = "DTS"
    "{00002001-0000-0010-8000-00AA00389B71}" = "DTS2"

    # Windows Media Audio
    "{00000161-0000-0010-8000-00AA00389B71}" = "WMAudioV8 (WMA2)"
    "{00000162-0000-0010-8000-00AA00389B71}" = "WMAudioV9 (WMA3 / Pro)"
    "{00000163-0000-0010-8000-00AA00389B71}" = "WMAudio_Lossless"
    "{0000000A-0000-0010-8000-00AA00389B71}" = "WMAudio_Voice"
    "{00000160-0000-0010-8000-00aa00389b71}" = "Windows Media Audio 1"	

    # Modern Open Codecs
    "{0000F1AC-0000-0010-8000-00AA00389B71}" = "FLAC"
    "{00006C61-0000-0010-8000-00AA00389B71}" = "ALAC (Apple Lossless)"
    "{0000704F-0000-0010-8000-00AA00389B71}" = "Opus"
    "{00005756-0000-0010-8000-00AA00389B71}" = "WavPack"
    
    # Other
    "{e06d8032-db46-11cf-b4d1-00805f6cbbea}" = "DVD audio data"	
    "{0000000B-0000-0010-8000-00AA00389B71}" = "Windows Media Audio Voice 10" # mmreg.h確認

    # -------------------------------------------------------------------------
    # Subtitles & Others
    # -------------------------------------------------------------------------
    "{C886D215-F485-40BB-8DB6-FADBC619A45D}" = "WebVTT"
    "{73E73992-9A10-4356-9557-7194E91E3E54}" = "TTML"
    "{F7239E31-9599-4E43-8DD5-FBAF75CF37F1}" = "VobSub"
    "{AFB6C280-2C84-11D5-8E92-005500A03C34}" = "MPEG2_Data"
}

# GUIDからメディアサブタイプ情報（名前とFourCC）を取得する関数
function Get-MediaSubtypeInfo {
    param (
        [string]$Guid
    )
    
    $Guid = $Guid.ToUpper()
    $Result = [PSCustomObject]@{
        Name   = "Unknown" # デフォルト
        FourCC = $null
    }

    # ---------------------------------------------------------
    # 1. FourCC / FormatTag の解析 (共通処理)
    # ---------------------------------------------------------
    # 標準的なDirectShow GUIDパターン: {XXXXXXXX-0000-0010-8000-00AA00389B71}
    # 先頭の8文字(DWORD)がFourCCまたはFormatTagを表す
    
    $FourCCStr = $null
    $IsAudio = $false
    $IsVideo = $false

    if ($Guid -match "^\{?([0-9A-F]{8})-0000-0010-8000-00AA00389B71\}?$") {
        try {
            $hexStr = $Matches[1]
            $val = [Convert]::ToUInt32($hexStr, 16)

            # ケースA: 値が小さい場合はオーディオFormatTagの可能性が高い (0xFFFF以下)
            if ($val -le 0xFFFF) {
                $IsAudio = $true
                # FormatTagとして記録しておきたい場合はここで処理
            }
            # ケースB: 値が大きい場合はFourCCの可能性が高い
            else {
                # リトルエンディアンとして解釈して文字に戻す
                $chars = @(
                    [char]($val -band 0xFF),
                    [char](($val -shr 8) -band 0xFF),
                    [char](($val -shr 16) -band 0xFF),
                    [char](($val -shr 24) -band 0xFF)
                )
                $decoded = -join $chars
                
                # 英数字・スペースのみで構成されている場合のみFourCCとみなす
                if ($decoded -match "^[\w\d\s]{4}$") {
                    $FourCCStr = $decoded
                    $Result.FourCC = $FourCCStr
                    $IsVideo = $true
                }
            }
        }
        catch {
            # 変換エラー時は無視
        }
    }

    # ---------------------------------------------------------
    # 2. 名前の決定
    # ---------------------------------------------------------
    
    # (A) 辞書にある場合
    if ($MediaSubtypes.ContainsKey($Guid)) {
        $Result.Name = $MediaSubtypes[$Guid]
    }
    # (B) 辞書になく、Videoと推定される場合 (FourCCあり)
    elseif ($IsVideo) {
        $Result.Name = "Unknown Video"
    }
    # (C) 辞書になく、Audioと推定される場合
    elseif ($IsAudio) {
        $Result.Name = "Unknown Audio"
    }
    # (D) それ以外
    else {
        $Result.Name = "Unknown GUID"
    }

    return $Result
}

# CLSIDからフィルター名を取得する関数
function Get-FilterName {
    param (
        [string]$Clsid
    )

    if ([string]::IsNullOrWhiteSpace($Clsid)) { return "Unknown" }
    
    # CodecTweakToolなどが無効化のために使うダミーGUID
    if ($Clsid -eq '{ABCD1234-0000-0000-0000-000000000000}') {
        return "USE MERIT (Dummy GUID)"
    }

    # 検索するレジストリパスのリスト (64bitネイティブと32bit WOW6432Node)
    $SearchPaths = @(
        "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid",
        "Registry::HKEY_CLASSES_ROOT\WOW6432Node\CLSID\$Clsid"
    )

    foreach ($Path in $SearchPaths) {
        if (Test-Path $Path) {
            $Val = (Get-ItemProperty -Path $Path -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
            if ($Val) { return $Val }
        }
    }
    
    return $Clsid
}

function Get-PreferredRegistryKeys {
    param (
        [string]$RegistryPath,
        [string]$Architecture
    )

    if (-not (Test-Path $RegistryPath)) {
        Write-Verbose "パスが見つかりません: $RegistryPath"
        return
    }

    $Key = Get-Item -Path $RegistryPath
    $Properties = $Key.Property

    foreach ($PropName in $Properties) {
        $SubtypeGuid = $PropName
        $FilterClsid = $Key.GetValue($PropName)
        
        # フィルター名とサブタイプ情報の取得
        $FilterName = Get-FilterName -Clsid $FilterClsid
        $SubtypeInfo = Get-MediaSubtypeInfo -Guid $SubtypeGuid

        # ステータスの判定
        $Status = "Forced Filter"
        if ($FilterName -like "USE MERIT*") {
            $Status = "Merit Fallback"
        }

        [PSCustomObject]@{
            Architecture         = $Architecture
            MediaSubtypeName     = $SubtypeInfo.Name
            FourCC               = $SubtypeInfo.FourCC
            PreferredFilterName  = $FilterName
            Status               = $Status
            MediaSubtypeGUID     = $SubtypeGuid
            PreferredFilterCLSID = $FilterClsid
        }
    }
}

$Items = @()

# 32-bit (64bit OS上の WOW6432Node)
$Items += Get-PreferredRegistryKeys -RegistryPath "HKLM:\SOFTWARE\WOW6432Node\Microsoft\DirectShow\Preferred" -Architecture "32-bit"

# 64-bit (ネイティブ)
$Items += Get-PreferredRegistryKeys -RegistryPath "HKLM:\SOFTWARE\Microsoft\DirectShow\Preferred" -Architecture "64-bit"

# 結果の出力
$Items