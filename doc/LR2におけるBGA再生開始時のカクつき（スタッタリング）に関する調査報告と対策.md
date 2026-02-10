# LR2におけるBGA再生開始時のカクつき（スタッタリング）に関する調査報告と対策

## 1. 事象

LR2（Lunatic Rave 2）において、特定の動画ファイル（BGA）が再生される瞬間、一瞬ノーツや映像が停止（カクつき）する現象が発生する。
音声トラックが含まれているが、LR2上では音が出ないため実害はないように見えるが、内部処理で遅延を引き起こしている。

**【重要：高解像度ラグとの区別】**
LR2の仕様（DirectX 9 / 32bit）に起因する「HD/FHD等の高解像度動画を再生した際に全体的にフレームレートが落ちる現象」とは**明確に区別される**。

* **高解像度ラグ:** 動画の画素数が多すぎるために発生し、再生中常に重くなる。
* **本事象:** 動画の解像度が低い軽量なファイルであっても、**再生開始の瞬間に**カクつきが発生する点が特徴である。

## 2. 原因：音声ストリームの「飢餓状態（Starvation）」

直接の原因は、**動画ファイルに含まれる「異常な低ビットレートの音声トラック」とDirectShowの仕様との相性問題**である。

* **詳細メカニズム:**
  1. **異常なVBR音声:** 音声トラックが「AAC VBR（可変ビットレート）」でエンコードされており、かつ中身が「無音」または「ほぼ無音」である。
  2. **データ供給不足:** VBRの特性により、無音区間のデータ量が極限まで削減（約2kbpsなど）されている。
  3. **クロックの初期化遅延:** DirectShowの音声レンダラーは、再生開始に必要なバッファが埋まるまで待機する仕様がある。しかし、データがスカスカであるためバッファが埋まらず、リファレンスクロック（基準時計）が動き出さない。
  4. **映像の巻き添え:** 音声レンダラーがマスタークロックとなっているため、時計が動くまで映像側も描画を停止せざるを得ず、カクつきが発生する。

* **判別方法（MediaInfo等）:**
  * 形式: AAC (VBR)
  * ビットレート: 極端に低い（例: 2kbps, 32kbps以下）
  * ストリームサイズ: 極小（数KB〜数十KB）

## 3. 解決策

以下の2つのアプローチがある。
**方法A**は根本解決（ファイル修正）、**方法B**は環境設定による回避策（今回の結論）である。

---

### 方法A：動画ファイルから音声トラックを削除する（推奨・根本解決）

当該BGAファイルから、不要な音声トラックを物理的に削除する。
`ffmpeg`を使用する場合、以下のコマンドで無劣化かつ高速に処理可能。

```bash
ffmpeg -i input.mp4 -c:v copy -an output.mp4
```

* `-c:v copy`: 映像は再エンコードせずそのままコピー（画質劣化なし）。
* `-an`: Audio None（音声を削除）。

---

### 方法B：Windows標準デコーダーを無効化する（環境回避策）

LR2（32bit）が使用する「Microsoft DTV-DVD Audio Decoder」をシステムレベルで無効化し、音声フィルタグラフの構築を失敗させることで、強制的に映像レンダラーをマスタークロックとして動作させる。

#### 手順1：ターゲットとなるデコーダーの特定（32bit環境）

LR2（32bitアプリ）が使用する可能性のある、Windows標準の VBR 対応音声デコーダーは以下の通り。
これらすべてのメリット値を下げることで、標準デコーダーによる VBR 音声処理を完全にブロックできる。

##### 1. Microsoft DTV-DVD Audio Decoder（最重要ターゲット）

