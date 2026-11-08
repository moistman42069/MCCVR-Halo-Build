#include "vehicle_camera_store.h"

#include <windows.h>

#include <utility>

namespace vehicle_camera_store
{
namespace
{
using vehicle_camera_file::Table;

class File
{
public:
    explicit File(HANDLE handle) noexcept : handle_(handle) {}
    ~File()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool Open() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE Get() const noexcept { return handle_; }

private:
    HANDLE handle_;
};

std::string ErrorText(DWORD error)
{
    return "Windows error " + std::to_string(static_cast<unsigned long>(error));
}

std::string Prefix()
{
    return std::string("vehicle cameras: ") + kFileNameText;
}
} // namespace

Store::Store(std::wstring path) : path_(std::move(path)) {}

Store::Look Store::Stat(Stamp& out) const
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data))
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Look::Missing : Look::Unreadable;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return Look::Unreadable;
    out.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                  data.ftLastWriteTime.dwLowDateTime;
    out.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return Look::Found;
}

// Writes the built-in table as a new file. CREATE_NEW: a file that appeared in the meantime is left alone.
bool Store::Create(std::string& why) const
{
    std::string text;
    for (const char c : vehicle_camera_file::ToJson(vehicle_camera_file::DefaultTable()))
    {
        if (c == '\n')
            text += '\r';
        text += c;
    }
    const File file(
        CreateFileW(path_.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.Open())
    {
        why = ErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool whole =
        WriteFile(file.Get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    if (!whole)
    {
        // A part-written file would be the player's from now on and never be put right: take it away again.
        why = ErrorText(GetLastError());
        FILE_DISPOSITION_INFO gone{};
        gone.DeleteFile = TRUE;
        SetFileInformationByHandle(file.Get(), FileDispositionInfo, &gone, sizeof(gone));
        return false;
    }
    return true;
}

// The whole file, or as much of an oversized one as shows that it is oversized. Shared with writers: an editor
// saving at the same moment is not blocked, and a half-written read is caught as a syntax error and read again
// when the file's time moves.
bool Store::ReadAll(std::string& out, std::string& why) const
{
    const File file(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.Open())
    {
        why = ErrorText(GetLastError());
        return false;
    }
    out.clear();
    char block[16384];
    while (out.size() <= vehicle_camera_file::kMaxFileBytes)
    {
        DWORD got = 0;
        if (!ReadFile(file.Get(), block, sizeof(block), &got, nullptr))
        {
            why = ErrorText(GetLastError());
            return false;
        }
        if (got == 0)
            break;
        out.append(block, got);
    }
    return true;
}

bool Store::Add(const vehicle_camera_autoadd::Addition& addition, const char* gameKey, const std::string& vehicleName,
                uint8_t seat, std::string& why)
{
    std::string text;
    if (!ReadAll(text, why))
        return false;
    if (text.size() > vehicle_camera_file::kMaxFileBytes)
    {
        why = "the file is larger than 512 KB";
        return false;
    }
    std::string bom;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        bom = text.substr(0, 3);
        text.erase(0, 3);
    }
    if (!vehicle_camera_autoadd::Insert(text, addition, gameKey, vehicleName, seat, why))
        return false;
    text.insert(0, bom);

    // The player's file as it was before the mod first added to it, kept once (an existing copy is never replaced).
    const std::wstring backup = path_ + kBackupSuffix;
    CopyFileW(path_.c_str(), backup.c_str(), TRUE);

    // Written whole to a file beside it, then put in its place in one step: a reader never sees half of it.
    const std::wstring temp = path_ + L".adding";
    {
        const File file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr));
        if (!file.Open())
        {
            why = ErrorText(GetLastError());
            return false;
        }
        DWORD written = 0;
        if (!WriteFile(file.Get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size() || !FlushFileBuffers(file.Get()))
        {
            why = ErrorText(GetLastError());
            FILE_DISPOSITION_INFO gone{};
            gone.DeleteFile = TRUE;
            SetFileInformationByHandle(file.Get(), FileDispositionInfo, &gone, sizeof(gone));
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        why = ErrorText(GetLastError());
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

PollResult Store::Poll()
{
    PollResult result;
    Stamp stamp{};
    Look look = Stat(stamp);
    if (look == Look::Missing)
    {
        std::string why;
        if (Create(why))
        {
            result.created = true;
            createFailureNoted_ = false;
            result.notes.push_back(Prefix() + " was not there, so the built-in cameras were written to it");
            look = Stat(stamp);
        }
        else if (!createFailureNoted_)
        {
            createFailureNoted_ = true;
            result.notes.push_back(Prefix() + " is not there and could not be written (" + why +
                                   "); the built-in cameras are in use");
        }
    }
    if (look != Look::Found)
    {
        // Nothing to read. A table read earlier from a file that is now gone gives way to the built-in one.
        if (look == Look::Unreadable && !readFailureNoted_)
        {
            readFailureNoted_ = true;
            result.notes.push_back(Prefix() + " could not be looked at; the cameras in use are unchanged");
        }
        if (look == Look::Missing && fromFile_)
        {
            table_ = vehicle_camera_file::DefaultTable();
            fromFile_ = false;
            haveSeen_ = false;
            result.changed = true;
        }
        return result;
    }
    if (haveSeen_ && stamp == seen_)
        return result;

    std::string text;
    std::string why;
    if (!ReadAll(text, why))
    {
        // Held by something else this moment: left unseen, so the next Poll tries again.
        if (!readFailureNoted_)
        {
            readFailureNoted_ = true;
            result.notes.push_back(Prefix() + " could not be read (" + why + "); it will be tried again");
        }
        return result;
    }
    readFailureNoted_ = false;
    haveSeen_ = true;
    seen_ = stamp;

    const char* start = text.data();
    std::size_t length = text.size();
    if (length >= 3 && static_cast<unsigned char>(start[0]) == 0xEF && static_cast<unsigned char>(start[1]) == 0xBB &&
        static_cast<unsigned char>(start[2]) == 0xBF)
    {
        start += 3;
        length -= 3;
    }
    Table read;
    const vehicle_camera_file::ParseResult parsed = vehicle_camera_file::FromJson(start, length, read);
    if (!parsed.ok)
    {
        result.notes.push_back(Prefix() + " was not applied: " + parsed.error + ". The cameras in use are unchanged");
        return result;
    }
    table_ = std::move(read);
    fromFile_ = true;
    result.changed = true;
    result.notes.push_back(Prefix() + ": read " + std::to_string(parsed.vehicles) + " vehicle entries and " +
                           std::to_string(parsed.cameras) + " cameras" +
                           (parsed.leftOut.empty()
                                ? std::string()
                                : "; " + std::to_string(parsed.leftOut.size()) +
                                      " part(s) of it could not be read and were left out, the rest applies:"));
    // What a slip cost, each by its path and line: the rest of the file is in use.
    constexpr std::size_t kLeftOutLines = 12;
    for (std::size_t i = 0; i < parsed.leftOut.size() && i < kLeftOutLines; ++i)
        result.notes.push_back(Prefix() + ": " + parsed.leftOut[i]);
    if (parsed.leftOut.size() > kLeftOutLines)
        result.notes.push_back(Prefix() + ": and " + std::to_string(parsed.leftOut.size() - kLeftOutLines) + " more");
    for (const std::string& key : parsed.ignored)
        result.notes.push_back(Prefix() + ": \"" + key + "\" is not a key this version reads; it was skipped");
    return result;
}
} // namespace vehicle_camera_store
