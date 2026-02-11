# LR2에서의 BGA 재생 개시 시의 끊김(Stuttering)에 관한 조사 보고와 대책

## 1. 현상

LR2(Lunatic Rave 2)에 있어서, 특정의 동영상 파일(BGA)이 재생되는 순간, 일순간 노트나 영상이 정지(끊김)하는 현상이 발생한다.
음성 트랙이 포함되어 있지만, LR2상에서는 소리가 나오지 않기 때문에 실해는 없는 것처럼 보이지만, 내부 처리로 지연을 일으키고 있다.

**【중요: 고해상도 렉과의 구별】**
LR2의 사양(DirectX 9 / 32 bit)에 기인하는 「HD/FHD 등의 고해상도 동영상을 재생했을 때에 전체적으로 프레임 레이트가 떨어지는 현상」과는 **명확하게 구별된다**.

* **고해상도 렉:** 동영상의 화소수가 너무 많기 때문에 발생해, 재생 중 항상 무거워진다.
* **본 현상:** 동영상의 해상도가 낮은 경량인 파일이어도, **재생 개시의 순간에** 끊김이 발생하는 점이 특징이다.

## 2. 원인: 음성 스트림의 「기아 상태(Starvation)」

직접적인 원인은, **동영상 파일에 포함되는 「비정상적인 저 비트 레이트의 음성 트랙」과 DirectShow의 사양과의 궁합 문제**이다.

* **상세 메커니즘:**
  1. **비정상적인 VBR 음성:** 음성 트랙이 「AAC VBR(가변 비트 레이트)」로 인코딩 되어 있고, 내용이 「무음」또는 「거의 무음」이다.
  2. **데이터 공급 부족:** VBR의 특성에 의해, 무음 구간의 데이터량이 극한까지 삭감(약 2 kbps 등)되고 있다.
  3. **클럭의 초기화 지연:** DirectShow의 음성 렌더러는, 재생 개시에 필요한 버퍼가 찰 때까지 대기하는 사양이 있다. 그러나, 데이터가 드문드문하기 때문에 버퍼가 차지 않고, 레퍼런스 클럭(기준 시계)이 움직이기 시작하지 않는다.
  4. **영상의 말려듦:** 음성 렌더러가 마스터 클럭이 되어 있기 때문에, 시계가 움직일 때까지 영상 측도 묘화를 정지하지 않을 수 없어, 끊김이 발생한다.

* **판별 방법(MediaInfo 등):**
  * 형식: AAC (VBR)
  * 비트 레이트: 극단적으로 낮다(예: 2kbps, 32kbps 이하)
  * 스트림 사이즈: 극소(수 KB~수십 KB)

## 3. 해결책

이하의 2개의 어프로치가 있다.
**방법 A**는 근본 해결(파일 수정), **방법 B**는 환경 설정에 의한 회피책(이번 결론)이다.

---

### 방법 A: 동영상 파일로부터 음성 트랙을 삭제한다(추천・근본 해결)

해당 BGA 파일로부터, 불필요한 음성 트랙을 물리적으로 삭제한다.
`ffmpeg`를 사용하는 경우, 이하의 커맨드로 무열화 또한 고속으로 처리 가능.

```bash
ffmpeg -i input.mp4 -c:v copy -an output.mp4
```

* `-c:v copy`: 영상은 재인코딩 하지 않고 그대로 카피(화질 열화 없음).
* `-an`: Audio None(음성을 삭제).

---

### 방법 B: Windows 표준 디코더를 무효화한다(환경 회피책)

LR2(32 bit)가 사용하는 「Microsoft DTV-DVD Audio Decoder」를 시스템 레벨로 무효화해, 음성 필터 그래프의 구축을 실패시키는 것으로, 강제적으로 영상 렌더러를 마스터 클럭으로서 동작시킨다.

#### 순서 1: 타겟이 되는 디코더의 특정(32 bit 환경)

