#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include "log.h"
#include "log_archive_logic.h"

static FILE* g_file = nullptr;
static CRITICAL_SECTION g_cs;
static std::wstring g_directory;

const wchar_t* LogDirectory() { return g_directory.c_str(); }

static log_archive::Stamp StampOf(const SYSTEMTIME& t)
{
    return {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond};
}

// The log the run before left, copied into <directory>Logs\ as "<name> <when that run started>.log"; then only the
// newest `keep` copies of this name are left there. Best effort: anything that fails leaves the log as it was.
static void ArchivePreviousLog(const wchar_t* filePath, unsigned keep)
{
    const HANDLE old = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (old == INVALID_HANDLE_VALUE)
        return;
    char head[16]{};
    DWORD read = 0;
    LARGE_INTEGER size{};
    FILETIME written{}, localWritten{};
    SYSTEMTIME lastWrite{};
    const bool known = GetFileSizeEx(old, &size) && size.QuadPart > 0 &&
                       ReadFile(old, head, sizeof(head), &read, nullptr) &&
                       GetFileTime(old, nullptr, nullptr, &written) &&
                       FileTimeToLocalFileTime(&written, &localWritten) &&
                       FileTimeToSystemTime(&localWritten, &lastWrite);
    CloseHandle(old);
    if (!known)
        return;
    log_archive::Stamp start{};
    if (!log_archive::RunStart(head, read, StampOf(lastWrite), start))
        start = StampOf(lastWrite);
    std::wstring name(filePath);
    const size_t slash = name.find_last_of(L"\\/");
    name = slash == std::wstring::npos ? name : name.substr(slash + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".log") == 0)
        name.resize(name.size() - 4);
    const std::wstring folder = g_directory + L"Logs\\";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return;
    if (!CopyFileW(filePath, (folder + log_archive::ArchiveName(name, start)).c_str(), FALSE))
        return;
    std::vector<std::wstring> archived;
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((folder + name + L" *.log").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            archived.emplace_back(found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    for (const std::wstring& stale : log_archive::ToPrune(std::move(archived), keep))
        DeleteFileW((folder + stale).c_str());
}

void LogInit(const wchar_t* filePath, unsigned keepRuns)
{
    InitializeCriticalSection(&g_cs);
    // Keep the previous run's log as <name>.prev so a failed relaunch (which
    // truncates the log) doesn't erase the evidence from the run before it.
    g_directory.assign(filePath);
    const size_t slash = g_directory.find_last_of(L"\\/");
    g_directory = slash == std::wstring::npos ? std::wstring() : g_directory.substr(0, slash + 1);
    if (keepRuns)
        ArchivePreviousLog(filePath, keepRuns);
    std::wstring prev(filePath);
    prev += L".prev";
    // The rename fails while another process still holds the old log. A log
    // opened inheritable and without delete sharing is kept by the crash
    // reporter MCC spawns (which outlives MCC): every later rename then fails
    // silently, and .prev stays days old while each launch overwrites the
    // log -- the evidence of a crash goes with the relaunch.
    // Copy it then: the holder shares read.
    if (!MoveFileExW(filePath, prev.c_str(), MOVEFILE_REPLACE_EXISTING) &&
        GetLastError() != ERROR_FILE_NOT_FOUND)
        CopyFileW(filePath, prev.c_str(), FALSE);
    // Shared read/write/delete, so the log can be read live (and rotated) while
    // the game runs, and NOT inheritable, so no child process ever holds it.
    const HANDLE handle = CreateFileW(filePath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_TEXT);
    if (fd < 0)
    {
        CloseHandle(handle);
        return;
    }
    g_file = _fdopen(fd, "wt");
    if (!g_file)
        _close(fd);
}

void Logf(const char* fmt, ...)
{
    if (!g_file)
        return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_file, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
    LeaveCriticalSection(&g_cs);
}
