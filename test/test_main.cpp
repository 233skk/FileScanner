/**
 * @file    test_main.cpp
 * @brief   FileScanner.dll 测试程序
 *
 * 用法: FileScannerTest.exe [文件夹路径]
 */

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "FileScanner.h"

class ConsoleCallback : public IScanCallback
{
public:
    ConsoleCallback() : m_fileCount(0), m_lastTick(0) {}

    virtual void OnFileFound(const std::wstring & /*path*/,
                             uint64_t               /*size*/,
                             uint64_t               /*lastWriteTime*/)
    {
        ++m_fileCount;
        ULONGLONG now = GetTickCount64();
        if (now - m_lastTick > 1000) {
            printf("\r  Scanned %u files...", m_fileCount);
            fflush(stdout);
            m_lastTick = now;
        }
    }

    virtual void OnError(const std::wstring &path,
                         const std::wstring &errorMsg)
    {
        printf("\n  [Skip] ");
        wprintf(L"%ls", path.c_str());
        printf("\n         Reason: ");
        wprintf(L"%ls", errorMsg.c_str());
        printf("\n");
    }

    virtual void OnScanComplete(bool cancelled)
    {
        printf("\r  ----------------------------------------\n");
        if (cancelled)
            printf("  Scan cancelled.\n");
        else
            printf("  Scan completed successfully.\n");
        printf("  Files found (via callback): %u\n", m_fileCount);
    }

private:
    unsigned int m_fileCount;
    ULONGLONG    m_lastTick;
};

static void PrintSize(uint64_t size)
{
    if (size >= 1024ULL * 1024 * 1024)
        printf("%.2f GB", static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0));
    else if (size >= 1024 * 1024)
        printf("%.2f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    else if (size >= 1024)
        printf("%.2f KB", static_cast<double>(size) / 1024.0);
    else
        printf("%llu B", static_cast<unsigned long long>(size));
}

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    std::wstring rootPath;
    if (argc >= 2) {
        int len = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, NULL, 0);
        if (len > 0) {
            std::vector<wchar_t> wideBuf(len);
            MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wideBuf.data(), len);
            rootPath = wideBuf.data();
        }
    }
    if (rootPath.empty()) {
        wchar_t buf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, buf);
        rootPath = buf;
    }

    printf("========================================\n");
    printf("  FileScanner Test\n");
    printf("  Target: ");
    wprintf(L"%ls", rootPath.c_str());
    printf("\n========================================\n\n");

    IFileScanner *scanner = CreateFileScanner();
    if (!scanner) {
        printf("ERROR: CreateFileScanner() returned NULL\n");
        return 1;
    }

    ConsoleCallback callback;
    if (!scanner->StartScan(rootPath, &callback)) {
        printf("ERROR: Invalid path or not a directory\n");
        DestroyFileScanner(scanner);
        return 1;
    }

    printf("Scanning...\n");
    DWORD startTick = GetTickCount();
    while (scanner->IsScanning()) {
        Sleep(100);
    }
    DWORD elapsed = GetTickCount() - startTick;

    const ScanResult &result = scanner->GetResult();
    printf("\n  -------- Results --------\n");
    printf("  Files:  %8u\n", result.fileCount);
    printf("  Dirs:   %8u\n", result.dirCount);
    printf("  Size:   ");
    PrintSize(result.totalSize);
    printf("\n  Time:   %8u ms\n", elapsed);
    printf("  ------------------------\n");

    DestroyFileScanner(scanner);
    return 0;
}
