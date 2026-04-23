#include "logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::string& path, LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) m_file.close();
    m_level = level;
    m_file.open(path, std::ios::out | std::ios::trunc);
    m_initialized = m_file.is_open();
    if (m_initialized) {
        m_file << "=== G29 FFB Mod - NFSU2 ===\n";
        m_file << "Log started\n";
        m_file.flush();
    }
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

static const char* LevelStr(LogLevel l) {
    switch (l) {
        case LogLevel::ERROR_ONLY: return "ERROR";
        case LogLevel::INFO:       return "INFO ";
        case LogLevel::DEBUG:      return "DEBUG";
        default:                   return "?    ";
    }
}

void Logger::WriteEntry(LogLevel level, const char* fmt, va_list args) {
    if (!m_initialized || level > m_level) return;

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;

    std::tm tm_local{};
    localtime_s(&tm_local, &t);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_file << std::put_time(&tm_local, "%H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << ms.count()
           << " [" << LevelStr(level) << "] " << buf << '\n';
    m_file.flush();
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    WriteEntry(level, fmt, args);
    va_end(args);
}

void Logger::Error(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    WriteEntry(LogLevel::ERROR_ONLY, fmt, args);
    va_end(args);
}

void Logger::Info(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    WriteEntry(LogLevel::INFO, fmt, args);
    va_end(args);
}

void Logger::Debug(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    WriteEntry(LogLevel::DEBUG, fmt, args);
    va_end(args);
}

void Logger::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_file << "Log closed\n";
        m_file.close();
        m_initialized = false;
    }
}