LR2(32 bit 어플리케이션)가 사용할 가능성이 있는, Windows 표준의 VBR 대응 음성 디코더는 이하와 같다.
이것들 모든 메리트 값을 내리는 것으로, 표준 디코더에 의한 VBR 음성 처리를 완전하게 블록 할 수 있다.

##### 1. Microsoft DTV-DVD Audio Decoder (최중요 타겟)

* **역활:** AAC, MPEG-2 Audio, MP2 의 디코드 (이번의 주범)
* **CLSID:** `{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **대상 레지스트리:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}`
* **원래 메리트 값** `0x005FFFFF (MERIT_NORMAL - 1)`

##### 2. MPEG Audio Decoder

* **역활:** MP3, MPEG-1 Audio (Layer I, II) 의 디코드 (MP3 VBR 대책)
* **CLSID:** `{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **대상 레지스트리:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{4A2286E0-7BEF-11CE-9BD9-0000E202599C}`
* **원래 메리트 값** `0x03680001 (MERIT_PREFERRED + 2 보다 높고 꽤 높은 우선도)`
* **주의:** 실체(quartz.dll)는 시스템 중요 파일이기 때문에, 절대로 삭제・등록 해제하지 말 것. 메리트 값 변경에만 그칠 것. 이하의 MPEG Layer-3 도 마찬가지.

##### 3. MPEG Layer-3

* **역활:** 낡은 형식이나 특정의 MP3 압축 형식의 디코드 (예비, 기본값이라도 사용되지 않는다고 생각됨)
* **CLSID:** `{6A08CF80-0E18-11CF-A24D-0020AFD79767}`
* **원래 메리트 값** `MERIT_DO_NOT_USE (0x00200000)`
* **대상 레지스트리:** `\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{6A08CF80-0E18-11CF-A24D-0020AFD79767}`

---

#### 순서 2: 메리트 값(우선도)의 변경 수단을 준비

이 필터의 메리트 값을 `DO_NOT_USE (0x00200000)`이하, 또는 `0`으로 변경한다.

##### 추천 툴 리스트

