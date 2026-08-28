#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <iostream>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERR,
    FATAL
};

struct LogEntry {
    LogLevel level;
    std::string message;
    std::string timestamp;
};

class Logger {
public:
    static Logger& Get() {
        static Logger instance;
        return instance;
    }

    void Init(const std::string& logFile = "editor.log");
    void Log(LogLevel level, const std::string& message);
    void Flush();

    // These are intentionally small diagnostics seams: callers can report the
    // actual path selected when the executable directory is not writable.
    bool IsOpen() const;
    std::string GetLogPath() const;
    
    const std::vector<LogEntry>& GetEntries() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_; 
    }
    
    void Clear() { 
        std::lock_guard<std::mutex> lock(mutex_); 
        entries_.clear(); 
    }

private:
    Logger() = default;
    ~Logger() {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    std::vector<LogEntry> entries_;
    std::ofstream file_;
    std::filesystem::path log_path_;
    int unflushed_lines_ = 0;
    mutable std::mutex mutex_;
};
