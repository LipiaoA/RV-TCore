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
#include <iomanip>
#include <condition_variable>
#include <unordered_map>
#include <cstdio>

typedef enum LogLevel { LEVEL1 = 0, LEVEL2, LEVEL3, LEVEL4, LEVEL5, LEVEL6, TOTAL_LEVEL } LogLevel;
enum LogMode { SYNC, ASYNC };

// 彩色输出 ANSI
static std::string level_color[LogLevel::TOTAL_LEVEL] = {"\033[30m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[41m"};
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

template <typename... Args>
inline std::string format_printf(const char *format, Args &&...args)
{
    try {
        if constexpr (sizeof...(Args) == 0) {
            return std::string(format);
        } else {
            int size = std::snprintf(nullptr, 0, format, args...);
            if (size <= 0)
                return "[Format Error]";

            std::string result(size, '\0');
            std::snprintf(&result[0], size + 1, format, args...);
            return result;
        }
    } catch (...) {
        return "[Format Exception]";
    }
}

// 按模块持有的自由格式文件句柄，不经过 LogOp 广播
class FileChannel {
public:
    explicit FileChannel(const std::string &path);
    ~FileChannel();

    FileChannel(const FileChannel &) = delete;
    FileChannel &operator=(const FileChannel &) = delete;

    bool enabled() const;

    template <typename... Args>
    void writef(const char *fmt, Args &&...args)
    {
        write(format_printf(fmt, std::forward<Args>(args)...));
    }

    void write(const std::string &raw);
    void writeln(const std::string &raw);
    std::ostream &stream();
    void flush();
    void close();

private:
    mutable std::mutex m_mutex;
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

public:
    void                        set_level(LogLevel level);
    void                        set_mode(LogMode mode);
    void                        add_writer(std::shared_ptr<LogOp> w);
    void                        flush();

    void                        set_channel_root(const std::string &dir);
    void                        set_max_channels(std::size_t n);
    // 同名复用同一文件；path 为空时使用 root/name.log
    std::shared_ptr<FileChannel> channel(const std::string &name, const std::string &path = "");
    void                        close_channel(const std::string &name);
    void                        close_all_channels();
    template <typename Fmt, typename... Args>
    void                        write(Fmt fmt,  Args &&...args) {
        std::ostringstream line;
        std::string formatted_msg = format_string(fmt, std::forward<Args>(args)...);
        line << formatted_msg << "\n";
        for (auto &logger : m_writer)
            logger->write(line.str(), LEVEL1);
    }

    template <typename Mut, typename... Args>
    void log(LogLevel level, const std::string &prefix, Mut mut, Args &&...args) {
        if (level > m_level)
            return;

        std::ostringstream line;

        // line << "[" << current_time() << "] ";
        // line << "[" << prefix << "] ";
        line << "[" << std::left << std::setw(16) << prefix << "] ";

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



private:
    LogSystem() = default;
    LogSystem(const LogSystem &) = delete;
    LogSystem &operator=(const LogSystem &) = delete;

    struct QueueItem {
        LogLevel level;
        std::string msg;
    };

    void worker_loop();
    void flush_channels();

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
            return format_printf(format, std::forward<Args>(args)...);
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

    std::string                                         m_channel_root{"."};
    std::size_t                                         m_max_channels{16};
    std::mutex                                          m_channel_mutex;
    std::unordered_map<std::string, std::shared_ptr<FileChannel>> m_channels;
};

#define LOG_LEVEL1(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL1, prefix, __VA_ARGS__)
#define LOG_LEVEL2(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL2, prefix, __VA_ARGS__)
#define LOG_LEVEL3(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL3, prefix, __VA_ARGS__)
#define LOG_LEVEL4(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL4, prefix, __VA_ARGS__)
#define LOG_LEVEL5(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL5, prefix, __VA_ARGS__)
#define LOG_LEVEL6(prefix, ...) LogSystem::instance().log(LogLevel::LEVEL6, prefix, __VA_ARGS__)

#define GET_LOG_CH(name) LogSystem::instance().channel(name)

#endif