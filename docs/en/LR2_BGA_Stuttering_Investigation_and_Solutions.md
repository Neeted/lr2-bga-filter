# Investigation Report and Solutions regarding Stuttering at BGA Playback Start in LR2

## 1. Phenomenon

In LR2 (Lunatic Rave 2), a phenomenon occurs where notes/video momentarily freeze (stutter) at the moment a specific video file (BGA) starts playing.
Although there is an audio track, no sound is output on LR2, so it seems harmless, but it causes delay in internal processing.

**[Important: Distinction from High-Resolution Lag]**
This must be **clearly distinguished** from "phenomenon where frame rate drops overall when playing high-res videos (HD/FHD etc.)" caused by LR2 specs (DirectX 9 / 32bit).

* **High-Resolution Lag**: Caused by too many pixels, always heavy during playback.
* **This Phenomenon**: Occurs even with low-res lightweight files, characterized by stutter **at the moment of playback start**.

## 2. Cause: Audio Stream "Starvation"

The direct cause is **compatibility issue between "abnormal low bitrate audio track" in video file and DirectShow specifications**.

* **Detailed Mechanism:**
  1. **Abnormal VBR Audio**: Audio track is encoded in "AAC VBR (Variable Bitrate)" and content is "silent" or "almost silent".
  2. **Data Supply Shortage**: Due to VBR characteristics, data amount in silent sections is extremely reduced (e.g. approx 2kbps).
  3. **Clock Initialization Delay**: DirectShow audio renderer waits until buffer required for playback start is filled. However, because data is sparse, buffer doesn't fill and Reference Clock doesn't start.
  4. **Video Collateral Damage**: Since audio renderer becomes the Master Clock, video side is forced to stop drawing until the clock moves, causing stutter.

* **Identification Method (MediaInfo etc.):**
  * Format: AAC (VBR)
  * Bitrate: Extremely low (e.g. 2kbps, under 32kbps)
  * Stream size: Tiny (few KB to tens of KB)

## 3. Solutions

There are two approaches.
**Method A** is root solution (file modification), **Method B** is workaround by environment settings (conclusion this time).

---

### Method A: Delete Audio Track from Video File (Recommended / Root Solution)

Physically delete unnecessary audio track from BGA file.
Using `ffmpeg`, it can be processed without re-encoding and fast with the following command:

```bash
ffmpeg -i input.mp4 -c:v copy -an output.mp4
```

* `-c:v copy`: Copy video stream without re-encoding (no quality loss).
* `-an`: Audio None (delete audio).

---

### Method B: Disable Windows Standard Decoder (Environment Workaround)

System-level disable "Microsoft DTV-DVD Audio Decoder" used by LR2 (32bit), forcing failure of audio filter graph construction, thereby forcing video renderer to operate as Master Clock.

#### Step 1: Identify Target Decoder (32bit Environment)

Windows standard VBR compatible audio decoders likely used by LR2 (32bit app) are following.
Decreasing Merit Value for all of these can completely block VBR audio processing by standard decoders.

##### 1. Microsoft DTV-DVD Audio Decoder (Primary Target)

* **Role**: Decode AAC, MPEG-2 Audio, MP2 (Main culprit this time)
* **CLSID**: `{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **Target Registry**: `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **Original Merit**: `0x005FFFFF (MERIT_NORMAL - 1)`

##### 2. MPEG Audio Decoder

* **Role**: Decode MP3, MPEG-1 Audio (Layer I, II) (MP3 VBR countermeasure)
* **CLSID**: `{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **Target Registry**: `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **Original Merit**: `0x03680001 (Higher than MERIT_PREFERRED + 2, quite high)`
* **Note**: Entity (quartz.dll) is system critical file, so NEVER delete/unregister. Only change Merit Value. Same for MPEG Layer-3 below.

##### 3. MPEG Layer-3

* **Role**: Decode old formats or specific MP3 compression formats (Backup, likely not used by default)
* **CLSID**: `{6A08CF80-0E18-11CF-A24D-0020AFD79767}`
* **Original Merit**: `MERIT_DO_NOT_USE (0x00200000)`
* **Target Registry**: `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{6A08CF80-0E18-11CF-A24D-0020AFD79767}`

---

#### Step 2: Prepare Means to Change Merit Value (Priority)

Change Merit Value of these filters to `DO_NOT_USE (0x00200000)` or lower, or `0`.

##### Recommended Tool List

