# LR2 BGA Filter

[![Japanese](https://img.shields.io/badge/lang-Japanese-blue.svg)](README.ja.md)
[![English](https://img.shields.io/badge/lang-English-red.svg)](README.md)
[![Korean](https://img.shields.io/badge/lang-Korean-red.svg)](README.ko.md)

LR2 (Lunatic Rave 2) 용 DirectShow 필터입니다. BGA의 재생 품질과 성능을 개선하는 것을 목적으로 합니다.

![header](docs/img/header.jpg)
## 특징

- **고해상도 동영상의 적절한 처리**: 적절한 다운스케일링으로 화질과 성능을 양립합니다.
- **기타 기능**:
  - 블랙 바(레터박스)의 자동 검출・제거
  - 외부 창에서의 BGA 표시
  - 밝기 조정
  - fps 제한
  - 오디오 트랙 자동 파기
    - [무음 트랙 유래의 문제](docs/ko/LR2_BGA_Stuttering_Investigation_and_Solutions.md)를 회피할 수 있습니다

## 동작 요건・설정 (중요)

본 필터를 정상적으로 동작시키기 위해서는, **LAVFilters**의 도입과, 이하의 특수한 설정이 필요합니다.

1. **LAVFilters 설치**
    - 본 필터는 LAVFilters(LAV Splitter, LAV Video Decoder)와 조합해서 사용하는 것을 전제로 하고 있습니다.
    - [**LAV Filters 다운로드**](https://github.com/nevcairiel/lavfilters/releases) `LAVFilters-*.**-Installer.exe`를 다운로드해 주세요.
    - LAV Splitter (x86), LAV Video Decoder (x86)를 설치해 주세요. 그 외는 불필요합니다.

2. **LAV Video Decoder 설정**
    - **주요 포맷의 유효화**: 거의 모든 Input Formats(H.264, HEVC, MPEG4 등)를 유효한 상태로 해 두세요. 기본값으로 유효하게 되어 있을 겁니다.
    - **출력 색공간의 제한 (Output Formats)**: **RGB32 만을 체크하고, 다른 것은 모두 해제해 주세요.**
        - *이유*: LR2 본체는 RGB24 입력밖에 받지 않습니다. LAV의 출력을 RGB32로 한정하는 것으로, 「LAV (RGB32) -> [본 필터] -> LR2 (RGB24)」라고 하는 경로를 의도적으로 성립시켜, 본 필터가 강제적으로 사용되도록 합니다.

3. **메리트 값(우선도)의 설정**
    - 인스톨러에 의해 본 필터(`LR2BGAFilter`)는 **가장 높은 우선도(Merit Value)**로 설정됩니다.
    - **두 번째로 LAV Video Decoder**가 오도록 설정됩니다.
    - *주의*: 이 순서를 지키지 않으면, OS 표준의 `quartz.dll` 등이 우선되어 버려 본 필터가 사용되지 않는 경우가 있습니다.

### 주의 사항 (면책)

상기의 설정(LAV의 출력 제한이나 메리트치 변경)은, 32 bit판의 DirectShow를 사용하는 시스템 전체(다른 동영상 플레이어 등)에 영향을 줄 가능성이 있습니다.
DirectShow 자체가 레거시 기술이며, 현대의 일반적인 용도에서는 영향은 한정적이라고 생각됩니다만, 양해 후 적용해 주세요.

## 배포 패키지 구성

릴리스 패키지는 이하의 구성으로 되어 있습니다.

```text
/ (root)
├── LR2BGAFilter.ax   (필터 본체)
├── Installer.exe     (설치/언인스톨용)
├── LR2BGAFilterConfigurationTool.bat (설정 화면 기동용)
└── README.md         (본서)
```

## 설치 방법

전술한 대로, LAV Filters의 도입과 설정이 필요합니다. 그 후, 이하의 순서로 인스톨 합니다.

1. [**Releases**](https://github.com/Neeted/lr2-bga-filter/releases) 페이지에서 최신 ZIP을 다운로드합니다.
2. 임의의 폴더에 압축을 풉니다. `C:\Bin\LR2BGAFilter` 등
3. `Installer.exe`를 실행합니다.
    - Tips: 커맨드 라인으로부터 `.\Installer.exe /lang:ja` `.\Installer.exe /lang:en` `.\Installer.exe /lang:ko`로 각각 일본어, 영어, 한국어 표시로 할 수 있습니다. 인수가 없는 경우는 OS의 언어 설정을 우선합니다.
4. 전술한 대로, LAVFilters에 관한 레지스트리 설정이 변경되는 것에 동의하고, 인스톨을 실행합니다.
5. 인스톨이 완료되면, `LR2BGAFilterConfigurationTool.bat`를 실행해 설정을 실시합니다.

## 설정 방법

자세한 설정 방법은 [**설정 방법**](docs/ko/LR2BGAFilterConfigurationTool.md)을 참조해 주세요.

1. `LR2BGAFilterConfigurationTool.bat`를 실행합니다.
2. 각종 설정을 실시합니다.
3. `OK` 버튼을 눌러 설정을 보존합니다.

## 언인스톨 방법

1. `Installer.exe`를 실행합니다.
2. 인스톨 완료인 경우는 자동으로 언인스톨 모드로 전환됩니다.
3. 인스톨 시의 백업을 복원할지, 사용자 설정을 삭제할지를 선택합니다.
4. 버튼을 눌러 언인스톨을 실행합니다.

## 라이센스

[MIT License](LICENSE)
