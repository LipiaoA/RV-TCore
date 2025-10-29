#include "log.h"
#include <iomanip>

LogSystem::~LogSystem()
{
    if (m_mode == LogMode::ASYNC) {
        m_running = false;
        m_cv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }
}

void LogSystem::set_mode(LogMode mode)
{
    m_mode = mode;
    if (mode == LogMode::ASYNC && !m_worker.joinable()) {
        m_running = true;
        m_worker = std::thread([this]
                               { worker_loop(); });
    }
}

void LogSystem::add_logger(std::shared_ptr<LogOp> logger)
{
    std::lock_guard<std::mutex> lock(m_sink_mutex);
    m_logger.push_back(std::move(logger));
}

template <typename... Args>
void LogSystem::log(LogLevel level, const std::string &module, Args &&...args)
{
    if (level < m_level)
        return;

    std::ostringstream oss;
    (append_to_stream(oss, std::forward<Args>(args)), ...);

    std::ostringstream line;
    line << "[" << current_time() << "] "
         << "[" << level_n[level] << "] "
         << "[" << module << "] "
         << oss.str() << "\n";

    auto msg = line.str();

    if (m_mode == LogMode::SYNC) {
        std::lock_guard<std::mutex> lock(m_sink_mutex);
        for (auto &logger : m_logger)
            logger->write(msg, level);
    } else { // ASYNC
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_queue.push({level, msg});
        }
        m_cv.notify_one();
    }
}

void LogSystem::flush()
{
    if (m_mode == LogMode::SYNC) {
        std::lock_guard<std::mutex> lock(m_sink_mutex);
        for (auto &logger : m_logger)
            logger->flush();
    } else {
        // 等待队列清空
        while (true)
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_queue.empty())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::lock_guard<std::mutex> lock(m_sink_mutex);
        for (auto &logger : m_logger)
            logger->flush();
    }
}

void LogSystem::worker_loop()
{
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_cv.wait(lock, [this]
                  { return !m_queue.empty() || !m_running; });
        while (!m_queue.empty())
        {
            auto item = m_queue.front();
            m_queue.pop();
            lock.unlock();
            std::lock_guard<std::mutex> sink_lock(m_sink_mutex);
            for (auto &logger : m_logger)
                logger->write(item.msg, item.level);
            lock.lock();
        }
    }
    // 清空剩余消息
    while (true) {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_queue.empty())
            break;
        auto item = m_queue.front();
        m_queue.pop();
        std::lock_guard<std::mutex> sink_lock(m_sink_mutex);
        for (auto &logger : m_logger)
            logger->write(item.msg, item.level);
    }
}

template <typename T>
void LogSystem::append_to_stream(std::ostringstream &oss, T &&value)
{
    if constexpr (is_streamable_v<T>)
        oss << std::forward<T>(value);
    else
        oss << "<unprintable type: " << typeid(T).name() << ">";
}

std::string LogSystem::current_time()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    std::tm tm_now;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}
