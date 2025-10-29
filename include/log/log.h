#ifndef RV_TCORE_LOG_H
#define RV_TCORE_LOG_H

#include <string>
#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include <fstream>
#include <memory>
#include <thread>
#include <condition_variable>


typedef enum LogLevel { LEVEL1 = 0, LEVEL2, LEVEL3, LEVEL4, LEVEL5, LEVEL6, TOTAL_LEVEL } LogLevel;
enum LogMode { SYNC, ASYNC };

// 彩色输出 ANSI
std::string level_color[LogLevel::TOTAL_LEVEL] = {"\033[37m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[41m"};
inline std::string color_reset(){ return "\033[0m"; }

class LogOp {
public:
    virtual ~LogOp() = default;
    virtual void write(const std::string &msg, LogLevel level) = 0;
    virtual void flush() {}
};

class ConsoleSink : public LogOp {
public:
    void write(const std::string &msg, LogLevel level) override
    {
        std::cout << level_color[level] << msg << color_reset();
    }
    void flush() override { std::cout.flush(); }
};

class FileSink : public LogOp {
public:
    explicit FileSink(const std::string &filename)
        : m_out(filename, std::ios::out | std::ios::trunc) {}
    void write(const std::string &msg, LogLevel) override
    {
        if (m_out.is_open())
            m_out << msg;
    }
    void flush() override
    {
        if (m_out.is_open())
            m_out.flush();
    }

private:
    std::ofstream m_out;
};

template <typename T, typename = void>
struct is_streamable : std::false_type {};

template <typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream &>() << std::declval<T>())>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_streamable_v = is_streamable<T>::value;

class LogSystem {
public:
    static LogSystem &instance()
    {
        static LogSystem s_instance;
        return s_instance;
    }

    ~LogSystem();

    void set_level(LogLevel level) { m_level = level; }

    void set_mode(LogMode mode);

    void add_logger(std::shared_ptr<LogOp> sink);

    template <typename... Args>
    void log(LogLevel level, const std::string &prefix, Args &&...args);

    void flush();

private:
    LogSystem() = default;
    LogSystem(const LogSystem &) = delete;
    LogSystem &operator=(const LogSystem &) = delete;

    struct QueueItem
    {
        LogLevel level;
        std::string msg;
    };

    void worker_loop();

    template <typename T>
    void append_to_stream(std::ostringstream &oss, T &&value);

    std::string current_time();

private:
    LogLevel m_level = LogLevel::LEVEL3;
    LogMode m_mode = LogMode::SYNC;

    std::vector<std::shared_ptr<LogOp>> m_logger;
    std::mutex m_sink_mutex;

    std::queue<QueueItem> m_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
    std::thread m_worker;
};

#define LOG_TRACE(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL1, prefix, __VA_ARGS__)
#define LOG_DEBUG(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL2, prefix, __VA_ARGS__)
#define LOG_INFO(prefix,  ...) LogSystem::instance().log(LogLevel::LEVEL3, prefix, __VA_ARGS__)
#define LOG_WARN(prefix,  ...) LogSystem::instance().log(LogLevel::LEVEL4, prefix, __VA_ARGS__)
#define LOG_ERROR(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL5, prefix, __VA_ARGS__)
#define LOG_FATAL(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL6, prefix, __VA_ARGS__)

#endif