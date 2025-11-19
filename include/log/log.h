#ifndef RV_TCORE_LOG_H
#define RV_TCORE_LOG_H

#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <queue>
#include <mutex>
#include <fstream>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>

typedef enum LogLevel { LEVEL1 = 0, LEVEL2, LEVEL3, LEVEL4, LEVEL5, LEVEL6, TOTAL_LEVEL } LogLevel;
enum LogMode { SYNC, ASYNC };

// 彩色输出 ANSI
static std::string level_color[LogLevel::TOTAL_LEVEL] = {"\033[37m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[41m"};
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
    static LogSystem &instance() {
        static LogSystem s_instance;
        return s_instance;
    }

    ~LogSystem();

    void set_level(LogLevel level) {
        m_level = level;
    }

    void set_mode(LogMode mode);

    void add_writer(std::shared_ptr<LogOp> w);

    template <typename Mut, typename... Args>
    void log(LogLevel level, const std::string &prefix, Mut mut, Args &&...args) {
        if (level > m_level)
            return;

        std::ostringstream line;

        line << "[" << current_time() << "] " << "[" << prefix << "] ";

        if constexpr (std::is_convertible_v<Mut, const char*>) {
            std::string formatted_msg = format_string(mut, std::forward<Args>(args)...);
            line << formatted_msg << "\n";
        } else {
            std::ostringstream oss;
            (append_to_stream(oss, std::forward<Mut>(mut)));

            line << oss.str() << "\n";
        }

        auto msg = line.str();

        if (m_mode == LogMode::SYNC) {
            std::lock_guard<std::mutex> lock(m_sink_mutex);
            for (auto &logger : m_writer)
                logger->write(msg, level);
        } else { // ASYNC
            {
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                m_queue.push({level, msg});
            }
            m_cv.notify_one();
        }
    }

    void flush();

private:
    LogSystem() = default;
    LogSystem(const LogSystem &) = delete;
    LogSystem &operator=(const LogSystem &) = delete;

    struct QueueItem {
        LogLevel level;
        std::string msg;
    };

    void worker_loop();

    template <typename T>
    void append_to_stream(std::ostringstream &oss, T &&value) {
        if constexpr (is_streamable_v<T>)
            oss << std::forward<T>(value);
        else
            oss << "<unprintable type: " << typeid(T).name() << ">";
    }
        // 格式化函数
    template <typename... Args>
    std::string format_string(const char* format, Args&&... args) {
        try {
            int size = std::snprintf(nullptr, 0, format, args...);
            if (size <= 0) return "[Format Error]";

            std::string result(size, '\0');
            std::snprintf(&result[0], size + 1, format, args...);
            return result;
        } catch (...) {
            return "[Format Exception]";
        }
    }

    std::string current_time();

private:
    LogLevel                                            m_level = LogLevel::LEVEL3;
    LogMode                                             m_mode = LogMode::SYNC;

    std::vector<std::shared_ptr<LogOp>>                 m_writer;
    std::mutex                                          m_sink_mutex;

    std::queue<QueueItem>                               m_queue;
    std::mutex                                          m_queue_mutex;
    std::condition_variable                             m_cv;
    std::atomic<bool>                                   m_running{false};
    std::thread                                         m_worker;
};

#define LOG_LEVEL1(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL1, prefix, __VA_ARGS__)
#define LOG_LEVEL2(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL2, prefix, __VA_ARGS__)
#define LOG_LEVEL3(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL3, prefix, __VA_ARGS__)
#define LOG_LEVEL4(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL4, prefix, __VA_ARGS__)
#define LOG_LEVEL5(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL5, prefix, __VA_ARGS__)
#define LOG_LEVEL6(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL6, prefix, __VA_ARGS__)

#endif