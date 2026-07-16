/**
 * @file    FileScannerImpl.h
 * @brief   FileScanner 内部实现——不对外暴露
 *
 * 包含辅助工具、C 回调适配器、以及核心扫描引擎 FileScannerImpl 的声明。
 * 外部调用方只需引用 FileScanner.h。
 */

#ifndef FILESCANNERIMPL_H
#define FILESCANNERIMPL_H

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "FileScanner.h"

/* =========================================================================
 *  工具函数
 * ========================================================================= */

// 将 Win32 错误码格式化为可读信息
std::wstring FormatWin32Error(DWORD err);

// 计算工作线程数，限制在 [2, 8]
unsigned int GetWorkerCount();

/* =========================================================================
 *  内部数据结构
 * ========================================================================= */

struct DirTask
{
    std::wstring path;
};

/* =========================================================================
 *  C 回调 -> C++ IScanCallback 适配器
 * ========================================================================= */

class CCallbackAdapter : public IScanCallback
{
    PFN_OnFileFound    m_onFileFound;
    PFN_OnError        m_onError;
    PFN_OnScanComplete m_onScanComplete;

public:
    CCallbackAdapter(PFN_OnFileFound f,PFN_OnError e,PFN_OnScanComplete c);

    virtual void OnFileFound(const std::wstring &path, uint64_t size,
                             uint64_t lastWriteTime) override;
    virtual void OnError(const std::wstring &path,
                         const std::wstring &msg) override;
    virtual void OnScanComplete(bool cancelled) override;
};

/* =========================================================================
 *  扫描器核心实现
 * ========================================================================= */

class FileScannerImpl : public IFileScanner
{
public:
    FileScannerImpl();
    virtual ~FileScannerImpl() override;

    // 预注册 C 回调（供 CreateFileScannerEx 使用）
    void SetCCallbacks(PFN_OnFileFound   f,
                       PFN_OnError       e,
                       PFN_OnScanComplete c);

    virtual bool  StartScan(const std::wstring &folderPath,
                            IScanCallback      *callback) override;
    virtual void  StopScan() override;

    virtual const ScanResult &GetResult()  const override;
    virtual bool              IsScanning() const override;

private:
    // 工作线程主循环
    void WorkerLoop();

    // 处理单个目录，返回 true 表示此线程遇到了"最后一个任务已处理完毕"
    bool ProcessDirectory(const DirTask &task);

    // 尝试通知扫描完成（内部会检查 m_completed，确保只通知一次）
    void TryNotifyComplete(bool cancelled);

    /* =============================================================
     *  锁层级（获取必须按此顺序，避免死锁）：
     *    1. m_stateMutex  -> m_scanning, m_completed, m_result (计数器)
     *    2. m_queueMutex  -> m_cancelled, m_pendingCount, m_queue
     * ============================================================= */

    bool                         m_cancelled;
    int                          m_pendingCount;
    bool                         m_scanning;
    bool                         m_completed;
    mutable std::mutex           m_queueMutex;
    mutable std::mutex           m_stateMutex;
    std::condition_variable      m_cond;

    std::vector<std::thread>     m_workers;
    std::queue<DirTask>          m_queue;

    ScanResult                   m_result;
    IScanCallback               *m_callback;
    CCallbackAdapter            *m_cAdapter;
};

#endif
