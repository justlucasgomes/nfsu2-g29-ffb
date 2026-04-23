#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <cstdarg>

enum class LogLevel { NONE = 0, ERROR_ONLY = 1, INFO = 2, DEBUG = 3 };

class Logger {
public:
    static Logger& Get();

    void Init(const std::string& path, LogLevel level = LogLevel::INFO);
    void SetLevel(LogLevel level);
    void Log(LogLevel level, const char* fmt, ...);
    void Close();

    void Error(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Debug(const char* fmt, ...);

private:
    Logger() = default;
    ~Logger() { Close(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void WriteEntry(LogLevel level, const char* fmt, va_list args);

    std::ofstream  m_file;
    LogLevel       m_level = LogLevel::INFO;
    std::mutex     m_mutex;
    bool           m_initialized = false;
};

#define LOG_ERROR(...) Logger::Get().Error(__VA_ARGS__)
#define LOG_INFO(...)  Logger::Get().Info(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::Get().Debug(__VA_ARGS__)
