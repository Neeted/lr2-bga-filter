#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <intrin.h>

//------------------------------------------------------------------------------
// LR2BGACPU.h
//
// 概要:
//   実行環境のCPU機能を検出し、利用可能なSIMD命令セット (SSE4.1, AVX2等) を
//   判定するためのヘルパークラスです。
//------------------------------------------------------------------------------

class LR2BGACPU {
public:
    static bool IsSSE41Supported();
    static bool IsAVX2Supported();

private:
    // CPUID情報を取得してキャッシュする
    static void CheckFeatures();
    static bool m_checked;
    static bool m_sse41;
    static bool m_avx2;
};

