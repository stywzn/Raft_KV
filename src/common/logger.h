#pragma once

#include <mutex>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace raftkv {

enum LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    // 单例模式：保证全局只有一个 Logger
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void SetLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    // 核心写日志函数
    void Log(LogLevel level, const char* file, int line, const std::string& msg) {
        if (level < level_) return;

        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::lock_guard<std::mutex> lock(mutex_); 
        
        std::cout << "[" << std::put_time(std::localtime(&now_time), "%H:%M:%S") 
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                  << "[" << LevelToString(level) << "] "
                  << "[" << file << ":" << line << "] "
                  << msg << std::endl;
    }

private:
    // 默认开启 DEBUG，方便调试
    Logger() : level_(DEBUG) {} 
    
    std::string LevelToString(LogLevel level) {
        switch(level) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO "; 
            case WARN:  return "WARN ";
            case ERROR: return "ERROR";
            default:    return "UNKNOWN";
        }
    }

    std::mutex mutex_;
    LogLevel level_;
};

} // namespace raftkv

// 定义宏
#define LOG_DEBUG(msg) { \
    std::stringstream ss; ss << msg; \
    raftkv::Logger::GetInstance().Log(raftkv::DEBUG, __FILE__, __LINE__, ss.str()); \
}

#define LOG_INFO(msg) { \
    std::stringstream ss; ss << msg; \
    raftkv::Logger::GetInstance().Log(raftkv::INFO, __FILE__, __LINE__, ss.str()); \
}

#define LOG_WARN(msg) { \
    std::stringstream ss; ss << msg; \
    raftkv::Logger::GetInstance().Log(raftkv::WARN, __FILE__, __LINE__, ss.str()); \
}

#define LOG_ERROR(msg) { \
    std::stringstream ss; ss << msg; \
    raftkv::Logger::GetInstance().Log(raftkv::ERROR, __FILE__, __LINE__, ss.str()); \
}