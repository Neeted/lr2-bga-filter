#include "LR2MemoryMonitor.h"
#include <vector>
#include <algorithm>
#include <string>

LR2MemoryMonitor::LR2MemoryMonitor()
    : m_bStop(false)
{
}

LR2MemoryMonitor::~LR2MemoryMonitor()
{
    Stop();
}

void LR2MemoryMonitor::Start()
{
    // 安全確認: 適切なプロセス内で実行されているか確認
    if (!IsTargetProcess()) {
        OutputDebugStringW(L"[LR2MemoryMonitor] Not running in LR2 process. Monitor disabled.\n");
        return;
    }

    Stop(); // 既に実行中のスレッドがあれば停止
    m_bStop = false;
    m_thread = std::thread(&LR2MemoryMonitor::MonitorThread, this);
}

void LR2MemoryMonitor::Stop()
{
    m_bStop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void LR2MemoryMonitor::SetResultCallback(std::function<void(int)> callback)
{
    m_callback = callback;
}

bool LR2MemoryMonitor::IsTargetProcess()
{
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return false;
    }

    std::wstring wpath(path);
    // 小文字に変換して比較
    std::transform(wpath.begin(), wpath.end(), wpath.begin(), ::towlower);

    // プロセス名に "body" (LR2body.exe) が含まれているか確認
    if (wpath.find(L"body") != std::wstring::npos) {
        return true;
    }

    return false;
}

