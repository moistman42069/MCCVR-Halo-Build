#pragma once

// THE LAST RUNS' LOGS (log.cpp LogInit). Each launch used to keep one older log, as <name>.prev. MCC's and Project
// Reclaimer's launches start a short-lived first process that loads the mod too, so a relaunch moved the run before it
// out of .prev at once: a co-op run whose character ran in circles was gone before anyone looked (2026-11-07). Now the
// log is first copied into a Logs folder beside it, named for the time its run started, and the newest few are kept.
// Pure logic: the caller reads the files and the clock. Unit-tested in tests/core_tests.cpp.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace log_archive
{
struct Stamp
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

// Days since 1970-01-01 for a civil date, and back (proleptic Gregorian).
inline long long DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
inline void CivilFromDays(long long z, int& y, int& m, int& d)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2);
}

// When the run a log holds started: its first line's "[hh:mm:ss" on the day of `lastWrite` (the time of its last line),
// or on the day before when that time of day is later than the last write's (a run that went past midnight). False when
// the first line does not start with a time.
inline bool RunStart(const char* head, size_t length, const Stamp& lastWrite, Stamp& start)
{
    auto digits = [&](size_t at, int& value) {
        if (at + 2 > length || head[at] < '0' || head[at] > '9' || head[at + 1] < '0' || head[at + 1] > '9')
            return false;
        value = (head[at] - '0') * 10 + (head[at + 1] - '0');
        return true;
    };
    int h = 0, mi = 0, s = 0;
    if (!head || length < 9 || head[0] != '[' || head[3] != ':' || head[6] != ':' || !digits(1, h) ||
        !digits(4, mi) || !digits(7, s) || h > 23 || mi > 59 || s > 59)
        return false;
    start = lastWrite;
    start.hour = h;
    start.minute = mi;
    start.second = s;
    if (h * 3600 + mi * 60 + s > lastWrite.hour * 3600 + lastWrite.minute * 60 + lastWrite.second)
        CivilFromDays(DaysFromCivil(lastWrite.year, lastWrite.month, lastWrite.day) - 1, start.year, start.month,
                      start.day);
    return true;
}

// "<stem> yyyy-mm-dd hh-mm-ss.log": names that sort by time.
inline std::wstring ArchiveName(const std::wstring& stem, const Stamp& s)
{
    wchar_t when[32]{};
    swprintf(when, 32, L" %04d-%02d-%02d %02d-%02d-%02d.log", s.year, s.month, s.day, s.hour, s.minute, s.second);
    return stem + when;
}

// Of one stem's archived names (any order), those to delete so that the newest `keep` remain.
inline std::vector<std::wstring> ToPrune(std::vector<std::wstring> names, size_t keep)
{
    if (names.size() <= keep)
        return {};
    std::sort(names.begin(), names.end());
    names.resize(names.size() - keep);
    return names;
}
} // namespace log_archive
