/**
 * @file    FileScanner.h
 * @brief   文件扫描 DLL 公开接口
 *
 * DLL 不存储文件系统树，仅累积统计。调用方通过 IScanCallback 实时接收数据并自行建树—
 */

#ifndef FILESCANNER_H
#define FILESCANNER_H

#include <cstdint>
#include <string>

struct ScanResult
{
    uint64_t totalSize;
    uint32_t fileCount;
    uint32_t dirCount;

    ScanResult() : totalSize(0), fileCount(0), dirCount(0) {}
    void Reset() { totalSize = 0; fileCount = 0; dirCount = 0; }
};

/* =========================================================================
 *  回调接口
 * ========================================================================= */

class IScanCallback
{
public:
    virtual ~IScanCallback() {}

    // 注意：回调在扫描工作线程中执行，不是 UI 线程。
    virtual void OnFileFound(const std::wstring &path, uint64_t size,uint64_t lastWriteTime) = 0;
    virtual void OnError(const std::wstring &path,const std::wstring &errorMsg) = 0;
    // 调用此函数后即可安全地调用 GetResult()
    virtual void OnScanComplete(bool cancelled) = 0;
};

/* =========================================================================
 *  扫描器接口
 * ========================================================================= */

class IFileScanner
{
public:
    virtual ~IFileScanner() {}

    // 非阻塞启动，立即返回。扫描在工作线程中异步执行。
    virtual bool StartScan(const std::wstring &folderPath,IScanCallback *callback) = 0;

    // 同步等待所有工作线程退出，之后触发 OnScanComplete(true)
    virtual void StopScan() = 0;

    virtual const ScanResult &GetResult() const = 0;
    virtual bool IsScanning() const = 0;
};

/* =========================================================================
 *  C 回调类型
 * ========================================================================= */

typedef void (*PFN_OnFileFound)(const wchar_t *path, unsigned long long size,
                                 unsigned long long lastWriteTime);
typedef void (*PFN_OnError)(const wchar_t *path, const wchar_t *msg);
typedef void (*PFN_OnScanComplete)(int cancelled);

extern "C" {

__declspec(dllexport) IFileScanner *CreateFileScanner();
__declspec(dllexport) void DestroyFileScanner(IFileScanner *scanner);

// 预注册 C 回调的工厂——调用 Scanner_StartScan 时无需再传 IScanCallback
__declspec(dllexport) IFileScanner *CreateFileScannerEx(
    PFN_OnFileFound   onFileFound,
    PFN_OnError       onError,
    PFN_OnScanComplete onScanComplete);

__declspec(dllexport) int  Scanner_StartScan(IFileScanner *scanner, const wchar_t *path);
__declspec(dllexport) void Scanner_StopScan(IFileScanner *scanner);
__declspec(dllexport) int  Scanner_IsScanning(IFileScanner *scanner);

} // extern "C"

#endif
