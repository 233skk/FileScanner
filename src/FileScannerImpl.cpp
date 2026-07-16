/**
 * @file    FileScannerImpl.cpp
 * @brief   FileScanner
 *
 */

#include "FileScannerImpl.h"

#include <algorithm>

/* =========================================================================
 *  工具函数
 * ========================================================================= */

std::wstring FormatWin32Error(DWORD err)
{
    wchar_t *buf = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, NULL);
    std::wstring msg;
    if (buf && len > 0) {
        if (len >= 2 && buf[len - 2] == L'\r' && buf[len - 1] == L'\n')
            len -= 2;
        else if (len >= 1 && (buf[len - 1] == L'\n' || buf[len - 1] == L'\r'))
            len -= 1;
        msg.assign(buf, len);
        LocalFree(buf);
    } else {
        msg = L"未知错误";
    }
    return msg;
}

unsigned int GetWorkerCount()
{
    unsigned int n = std::thread::hardware_concurrency();
    if (n < 2)  n = 2;
    if (n > 8)  n = 8;
    return n;
}

/* =========================================================================
 *  CCallbackAdapter
 * ========================================================================= */

CCallbackAdapter::CCallbackAdapter(PFN_OnFileFound   f,
                                   PFN_OnError       e,
                                   PFN_OnScanComplete c)
    : m_onFileFound(f)
    , m_onError(e)
    , m_onScanComplete(c)
{}

void CCallbackAdapter::OnFileFound(const std::wstring &path, uint64_t size,
                                   uint64_t lastWriteTime)
{
    if (m_onFileFound)
        m_onFileFound(path.c_str(), size, lastWriteTime);
}

void CCallbackAdapter::OnError(const std::wstring &path, const std::wstring &msg)
{
    if (m_onError)
        m_onError(path.c_str(), msg.c_str());
}

void CCallbackAdapter::OnScanComplete(bool cancelled)
{
    if (m_onScanComplete)
        m_onScanComplete(cancelled ? 1 : 0);
}

/* =========================================================================
 *  FileScannerImpl
 * ========================================================================= */

FileScannerImpl::FileScannerImpl()
    : m_cancelled(false)
    , m_pendingCount(0)
    , m_scanning(false)
    , m_completed(false)
    , m_callback(NULL)
    , m_cAdapter(NULL)
{}

FileScannerImpl::~FileScannerImpl()
{
    StopScan();
    delete m_cAdapter;
    m_cAdapter = NULL;
}

void FileScannerImpl::SetCCallbacks(PFN_OnFileFound   f,
                                    PFN_OnError       e,
                                    PFN_OnScanComplete c)
{
    delete m_cAdapter;
    m_cAdapter = new CCallbackAdapter(f, e, c);
}

bool FileScannerImpl::StartScan(const std::wstring &folderPath , IScanCallback *callback)
{
    // m_stateMutex 充当"扫描中"的标志
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_scanning)
            return false;
        m_scanning  = true;
        m_completed = false;
    }

    DWORD attrs = GetFileAttributesW(folderPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_scanning = false;
        return false;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_scanning = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cancelled = false;
    }
    m_result.Reset();

    // 传入的 C++ 回调优先于 CreateFileScannerEx 预注册的 C 回调
    if (callback)
        m_callback = callback;
    else if (m_cAdapter)
        m_callback = m_cAdapter;
    else {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_scanning = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_result.dirCount = 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        DirTask rootTask;
        rootTask.path = folderPath;
        m_queue.push(rootTask);
        m_pendingCount = 1;
    }

    unsigned int workerCount = GetWorkerCount();
    m_workers.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i) {
        m_workers.push_back(std::thread([this] { WorkerLoop(); }));
    }

    return true;
}

void FileScannerImpl::StopScan()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cancelled = true;
    }
    m_cond.notify_all();

    for (size_t i = 0; i < m_workers.size(); ++i) {
        if (m_workers[i].joinable()) {
            m_workers[i].join();
        }
    }
    m_workers.clear();

    // TryNotifyComplete 内部会检查 m_completed，确保只通知一次
    TryNotifyComplete(true);
}

const ScanResult &FileScannerImpl::GetResult() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_result;
}

bool FileScannerImpl::IsScanning() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_scanning;
}

void FileScannerImpl::WorkerLoop()
{
    for (;;) {
        DirTask task;
        bool hasTask = false;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);

            m_cond.wait(lock, [this] {
                return m_cancelled || !m_queue.empty() || m_pendingCount == 0;
            });

            if (m_cancelled)
                return;

            if (m_queue.empty())
                return;

            task    = m_queue.front();
            m_queue.pop();
            hasTask = true;
        }

        if (hasTask) {
            bool wasLast = ProcessDirectory(task);
            if (wasLast) {
                TryNotifyComplete(false);
                return;
            }
        }
    }
}

bool FileScannerImpl::ProcessDirectory(const DirTask &task)
{
    const std::wstring &dirPath = task.path;
    std::wstring searchPattern = dirPath + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        {
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                cancelled = m_cancelled;
            }
            if (m_callback && !cancelled) {
                DWORD err = GetLastError();
                m_callback->OnError(dirPath, FormatWin32Error(err));
            }
        }

        int remaining;
        bool cancelled;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            --m_pendingCount;
            remaining = m_pendingCount;
            cancelled = m_cancelled;
            if (remaining == 0)
                m_cond.notify_all();
        }
        return (remaining == 0 && !cancelled);
    }

    // 每 64 项才取锁检查取消标志，批量检查减少开销
    unsigned int cancelCheckCounter = 0;
    do {
        if (++cancelCheckCounter >= 64) {
            cancelCheckCounter = 0;
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                cancelled = m_cancelled;
            }
            if (cancelled)
                break;
        }

        if (wcscmp(fd.cFileName, L".")  == 0
            || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;

        std::wstring fullPath = dirPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_result.dirCount++;
            }
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                DirTask subTask;
                subTask.path = fullPath;
                m_queue.push(subTask);
                ++m_pendingCount;
            }
            m_cond.notify_one();

        } else {
            ULARGE_INTEGER uli;
            uli.LowPart  = fd.nFileSizeLow;
            uli.HighPart = fd.nFileSizeHigh;
            uint64_t fileSize = uli.QuadPart;

            ULARGE_INTEGER uliTime;
            uliTime.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
            uliTime.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            uint64_t lastWrite = uliTime.QuadPart;

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_result.totalSize += fileSize;
                m_result.fileCount++;
            }

            if (m_callback) {
                m_callback->OnFileFound(fullPath, fileSize, lastWrite);
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    int remaining;
    bool cancelled;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        --m_pendingCount;
        remaining = m_pendingCount;
        cancelled = m_cancelled;
        if (remaining == 0)
            m_cond.notify_all();
    }
    return (remaining == 0 && !cancelled);
}

void FileScannerImpl::TryNotifyComplete(bool cancelled)
{
    IScanCallback *cb = NULL;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_completed)
            return;
        m_completed = true;
        m_scanning  = false;
        cb          = m_callback;
        m_callback  = NULL;
    }
    // 锁外回调：如果回调内部调了 StopScan -> TryNotifyComplete，持锁会导致死锁
    if (cb) {
        cb->OnScanComplete(cancelled);
    }
}