* **役割:** AAC, MPEG-2 Audio, MP2 のデコード（今回の主犯格）
* **CLSID:** `{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **対象レジストリ:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **元メリット値** `0x005FFFFF (MERIT_NORMAL - 1)`

##### 2. MPEG Audio Decoder

* **役割:** MP3, MPEG-1 Audio (Layer I, II) のデコード（MP3 VBR対策）
* **CLSID:** `{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **対象レジストリ:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **元メリット値** `0x03680001 (MERIT_PREFERRED + 2 よりも高くかなりの優先度)`
* **注意:** 実体（quartz.dll）はシステム重要ファイルのため、絶対に削除・登録解除しないこと。メリット値変更のみに留める。以下のMPEG Layer-3も同様。

##### 3. MPEG Layer-3

* **役割:** 古い形式や特定の MP3 圧縮形式のデコード（予備、デフォルト値でも使われないと思われる）
* **CLSID:** `{6A08CF80-0E18-11CF-A24D-0020AFD79767}`
* **元メリット値** `MERIT_DO_NOT_USE (0x00200000)`
* **対象レジストリ:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{6A08CF80-0E18-11CF-A24D-0020AFD79767}`

---

#### 手順2：メリット値（優先度）の変更手段を用意

このフィルタのメリット値を`DO_NOT_USE (0x00200000)`以下、または`0`に変更する。

##### 推奨ツールリスト

* [DirectShow Filter Tool (dftool)](https://web.archive.org/web/20241212203500/https://hp.vector.co.jp/authors/VA032094/DFTool.html)
* [GraphStudioNext (32bit版)](https://github.com/cplussharp/graph-studio-next)
* [DirectShow Filter Manager (DSFMgr)](https://www.videohelp.com/software/DirectShow-Filter-Manager)

##### GraphStudioNextの使い方

1. **管理者権限で起動:** `graphstudionext.exe` (32bit版) を右クリックし「管理者として実行」。
2. **フィルタ一覧を表示:** メニューの `Graph` > `Insert Filter...` を選択。
3. **対象を検索:** 上記の名称（例: `Microsoft DTV-DVD Audio Decoder`）で検索。
4. **メリット値変更:** フィルタ名を選択し、右ペインの`Change Merit` ボタンを押す。
5. **値を設定:** `DO_NOT_USE (0x00200000)` 以下、または `0` を入力して適用する。

※ 権限エラーが出る場合は以下の手順でレジストリ権限を操作する。

---

#### 【オプション】手順3: TrustedInstaller権限の処理手順

Windows標準フィルタは強力な保護がかかっているため、以下の順序で操作する必要がある。

1. **レジストリエディタ起動:** 管理者権限で `regedit` を開く。
2. **キーへ移動:** `HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}` を開く。
3. **所有者の変更:**
    * キーを右クリック → [アクセス許可] → [詳細設定]。
    * 所有者を `Administrators` に変更する。
4. **アクセス許可の変更:**
    * 継承元が存在する場合 [継承の無効化] を行い、親キーからの設定を切り離す。
    * `Administrators` に「フルコントロール」を付与する。
5. **メリット値の書き換え:**
    * バイナリ値 `FilterData` を編集し、メリット値を `00 00 00 00 00 00 00 00` 等に変更する。バイナリ構造はメリット値以外のものも含まれているため通常はツールを使用して変更する。
6. **【推奨】権限を元に戻す（保護の復元）:**
    * `Administrators` の権限を「読み取り」のみに戻す。
    * 所有者を `NT Service\TrustedInstaller` に戻す。

#### 結果確認

* GraphStudioNext (32bit) 等で当該ファイルをレンダリングした際、Audio出力ピンがどこにも接続されていない（またはAudioピン自体生成されない）状態になれば成功。
* LR2での再生時、カクつきがなくスムーズに開始されることを確認。

## 4. 副作用と注意点

* **他のアプリへの影響:** 「Microsoft DTV-DVD Audio Decoder」を無効化すると、Windows Media Playerや「映画＆テレビ」アプリ等で、MPEG-2音声やAAC音声が再生できなくなる可能性がある。
* **LAV Filtersとの兼ね合い:** LAV Audio Decoderがインストールされている場合、そちらに接続されてしまうとカクつきが再発する可能性がある。その場合はLAV側でも当該フォーマット（AAC等）を無効化する必要がある。

---

## [2026-02-10] 5. 実装された解決策：LR2 BGA Null Audio Renderer

上記の方法A・Bに加え、本プロジェクトでは**方法C**として、専用のDirectShowフィルターを実装した。

### 方法C：LR2 BGA Null Audio Renderer（推奨・自動解決）

`LR2BGAFilter.ax` に同梱される「**LR2 BGA Null Audio Renderer**」は、音声ストリームを即座に破棄するNull Rendererである。

#### 技術仕様

| 項目           | 詳細                                     |
| -------------- | ---------------------------------------- |
| **フィルタ名** | LR2 BGA Null Audio Renderer              |
| **CLSID**      | `{64878B0F-CC73-484F-9B7B-47520B40C8F0}` |
| **継承元**     | `CBaseRenderer` (DirectShow BaseClasses) |
| **入力**       | `MEDIATYPE_Audio` (全サブタイプ対応)     |
| **Merit**      | `0xfff00000` (最高優先度)                |

#### 動作原理

1. **自動接続**: Merit値が最高優先度のため、BGAファイルに音声トラックが存在すると自動的に接続される。
2. **即時破棄**: `DoRenderSample()` でサンプルを即座に破棄する。
3. **待機なし**: `ShouldDrawSampleNow()` で常に `S_OK` を返し、プレゼンテーション時刻を待たない。

これにより、音声レンダラーがマスタークロックとなっても、データ待機によるクロック遅延が発生しない。

#### メリット

* **環境変更不要**: レジストリ操作やシステムフィルタの無効化が不要。
* **他アプリへの影響なし**: LR2 BGA Filter登録時のみ有効。
* **自動適用**: ユーザー操作なしで問題のあるBGAに自動対応。

#### 使用方法

`LR2BGAFilter.ax` を `regsvr32` で登録するだけで有効になる。

```powershell
regsvr32 LR2BGAFilter.ax
```

GraphStudioNext等で確認すると、Audio出力ピンが「LR2 BGA Null Audio Renderer」に接続されていることが分かる。
