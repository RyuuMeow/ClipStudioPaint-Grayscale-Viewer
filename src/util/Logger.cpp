#include "util/Logger.h"
#include <cstdio>
#include <mutex>

namespace csp::util
{
    static std::mutex g_logMutex;

    Logger& Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::Debug(const wchar_t* fmt, ...)
    {
        if (Level > LogLevel::Debug)
        {
            return;
        }
        va_list args;
        va_start(args, fmt);
        Log(LogLevel::Debug, L"[DEBUG]", fmt, args);
        va_end(args);
    }

    void Logger::Info(const wchar_t* fmt, ...)
    {
        if (Level > LogLevel::Info)
        {
            return;
        }
        va_list args;
        va_start(args, fmt);
        Log(LogLevel::Info, L"[INFO] ", fmt, args);
        va_end(args);
    }

    void Logger::Warning(const wchar_t* fmt, ...)
    {
        if (Level > LogLevel::Warning)
        {
            return;
        }
        va_list args;
        va_start(args, fmt);
        Log(LogLevel::Warning, L"[WARN] ", fmt, args);
        va_end(args);
    }

    void Logger::Error(const wchar_t* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Log(LogLevel::Error, L"[ERROR]", fmt, args);
        va_end(args);
    }

    void Logger::Log(LogLevel /*level*/, const wchar_t* Prefix, const wchar_t* Fmt, va_list Args)
    {
        wchar_t buf[1024];
        int n = _vsnwprintf_s(buf, _countof(buf) - 1, _TRUNCATE, Fmt, Args);
        if (n < 0)
        {
            n = static_cast<int>(wcslen(buf));
        }

        wchar_t line[1100];
        _snwprintf_s(line, _countof(line), _TRUNCATE, L"%s %s\n", Prefix, buf);

        std::lock_guard lock(g_logMutex);

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole && hConsole != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteConsoleW(hConsole, line, static_cast<DWORD>(wcslen(line)), &written, nullptr);
        }

        OutputDebugStringW(line);
    }

}