* [DirectShow Filter Tool (dftool)](https://web.archive.org/web/20241212203500/https://hp.vector.co.jp/authors/VA032094/DFTool.html)
* [GraphStudioNext (32bit)](https://github.com/cplussharp/graph-studio-next)
* [DirectShow Filter Manager (DSFMgr)](https://www.videohelp.com/software/DirectShow-Filter-Manager)

##### How to use GraphStudioNext

1. **Run as Admin**: Right click `graphstudionext.exe` (32bit) and "Run as Administrator".
2. **Show Filter List**: Menu `Graph` > `Insert Filter...`.
3. **Search Target**: Search by name (e.g. `Microsoft DTV-DVD Audio Decoder`).
4. **Change Merit**: Select filter name and click `Change Merit` button on right pane.
5. **Set Value**: Input `DO_NOT_USE (0x00200000)` or lower, or `0`, and apply.

* Note: If permission error occurs, manipulate registry permissions with following steps.

---

#### [Optional] Step 3: TrustedInstaller Permission Handling Procedure

Windows standard filters are strongly protected, so operation in following order is required.

1. **Open Registry Editor**: Open `regedit` with Admin rights.
2. **Go to Key**: Open `HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`.
3. **Change Owner**:
    * Right click key -> [Permissions] -> [Advanced].
    * Change owner to `Administrators`.
4. **Change Permissions**:
    * If inheritance exists, [Disable Inheritance] and disconnect settings from parent key.
    * Grant "Full Control" to `Administrators`.
5. **Rewrite Merit Value**:
    * Edit binary value `FilterData`, change merit value part to `00 00 00 00 00 00 00 00` etc. Binary structure contains things other than merit value so usually use tool to change.
6. **[Recommended] Restore Permissions (Restore Protection)**:
    * Revert `Administrators` permission to "Read" only.
    * Revert owner to `NT Service\TrustedInstaller`.

#### Result Confirmation

* When rendering file with GraphStudioNext (32bit) etc., successful if Audio output pin is not connected anywhere (or Audio pin itself is not generated).
* Confirm playback starts smoothly without stutter in LR2.

## 4. Side Effects and Notes

* **Impact on other apps**: Disabling "Microsoft DTV-DVD Audio Decoder" might disable playback of MPEG-2 Audio or AAC Audio in Windows Media Player or "Movies & TV" app.
* **Relation with LAV Filters**: If LAV Audio Decoder is installed, if connected there, stutter might recur. In that case, need to disable format (AAC etc.) in LAV side too.

---

## [2026-02-10] 5. Implemented Solution: LR2 BGA Null Audio Renderer

In addition to Method A/B, this project implemented **Method C** as a dedicated DirectShow filter.

### Method C: LR2 BGA Null Audio Renderer (Recommended / Auto Solution)

"**LR2 BGA Null Audio Renderer**" bundled with `LR2BGAFilter.ax` is a Null Renderer that immediately discards audio stream.

#### Technical Specifications

| Item            | Details                                  |
| :-------------- | :--------------------------------------- |
| **Filter Name** | LR2 BGA Null Audio Renderer              |
| **CLSID**       | `{64878B0F-CC73-484F-9B7B-47520B40C8F0}` |
| **Inherits**    | `CBaseRenderer` (DirectShow BaseClasses) |
| **Input**       | `MEDIATYPE_Audio` (All subtypes)         |
| **Merit**       | `0xfff00000` (Highest Priority)          |

#### Operating Principle

1. **Auto Connection**: Because Merit Value is Highest Priority, it automatically connects if audio track exists in BGA file.
2. **Immediate Discard**: Immediately discard sample with `DoRenderSample()`.
3. **No Wait**: Always return `S_OK` with `ShouldDrawSampleNow()`, not waiting for presentation time.

Thereby, even if audio renderer becomes Master Clock, clock delay due to data wait does not occur.

#### Merits

* **No Environment Change**: No registry operation or system filter disabling required.
* **No Impact on Other Apps**: Effective only when LR2 BGA Filter is registered.
* **Auto Apply**: Automatically handles problematic BGAs without user operation.

#### How to Use

~~Just register `LR2BGAFilter.ax` with `regsvr32`.~~

Please use the created installer.

```powershell
regsvr32 LR2BGAFilter.ax
```

Confirm with GraphStudioNext etc. that Audio output pin is connected to "LR2 BGA Null Audio Renderer".
