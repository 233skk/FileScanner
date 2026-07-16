/**
 * @file    FileScanner.cpp
 * @brief   DLL 入口 + extern "C" 导出工厂函数
 *
 * 这是 DLL 的对外边界层，只负责创建/销毁扫描器实例以及参数校验。
 * 所有扫描逻辑在 FileScannerImpl 中。
 */

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include "FileScanner.h"
#include "FileScannerImpl.h"

/* =========================================================================
 *  DllMain
 * ========================================================================= */

BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/,
                    DWORD     fdwReason,
                    LPVOID    /*lpvReserved*/)
{
    (void)fdwReason;
    return TRUE;
}

/* =========================================================================
 *  extern "C" 导出
 * ========================================================================= */

extern "C" {

__declspec(dllexport) IFileScanner *CreateFileScanner()
{
    return new FileScannerImpl();
}

__declspec(dllexport) void DestroyFileScanner(IFileScanner *scanner)
{
    delete scanner;
}

__declspec(dllexport) IFileScanner *CreateFileScannerEx(
    PFN_OnFileFound    onFileFound,
    PFN_OnError        onError,
    PFN_OnScanComplete onScanComplete)
{
    FileScannerImpl *scanner = new FileScannerImpl();
    scanner->SetCCallbacks(onFileFound, onError, onScanComplete);
    return scanner;
}

__declspec(dllexport) int Scanner_StartScan(IFileScanner *scanner, const wchar_t *path)
{
    if (!scanner || !path) return 0;
    // 传 NULL -> 内部使用 CreateFileScannerEx 预注册的 C 回调
    return scanner->StartScan(path, NULL) ? 1 : 0;
}

__declspec(dllexport) void Scanner_StopScan(IFileScanner *scanner)
{
    if (scanner) scanner->StopScan();
}

__declspec(dllexport) int Scanner_IsScanning(IFileScanner *scanner)
{
    if (!scanner) return 0;
    return scanner->IsScanning() ? 1 : 0;
}

} // extern "C"