* [DirectShow Filter Tool (dftool)](https://web.archive.org/web/20241212203500/https://hp.vector.co.jp/authors/VA032094/DFTool.html)
* [GraphStudioNext (32bit판)](https://github.com/cplussharp/graph-studio-next)
* [DirectShow Filter Manager (DSFMgr)](https://www.videohelp.com/software/DirectShow-Filter-Manager)

##### GraphStudioNext의 사용법

1. **관리자 권한으로 기동:** `graphstudionext.exe` (32bit판) 를 우클릭 해 「관리자로서 실행」.
2. **필터 일람을 표시:** 메뉴의 `Graph` > `Insert Filter...` 를 선택.
3. **대상을 검색:** 상기의 명칭(예: `Microsoft DTV-DVD Audio Decoder`)으로 검색.
4. **메리트 값 변경:** 필터명을 선택해, 오른쪽 페인의 `Change Merit` 버튼을 누른다.
5. **값을 설정:** `DO_NOT_USE (0x00200000)` 이하, 또는 `0` 을 입력해 적용한다.

※ 권한 에러가 나오는 경우는 이하의 순서로 레지스트리 권한을 조작한다.

---

#### 【옵션】순서 3: TrustedInstaller 권한의 처리 순서

Windows 표준 필터는 강력한 보호가 걸려 있기 때문에, 이하의 순서로 조작할 필요가 있다.

1. **레지스트리 편집기 기동:** 관리자 권한으로 `regedit` 를 연다.
2. **키로 이동:** `HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{E1F1A0B8-BEEE-490D-BA7C-066C40B5E2B9}` 를 연다.
3. **소유자의 변경:**
    * 키를 우클릭 → [액세스 허가] → [상세 설정].
    * 소유자를 `Administrators` 로 변경한다.
4. **액세스 허가의 변경:**
    * 상속원이 존재하는 경우 [상속의 무효화] 를 실시해, 부모 키로부터의 설정을 분리한다.
    * `Administrators` 에 「풀 컨트롤」을 부여한다.
5. **메리트 값의 재작성:**
    * 바이너리 값 `FilterData` 를 편집해, 메리트 값 부분을 `00 00 00 00 00 00 00 00` 등으로 변경한다. 바이너리 구조는 메리트 값 이외의 것도 포함되어 있기 때문에 통상은 툴을 사용해 변경한다.
6. **【추천】권한을 원래대로 되돌린다(보호의 복원):**
    * `Administrators` 의 권한을 「읽기」만으로 되돌린다.
    * 소유자를 `NT Service\TrustedInstaller` 로 되돌린다.

#### 결과 확인

* GraphStudioNext(32 bit) 등으로 해당 파일을 렌더링 했을 때, Audio 출력 핀이 어디에도 접속되지 않은(또는 Audio 핀 자체가 생성되지 않는) 상태가 되면 성공.
* LR2에서의 재생 시, 끊김 없이 부드럽게 개시되는 것을 확인.

## 4. 부작용과 주의점

* **다른 어플리케이션에의 영향:** 「Microsoft DTV-DVD Audio Decoder」를 무효화하면, Windows Media Player나 「영화&TV」어플리케이션 등으로, MPEG-2 음성이나 AAC 음성을 재생할 수 없게 될 가능성이 있다.
* **LAV Filters와의 균형:** LAV Audio Decoder가 인스톨 되어 있는 경우, 그쪽에 접속되어 버리면 끊김이 재발할 가능성이 있다. 그 경우는 LAV 측에서도 해당 포맷(AAC 등)을 무효화할 필요가 있다.

---

## [2026-02-10] 5. 구현된 해결책: LR2 BGA Null Audio Renderer

상기의 방법 A・B에 가세해, 본 프로젝트에서는 **방법 C**로서 전용의 DirectShow 필터를 구현했다.

### 방법 C: LR2 BGA Null Audio Renderer (추천・자동 해결)

`LR2BGAFilter.ax`에 동봉되는 「**LR2 BGA Null Audio Renderer**」는, 음성 스트림을 즉석에서 파기하는 Null Renderer이다.

#### 기술 사양

| 항목       | 상세                                     |
| ---------- | ---------------------------------------- |
| **필터명** | LR2 BGA Null Audio Renderer              |
| **CLSID**  | `{64878B0F-CC73-484F-9B7B-47520B40C8F0}` |
| **상속원** | `CBaseRenderer` (DirectShow BaseClasses) |
| **입력**   | `MEDIATYPE_Audio` (전 서브 타입 대응)    |
| **Merit**  | `0xfff00000` (최고 우선도)               |

#### 동작 원리

1. **자동 접속**: Merit 값이 최고 우선도이기 때문에, BGA 파일에 음성 트랙이 존재하면 자동적으로 접속된다.
2. **즉시 파기**: `DoRenderSample()`로 샘플을 즉석에서 파기한다.
3. **대기 없음**: `ShouldDrawSampleNow()`로 항상 `S_OK`를 돌려주어, 프레젠테이션 시각을 기다리지 않는다.

이것에 의해, 음성 렌더러가 마스터 클럭이 되어도, 데이터 대기에 의한 클럭 지연이 발생하지 않는다.

#### 메리트

* **환경 변경 불필요**: 레지스트리 조작이나 시스템 필터의 무효화가 불필요.
* **타 어플리케이션에의 영향 없음**: LR2 BGA Filter 등록 시에만 유효.
* **자동 적용**: 사용자 조작 없이 문제가 있는 BGA에 자동 대응.

#### 사용 방법

~~`LR2BGAFilter.ax`를 `regsvr32`로 등록하는 것만으로 유효하게 된다.~~

인스톨러를 작성했으므로, 그쪽을 사용해 주세요.

```powershell
regsvr32 LR2BGAFilter.ax
```

GraphStudioNext 등으로 확인하면, Audio 출력 핀이 「LR2 BGA Null Audio Renderer」에 접속되어 있는 것을 알 수 있다.
