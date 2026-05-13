#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <Windows.h>

namespace csp::util
{

    class Logger
    {
    public:
        enum class LogLevel
        { Debug, Info, Warning, Error };
        using LogSink = std::function<void(const std::wstring& Line)>;

        static Logger& Instance();

        void SetLevel(const LogLevel InLevel) { Level = InLevel; }
        void SetSink(LogSink Sink);
        std::vector<std::wstring> History() const;

        void Debug(const wchar_t* fmt, ...);
        void Info(const wchar_t* fmt, ...);
        void Warning(const wchar_t* fmt, ...);
        void Error(const wchar_t* fmt, ...);

    private:
        Logger() = default;
        void Log(LogLevel Level, const wchar_t* Prefix, const wchar_t* Fmt, va_list Args);

        LogLevel Level = LogLevel::Info;
        LogSink SinkCallback;
        std::vector<std::wstring> Lines;
    };

    #define LOG_DEBUG(fmt, ...)   csp::util::Logger::Instance().Debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...)    csp::util::Logger::Instance().Info(fmt, ##__VA_ARGS__)
    #define LOG_WARNING(fmt, ...) csp::util::Logger::Instance().Warning(fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...)   csp::util::Logger::Instance().Error(fmt, ##__VA_ARGS__)

}
