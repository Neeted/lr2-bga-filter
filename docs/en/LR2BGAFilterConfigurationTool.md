# LR2BGAFilterConfigurationTool

## Overview

LR2BGAFilterConfigurationTool is a tool for configuring LR2BGAFilter.

## How to Use

1. Run `LR2BGAFilterConfigurationTool.bat`.
2. Configure settings.
3. Click `OK` button to save.

## Settings

### LR2 Output

- [ ] Dummy: Output a dummy black screen (1x1) to LR2 for 1 frame only.
- [ ] Passthrough: Output to LR2 at input resolution. Black bar removal is applied.
- [ ] Keep Aspect: Add black bars to maintain input video aspect ratio when outputting to LR2.
- [ ] Limit FPS: Limit output frame rate to LR2.
- Size: Set output resolution to LR2. **Recommended to match skin's BGA display area**. If matched correctly and Keep Aspect is enabled, no resizing occurs on LR2 side, achieving aspect ratio preservation.
- Algo: Select resize algorithm.
  - Nearest Neighbor: Fastest but output quality is low.
  - Bilinear: Good balance of performance and quality. Fully parallelized.
- Brightness: Adjust BGA brightness with slider.

### External Window

Draggable to move.

- [ ] Enable: Display BGA in external window.
- [ ] Passthrough: Output to external window at input resolution. Size setting is ignored. Black bar removal is applied.
- [ ] Keep Aspect: Add black bars to maintain input video aspect ratio when outputting to external window.
- Pos: Set external window position. Position moved by mouse is automatically saved.
- Size: Set external window size.
- Algo: Select resize algorithm. Can be set separately from LR2 output.
- Z-Order: Set Z-Order of external window.
  - Top: Always on top.
  - Bottom: Bottom-most on startup.
- Brightness: Adjust BGA brightness with slider. This adjusts transparency of black overlay window. This allows capturing pre-brightness-adjusted BGA with capture software like OBS.

### External Window Close Triggers

- [ ] R-Click: Close external window with right click.
- [ ] Gamepad: Close external window when specified button on gamepad is pressed.
  - [ ] ID: Gamepad ID. Should be 0 if only one gamepad is connected.
  - [ ] Btn: Gamepad button number.
  - ID and Btn can be checked in debug window (see below).
- [ ] Keyboard: Close external window when specified key is pressed.
  - Key Code: Set key code. Input in hex. e.g. 0xD (Enter key). Refer to debug window.
- [ ] Result Screen: Automatically close external window on result screen transition.
  - *Note*: **This feature detects transition by reading LR2 memory. It's not a clean implementation, so do not use if concerned.**

### Auto Letterbox Removal

Supports cropping to 16:9 and 4:3.

- [ ] Enable: Enable black bar removal.
- Threshold: Set threshold for black bar removal. Pixels below threshold are considered black.
- Stable: Set consecutive detection count for switching. Detection runs once every 200ms, so 3 means black bars must persist for over 600ms to be removed.

### Other

- [ ] Debug Mode (Info Window): Display debug window during video playback. You can check gamepad button numbers and key codes.
- [ ] Auto Open Properties: Automatically open settings screen during video playback. Useful for initial setup.

## Supplementary Notes

- Settings are saved in registry `HKEY_CURRENT_USER\SOFTWARE\LR2BGAFilter`. The reason for using registry instead of file is to avoid file I/O during video playback and prevent startup delay.
- Supplementary explanation on processing order as it might be confusing:
  - Black bar removal is performed first on the input frame.
  - So if you apply black bar removal to a BGA with 1:1 black bars for LR2 and output with Keep Aspect disabled, it will look vertically stretched. If Keep Aspect is enabled, it should look the same as original 1:1 BGA with black bars.
  - For 16:9 BGA without black bars, black bar removal is not applied, and if Keep Aspect is enabled, black bars are added according to output size.
  - **In short, it is recommended to always keep both Auto Letterbox Removal and Keep Aspect enabled.** It should display nicely even on skins where BGA area is not 1:1.
