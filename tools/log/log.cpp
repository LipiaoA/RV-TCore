
#include "log.h"
#include <filesystem>
#include <iomanip>
#include "util.h"

using namespace std;

FileChannel::FileChannel(const string &path)
    : m_out(path, ios::out | ios::trunc)
{
}

FileChannel::~FileChannel()
{
    close();
}

bool FileChannel::enabled() const
{
    lock_guard<mutex> lock(m_mutex);
    return m_out.is_open();
}

void FileChannel::write(const string &raw)
{
    lock_guard<mutex> lock(m_mutex);
    if (m_out.is_open())
        m_out << raw;
}

void FileChannel::writeln(const string &raw)
{
    write(raw + "\n");
}

ostream &FileChannel::stream()
{
    return m_out;
}

void FileChannel::flush()
{
    lock_guard<mutex> lock(m_mutex);
    if (m_out.is_open())
        m_out.flush();
}

void FileChannel::close()
{
    lock_guard<mutex> lock(m_mutex);
    if (m_out.is_open()) {
        m_out.flush();
        m_out.close();
    }
}

LogSystem::~LogSystem()
{
    if (m_mode == LogMode::ASYNC) {
        m_running = false;
        m_cv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }
    close_all_channels();
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

void LogSystem::set_level(LogLevel level)
{
    m_level = level;
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
    flush_channels();
}

void LogSystem::set_channel_root(const string &dir)
{
    lock_guard<mutex> lock(m_channel_mutex);
    m_channel_root = dir.empty() ? "." : dir;
}

void LogSystem::set_max_channels(size_t n)
{
    lock_guard<mutex> lock(m_channel_mutex);
    m_max_channels = n;
}

shared_ptr<FileChannel> LogSystem::channel(const string &name, const string &path)
{
    lock_guard<mutex> lock(m_channel_mutex);
    auto it = m_channels.find(name);
    if (it != m_channels.end())
        return it->second;

    if (m_channels.size() >= m_max_channels)
        return nullptr;

    const string full = path.empty() ? join_channel_path(m_channel_root, name) : path;
    ensure_parent_dir(full);
    auto ch = make_shared<FileChannel>(full);
    if (!ch->enabled())
        return ch;

    m_channels.emplace(name, ch);
    return ch;
}

void LogSystem::close_channel(const string &name)
{
    shared_ptr<FileChannel> ch;
    {
        lock_guard<mutex> lock(m_channel_mutex);
        auto it = m_channels.find(name);
        if (it == m_channels.end())
            return;
        ch = std::move(it->second);
        m_channels.erase(it);
    }
    if (ch)
        ch->close();
}

void LogSystem::close_all_channels()
{
    unordered_map<string, shared_ptr<FileChannel>> channels;
    {
        lock_guard<mutex> lock(m_channel_mutex);
        channels.swap(m_channels);
    }
    for (auto &item : channels) {
        if (item.second)
            item.second->close();
    }
}

void LogSystem::flush_channels()
{
    vector<shared_ptr<FileChannel>> channels;
    {
        lock_guard<mutex> lock(m_channel_mutex);
        channels.reserve(m_channels.size());
        for (auto &item : m_channels)
            channels.push_back(item.second);
    }
    for (auto &ch : channels) {
        if (ch)
            ch->flush();
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
