# LR2 BGA Filter

[![Japanese](https://img.shields.io/badge/lang-Japanese-blue.svg)](README.ja.md)
[![English](https://img.shields.io/badge/lang-English-red.svg)](README.md)
[![Korean](https://img.shields.io/badge/lang-Korean-red.svg)](README.ko.md)

DirectShow filter for LR2 (Lunatic Rave 2). Aims to improve BGA playback quality and performance.

## Features

- **Proper handling of high-resolution videos**: Balances image quality and performance with appropriate downscaling.
- **Other features**:
  - Auto black bar (letterbox) detection/removal
  - BGA display in external window
  - Brightness adjustment
  - FPS limit
  - Auto discard audio tracks
    - Avoids [troubles caused by silent tracks](docs/en/LR2_BGA_Stuttering_Investigation_and_Solutions.md)

## Requirements & Settings (IMPORTANT)

To make this filter work properly, **LAVFilters** installation and the following special settings are required.

1. **Install LAVFilters**
    - This filter assumes use with LAVFilters (LAV Splitter, LAV Video Decoder).
    - [Download LAV Filters](https://github.com/nevcairiel/lavfilters/releases) Download `LAVFilters-*.**-Installer.exe`.
    - Install LAV Splitter (x86), LAV Video Decoder (x86). Others are unnecessary.

2. **LAV Video Decoder Settings**
    - **Enable Major Formats**: Keep almost all Input Formats (H.264, HEVC, MPEG4, etc.) enabled. Should be enabled by default.
    - **Output Formats**: **Check ONLY RGB32 and uncheck everything else.**
        - *Reason*: LR2 only accepts RGB24 input. By limiting LAV output to RGB32, we intentionally establish the route "LAV (RGB32) -> [This Filter] -> LR2 (RGB24)", forcing this filter to be used.

3. **Merit Value (Priority) Settings**
    - The installer sets this filter (`LR2BGAFilter`) to **Highest Priority (Merit Value)**.
    - **LAV Video Decoder** is set to 2nd place.
    - *Note*: If this order is not followed, OS standard `quartz.dll` etc. might take priority and this filter might not be used.

### Disclaimer

The above settings (LAV output restriction and Merit Value changes) might affect the entire system using 32bit DirectShow (other video players etc.).
DirectShow itself is a legacy technology, so the impact on modern general usage should be limited, but please apply at your own discretion.

## Package Structure

The release package consists of:

```text
/ (root)
├── LR2BGAFilter.ax   (Filter binary)
├── Installer.exe     (For Install/Uninstall)
├── LR2BGAFilterConfigurationTool.bat (For Settings)
└── README.md         (This file)
```

## How to Install

As mentioned, LAV Filters installation and settings are required. Then install as follows:

1. Download latest ZIP from [Releases].
2. Extract to arbitrary folder. e.g. `C:\Bin\LR2BGAFilter`
3. Run `Installer.exe`.
    - Tips: You can force language display with command line args `.\Installer.exe /lang:ja` `.\Installer.exe /lang:en` `.\Installer.exe /lang:ko`. Default is OS setting.
4. Agree to registry changes for LAVFilters as mentioned and execute install.
5. After installation, run `LR2BGAFilterConfigurationTool.bat` to configure.

## How to Configure

Refer to [Configuration Guide](docs/en/LR2BGAFilterConfigurationTool.md) for details.

1. Run `LR2BGAFilterConfigurationTool.bat`.
2. Configure settings.
3. Click `OK` button to save.

## How to Uninstall

1. Run `Installer.exe`.
2. Automatically switches to uninstall mode if installed.
3. Choose to restore backup or delete user settings.
4. Click button to uninstall.

## License

[MIT License](LICENSE)
