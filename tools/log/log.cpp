
#include "log.h"
#include <iomanip>

using namespace std;

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
        m_worker = thread([this]
                               { worker_loop(); });
    }
}

void LogSystem::add_writer(shared_ptr<LogOp> w)
{
    lock_guard<mutex> lock(m_sink_mutex);
    m_writer.push_back(move(w));
}

void LogSystem::flush()
{
    if (m_mode == LogMode::SYNC) {
        lock_guard<mutex> lock(m_sink_mutex);
        for (auto &logger : m_writer)
            logger->flush();
    } else {
        // 等待队列清空
        while (true)
        {
            lock_guard<mutex> lock(m_queue_mutex);
            if (m_queue.empty())
                break;
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        lock_guard<mutex> lock(m_sink_mutex);
        for (auto &logger : m_writer)
            logger->flush();
    }
}

void LogSystem::worker_loop()
{
    while (m_running) {
        unique_lock<mutex> lock(m_queue_mutex);
        m_cv.wait(lock, [this]
                  { return !m_queue.empty() || !m_running; });
        while (!m_queue.empty())
        {
            auto item = m_queue.front();
            m_queue.pop();
            lock.unlock();
            lock_guard<mutex> sink_lock(m_sink_mutex);
            for (auto &logger : m_writer)
                logger->write(item.msg, item.level);
            lock.lock();
        }
    }
    // 清空剩余消息
    while (true) {
        lock_guard<mutex> lock(m_queue_mutex);
        if (m_queue.empty())
            break;
        auto item = m_queue.front();
        m_queue.pop();
        lock_guard<mutex> sink_lock(m_sink_mutex);
        for (auto &logger : m_writer)
            logger->write(item.msg, item.level);
    }
}

string LogSystem::current_time()
{
    using namespace chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    ostringstream oss;
    tm tm_now;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    oss << put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
        << "." << setw(3) << setfill('0') << ms.count();
    return oss.str();
}
