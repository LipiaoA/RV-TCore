// log_level.h
#pragma once
#include <string>

enum class LogLevel {
    LEVEL1 = 0,
    LEVEL2,
    LEVEL3,
    LEVEL4,
    LEVEL5,
    LEVEL6,
	TOTAL_LEVEL
};

const std::string level_n[TOTAL_LEVEL] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

enum class LogLevel {
    TRACE=0, DEBUG, INFO, WARN, ERROR, FATAL
};

enum class LogMode { SYNC, ASYNC };

// 彩色输出 ANSI
inline std::string level_color(LogLevel level){
    switch(level){
        case LogLevel::TRACE: return "\033[37m";
        case LogLevel::DEBUG: return "\033[36m";
        case LogLevel::INFO:  return "\033[32m";
        case LogLevel::WARN:  return "\033[33m";
        case LogLevel::ERROR: return "\033[31m";
        case LogLevel::FATAL: return "\033[41m";
        default: return "\033[0m";
    }
}
inline std::string color_reset(){ return "\033[0m"; }


class LogSink{
public:
    virtual ~LogSink()=default;
    virtual void write(const std::string& formatted_msg, LogLevel level)=0;
    virtual void flush(){}
};

class ConsoleSink : public LogSink{
public:
    void write(const std::string& formatted_msg, LogLevel level) override{
        std::cout << level_color(level) << formatted_msg << color_reset();
    }
    void flush() override{ std::cout.flush(); }
};

class FileSink : public LogSink{
public:
    explicit FileSink(const std::string& filename)
        : m_out(filename,std::ios::out|std::ios::trunc) {}
    void write(const std::string& formatted_msg, LogLevel) override{
        if(m_out.is_open()) m_out << formatted_msg;
    }
    void flush() override{ if(m_out.is_open()) m_out.flush(); }
private:
    std::ofstream m_out;
};

template<typename T, typename=void>
struct is_streamable : std::false_type{};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>()<<std::declval<T>())>>
    : std::true_type{};

template<typename T>
inline constexpr bool is_streamable_v = is_streamable<T>::value;


class LogSystem{
public:
    static LogSystem& instance(){
        static LogSystem s_instance;
        return s_instance;
    }

    ~LogSystem(){
        if(m_mode == LogMode::ASYNC){
            m_running = false;
            m_cv.notify_all();
            if(m_worker.joinable()) m_worker.join();
        }
    }

    void set_level(LogLevel level){ m_level = level; }

    void set_mode(LogMode mode){
        m_mode = mode;
        if(mode == LogMode::ASYNC && !m_worker.joinable()){
            m_running = true;
            m_worker = std::thread([this]{ worker_loop(); });
        }
    }

    void add_sink(std::shared_ptr<LogSink> sink){
        std::lock_guard<std::mutex> lock(m_sink_mutex);
        m_sinks.push_back(std::move(sink));
    }

    template<typename... Args>
    void log(LogLevel level, const std::string& module, Args&&... args){
        if(level < m_level) return;

        std::ostringstream oss;
        (append_to_stream(oss,std::forward<Args>(args)),...);

        std::ostringstream line;
        line << "[" << current_time() << "] "
             << "[" << level_n[level] << "] "
             << "[" << module << "] "
             << oss.str() << "\n";

        auto msg = line.str();

        if(m_mode == LogMode::SYNC){
            std::lock_guard<std::mutex> lock(m_sink_mutex);
            for(auto& sink : m_sinks) sink->write(msg, level);
        } else { // ASYNC
            {
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                m_queue.push({level,msg});
            }
            m_cv.notify_one();
        }
    }

    void flush(){
        if(m_mode == LogMode::SYNC){
            std::lock_guard<std::mutex> lock(m_sink_mutex);
            for(auto& sink: m_sinks) sink->flush();
        } else {
            // 等待队列清空
            while(true){
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                if(m_queue.empty()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::lock_guard<std::mutex> lock(m_sink_mutex);
            for(auto& sink: m_sinks) sink->flush();
        }
    }

private:
    LogSystem() = default;
    LogSystem(const LogSystem&) = delete;
    LogSystem& operator=(const LogSystem&) = delete;

    struct QueueItem{ LogLevel level; std::string msg; };

    void worker_loop(){
        while(m_running){
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_cv.wait(lock,[this]{ return !m_queue.empty() || !m_running; });
            while(!m_queue.empty()){
                auto item = m_queue.front(); m_queue.pop();
                lock.unlock();
                std::lock_guard<std::mutex> sink_lock(m_sink_mutex);
                for(auto& sink: m_sinks) sink->write(item.msg,item.level);
                lock.lock();
            }
        }
        // 清空剩余消息
        while(true){
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if(m_queue.empty()) break;
            auto item = m_queue.front(); m_queue.pop();
            std::lock_guard<std::mutex> sink_lock(m_sink_mutex);
            for(auto& sink: m_sinks) sink->write(item.msg,item.level);
        }
    }

    template<typename T>
    void append_to_stream(std::ostringstream& oss, T&& value){
        if constexpr(is_streamable_v<T>)
            oss << std::forward<T>(value);
        else
            oss << "<unprintable type: " << typeid(T).name() << ">";
    }

    std::string current_time(){
        using namespace std::chrono;
        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        std::tm tm_now;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm_now,&t);
#else
        localtime_r(&t,&tm_now);
#endif
        oss << std::put_time(&tm_now,"%Y-%m-%d %H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

private:
    LogLevel m_level = LogLevel::INFO;
    LogMode  m_mode = LogMode::SYNC;

    std::vector<std::shared_ptr<LogSink>> m_sinks;
    std::mutex m_sink_mutex;

    std::queue<QueueItem> m_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
    std::thread m_worker;
};

// ===============================
// 宏封装接口
// ===============================
#define LOG_TRACE(module, ...) LogSystem::instance().log(LogLevel::TRACE,module,__VA_ARGS__)
#define LOG_DEBUG(module, ...) LogSystem::instance().log(LogLevel::DEBUG,module,__VA_ARGS__)
#define LOG_INFO(module,  ...) LogSystem::instance().log(LogLevel::INFO,module,__VA_ARGS__)
#define LOG_WARN(module,  ...) LogSystem::instance().log(LogLevel::WARN,module,__VA_ARGS__)
#define LOG_ERROR(module, ...) LogSystem::instance().log(LogLevel::ERROR,module,__VA_ARGS__)
#define LOG_FATAL(module, ...) LogSystem::instance().log(LogLevel::FATAL,module,__VA_ARGS__)
