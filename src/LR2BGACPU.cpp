#include "LR2BGACPU.h"
#include <intrin.h>

bool LR2BGACPU::m_checked = false;
bool LR2BGACPU::m_sse41 = false;
bool LR2BGACPU::m_avx2 = false;

void LR2BGACPU::CheckFeatures() {
    if (m_checked) return;

    int ids[4];
    
    // Check for SSE4.1
    // CPUID EAX=1
    __cpuid(ids, 1);
    // ECX bit 19 = SSE4.1
    m_sse41 = (ids[2] & (1 << 19)) != 0;

    // Check for AVX2
    // Must check OSXSAVE bit first (ECX bit 27 of CPUID EAX=1)
    // AVX2の確認には、まずOSXSAVEビット(CPUID EAX=1のECX bit 27)をチェックする必要があります
    bool osxsave = (ids[2] & (1 << 27)) != 0;
    
    if (osxsave) {
        // CPUID EAX=7, ECX=0
        __cpuidex(ids, 7, 0);
        // EBX bit 5 = AVX2
        m_avx2 = (ids[1] & (1 << 5)) != 0;
    } else {
        m_avx2 = false;
    }

    m_checked = true;
}

bool LR2BGACPU::IsSSE41Supported() {
    CheckFeatures();
    return m_sse41;
}

bool LR2BGACPU::IsAVX2Supported() {
    CheckFeatures();
    return m_avx2;
}