// SEH検証付きメモリ読み込みヘルパー
// C++オブジェクトの巻き戻しと同一関数内に書けないため、分離しています。
static bool SafeRead(void* src, void* dst, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LR2MemoryMonitor::MonitorThread()
{
    // 現在のプロセス (LR2body.exe) のベースアドレスを取得
    HMODULE hModule = GetModuleHandle(NULL);
    if (hModule == NULL) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(hModule);

    OutputDebugStringW(L"[LR2MemoryMonitor] Monitor started.\n");

    // メモリダンプからの解析に基づくアドレス検出:
    // メインゲーム構造体への静的ポインタが Base + 0x3AD80 (0x0043AD80) にあると考えられます。
    // その構造体の Base + 0x23DB4 (kSceneIdOffset) がシーンIDです。
    
    uintptr_t pointerToGameObj = baseAddr + 0x3AD80;
    uintptr_t gameObjBase = 0;
    
    // Initial read of base pointer
    if (SafeRead((void*)pointerToGameObj, &gameObjBase, sizeof(uintptr_t))) {
         wchar_t debugBuf[128];
         swprintf_s(debugBuf, L"[LR2MemoryMonitor] GameObjBase found at 0x%p -> 0x%p\n", (void*)pointerToGameObj, (void*)gameObjBase);
         OutputDebugStringW(debugBuf);
    }

    OutputDebugStringW(L"[LR2MemoryMonitor] Monitor started.\n");

    // フック地点: Base + 0xE796
    // 本来の命令: 5バイト: BB 01 00 00 00 (MOV EBX, 1)
    // フック後:   5バイト: E9 XX XX XX XX (JMP Trampoline)
    //
    // この地点の直前で EAX に「LR2内部構造体のベースアドレス」が格納されています。
    // LR2Helperはこの地点をフックして EAX の値を自身のメモリ領域に保存しています。
    // 本クラス(LR2MemoryMonitor)も同様に EAX の値を参照する必要があります。
    
    uintptr_t hookAddr = baseAddr + 0xE796;
    uintptr_t storageAddr = 0; // EAX (GameObjBase) が保存されるアドレスへのポインタ

    // --- フックのセットアップと共存戦略 ---
    unsigned char checkByte = 0;
    if (SafeRead((void*)hookAddr, &checkByte, 1)) {
        if (checkByte == 0xE9) {
            // ケース1: 既にフックされている
            // LR2Helperによるフック、あるいは本フィルタが再読み込みされた場合の自身の過去のフックなどが考えられます。
            // いずれの場合も戦略は同じで、トランポリンコードからEAXの保存先を探して相乗りします。
            OutputDebugStringW(L"[LR2MemoryMonitor] Existing hook detected. Attempting to piggyback...\n");
            
            // JMP先のトランポリンアドレスを計算
            int relativeOffset = 0;
            if (SafeRead((void*)(hookAddr + 1), &relativeOffset, 4)) {
                uintptr_t trampolineAddr = hookAddr + 5 + relativeOffset;
                
                // トランポリンコード内から `MOV [ADDR], EAX` を探す
                // LR2Helperのコードパターンはおおよそ以下のようになっていると想定される:
                //   MOV EBX, 1        (上書きされた命令の退避実行)
                //   CMP EAX, ...      (アドレス妥当性チェック)
                //   JL ...
                //   CMP ...
                //   JG ...
                //   MOV [ADDR], EAX   <-- この [ADDR] を知りたい (オフセット20-30バイト付近にあることが多い)
                
                unsigned char trampData[64];
                if (SafeRead((void*)trampolineAddr, trampData, 64)) {
                    for (int i = 0; i < 50; ++i) { // スキャン範囲 (十分な広さを確保)
                        if (trampData[i] == 0xA3) {
                            // パターン: A3 XX XX XX XX (MOV [ofs32], EAX)
                            // 次の4バイトが格納先アドレス
                            storageAddr = *(uintptr_t*)(trampData + i + 1);
                            break;
                        }
                        if (trampData[i] == 0x89 && trampData[i+1] == 0x05) {
                            // パターン: 89 05 XX XX XX XX (MOV [mem], EAX)
                            // 次の4バイトが格納先アドレス
                            storageAddr = *(uintptr_t*)(trampData + i + 2);
                            break;
                        }
                    }
                }
            }
        } else if (checkByte == 0xBB) {
            // ケース2: フックされていない (クリーンな状態)
            // 戦略: 自前でトランポリンコードを作成し、フックを設置する。
            OutputDebugStringW(L"[LR2MemoryMonitor] No hook detected. Installing custom hook...\n");
            
            // トランポリン領域 + データ保存領域を確保
            // 構成: [コード (32 bytes)] [データ保存用 (4 bytes)]
            void* pAlloc = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (pAlloc) {
                uintptr_t trampBase = (uintptr_t)pAlloc;
                uintptr_t dataAddr = trampBase + 32; // オフセット32の位置を EAX 保存場所とする
                storageAddr = dataAddr;
                
                std::vector<unsigned char> code;
                
                // 1. CMP EAX, 100000 (0x186A0) 
                //    最小値チェック
                code.push_back(0x3D);
                code.push_back(0xA0);
                code.push_back(0x86);
                code.push_back(0x01);
                code.push_back(0x00);
                
                // 2. JL +14 bytes (Skip to original code)
                //    範囲外なら保存せずスキップ
                //    7C 0E (ジャンプ先: CMP(5) + JG(2) + MOV(5) + 2(Reserve) = 14 bytes先)
                //    ※命令長再計算: 
                //      CMP EAX, 2M (5 bytes)
                //      JG +5       (2 bytes)
                //      MOV [addr], EAX (5 bytes)
                //      Total 12 bytes to skip. -> 7C 0C
                code.push_back(0x7C);
                code.push_back(0x0C);
                
                // 3. CMP EAX, 2000000 (0x1E8480)
                //    最大値チェック
                code.push_back(0x3D);
                code.push_back(0x80);
                code.push_back(0x84);
                code.push_back(0x1E);
                code.push_back(0x00);
                
                // 4. JG +5 bytes (Skip to original code)
                //    範囲外なら保存せずスキップ
                //    7F 05 (ジャンプ先: MOV(5) = 5 bytes先)
                code.push_back(0x7F);
                code.push_back(0x05);

                // 5. MOV [dataAddr], EAX
                //    現在のEAX(ベースアドレス)を確保した領域に保存する
                code.push_back(0xA3);
                uintptr_t* pDataAddr = (uintptr_t*)&dataAddr;
                code.push_back(((unsigned char*)pDataAddr)[0]);
                code.push_back(((unsigned char*)pDataAddr)[1]);
                code.push_back(((unsigned char*)pDataAddr)[2]);
                code.push_back(((unsigned char*)pDataAddr)[3]);
                
                // 6. MOV EBX, 1
                //    本来あった命令を実行 (上書きしてしまった分)
                //    ここが "original_code" ラベル相当
                code.push_back(0xBB);
                code.push_back(0x01);
                code.push_back(0x00);
                code.push_back(0x00);
                code.push_back(0x00);
                
                // 7. JMP returnsAddr
                //    元のコードフローに戻る (Base + 0xE79B)
                code.push_back(0xE9);
                uintptr_t returnAddr = baseAddr + 0xE79B;
                uintptr_t currentIp = trampBase + code.size() + 4; // 次の命令のアドレス
                int jmpOffset = (int)(returnAddr - currentIp);
                unsigned char* pOffset = (unsigned char*)&jmpOffset;
                code.push_back(pOffset[0]);
                code.push_back(pOffset[1]);
                code.push_back(pOffset[2]);
                code.push_back(pOffset[3]);
                
                // 作成したコードを書き込み
                memcpy(pAlloc, code.data(), code.size());
                
                // 本体側にJMP命令を書き込む (フック設置)
                DWORD oldProtect;
                if (VirtualProtect((void*)hookAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    unsigned char hookCode[5];
                    hookCode[0] = 0xE9; // JMP
                    uintptr_t hookJmpTarget = trampBase;
                    uintptr_t hookIp = hookAddr + 5;
                    int hookOffset = (int)(hookJmpTarget - hookIp);
                    *(int*)(hookCode + 1) = hookOffset;
                    
                    memcpy((void*)hookAddr, hookCode, 5);
                    VirtualProtect((void*)hookAddr, 5, oldProtect, &oldProtect);
                }
            }
        }
    }

    if (storageAddr == 0) {
        OutputDebugStringW(L"[LR2MemoryMonitor] Failed to setup hook or find storage. Monitor aborting.\n");
        m_bStop = true;
    }
    
    // アドレスフィルタリング用定数 (LR2Helperと同様の値)
    // 取得したアドレスがこの範囲外なら無効な値として無視する
    const uintptr_t kMinBaseAddr = 100000;
    const uintptr_t kMaxBaseAddr = 2000000;
    
    int lastScene = -1;

    while (!m_bStop && storageAddr != 0) {
        int currentScene = -1;
        bool readSuccess = false;
        uintptr_t gameObjBase = 0;

        // 保存された EAX (GameObjBase) を読み取る
        if (SafeRead((void*)storageAddr, &gameObjBase, sizeof(uintptr_t))) {
            // アドレスが妥当な範囲かチェック
            if (gameObjBase >= kMinBaseAddr && gameObjBase <= kMaxBaseAddr) {
                 // シーンIDのオフセット: base + 0x23DB4
                 uintptr_t targetAddr = gameObjBase + 0x23DB4; 
                 if (SafeRead((void*)targetAddr, &currentScene, sizeof(int))) {
                     readSuccess = true;
                 }
            }
        }

        if (readSuccess) {
            // シーン変更検知
            if (currentScene != lastScene && lastScene != -1) {
                wchar_t debugBuf[128];
                swprintf_s(debugBuf, L"[LR2MemoryMonitor] Scene Changed: %d -> %d\n", lastScene, currentScene);
                OutputDebugStringW(debugBuf);
                
                if (m_callback) {
                    m_callback(currentScene);
                }
            }
            lastScene = currentScene;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps間隔で監視
    }

    OutputDebugStringW(L"[LR2MemoryMonitor] Monitor stopped.\n");
}
