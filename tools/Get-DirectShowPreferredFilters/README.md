# Get-DirectShowPreferredFilters

このプロジェクトは、WindowsのDirectShow設定（Preferred Filters）を調査・変更するためのスクリプト集です。
特に、既存のアプリケーション（LR2など）のDirectShowグラフに独自のフィルターを挿入するための検証を目的としています。

## ツール一覧

### 1. `Get-DirectShowPreferredFilters.ps1`

### **DirectShow Preferred Filter 一覧表示スクリプト**

Windowsレジストリ (`HKLM\...\DirectShow\Preferred`) に登録されている優先フィルター設定を一覧表示します。
CodecTweakToolなどで設定された内容を確認する際に使用します。

* **機能**:
  * 32bit / 64bit 両方のレジストリを確認
  * Media Subtype GUID (FourCC) を可読な形式（H264, XVID, RGB24等）にデコードして表示
  * フィルターのCLSIDを実際の名称に解決
  * 「USE MERIT」（ダミーGUIDによる無効化）の状態を検出可能

* **使用法**:

    ```powershell
    ./Get-DirectShowPreferredFilters.ps1 | Out-GridView
    ```

### 2. `register_rgb24_filter.ps1`

### **RGB24 Preferred Filter 登録スクリプト**

特定のMedia Subtype（ここではRGB24）に対して、指定したフィルター（CLSID）を優先フィルターとして登録します。
DirectShowの `RenderFile` 等による自動グラフ構築時に、このフィルターが優先的に選択されるようになります。

* **注意**: 実行には管理者権限が必要です。また、レジストリキーのアクセス許可設定（TrustedInstallerからの所有権変更など）が必要な場合があります。

---

## 知見: LR2 (DirectShow) への独自フィルター挿入について

### 課題

LR2（Lunatic Rave 2）のBGA再生において、LAV Video Decoderとレンダラー（MovieRenderer）の間に独自のリサイズフィルターを挿入したいという要件がありました。

### 検証結果

Preferred Filter設定による強制挿入（`register_rgb24_filter.ps1` の使用）を試みましたが、**成功しませんでした**。

* **理由**: DirectShowのグラフ構築ロジック（Intelligent Connect）は、まず「上流（LAV）と下流（レンダラー）が直接接続できるか」を試みます。LAVがRGB24を出力し、レンダラーがRGB24を受け入れる場合、**中間フィルターの検索プロセスがスキップされ、直接接続が成立してしまう**ためです。

### 解決策

**「LAV Video Decoderの出力フォーマットを制限する」** ことが最も現実的で確実な解決策となります。

1. **LAVの設定変更**: LAV Video Decoderの設定で、出力フォーマットの **RGB24チェックを外す**（RGB32のみ許可するなど）。
2. **結果**: LAV（RGB32出力）とレンダラー（RGB24入力）が直接接続できなくなるため、DirectShowは中間フィルターを探し始めます。
3. **独自フィルターの挿入**: 独自フィルターが `Input: RGB32 -> Output: RGB24` の変換能力を持っていれば、DirectShowによって自動的にグラフに組み込まれます。

```text
[Video File] -> [LAV Video Decoder (RGB32)] -> [独自リサイズフィルター (RGB32->RGB24)] -> [LR2 MovieRenderer (RGB24)]
```

この構成により、確実に独自フィルターを経由させることが可能です。
