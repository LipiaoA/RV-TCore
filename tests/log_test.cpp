#include <thread>
#include <vector>

#include "log.h"

using namespace std;

struct CacheLine {
    uint64_t addr;
    bool valid;
    friend ostream& operator<<(ostream& os,const CacheLine& c){
        return os << "{addr=0x" << hex << c.addr << ", valid=" << boolalpha << c.valid << "}";
    }
};

int main(){
    auto& log = LogSystem::instance();
    log.set_level(LogLevel::LEVEL1);

    log.add_writer(make_shared<ConsoleSink>());
    log.add_writer(make_shared<FileSink>("system.log"));

    CacheLine line{0x1234,true};

    // 同步模式
    log.set_mode(LogMode::SYNC);
    LOG_LEVEL1(string("System"), string("Starting in synchronous mode"));

    // 异步模式
    log.set_mode(LogMode::ASYNC);
    LOG_LEVEL2("System","Starting in asynchronous mode");

    // 多线程日志
    vector<thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([t, &line]() {
            for(int i = 0; i < 5; i++) {
                LOG_LEVEL2("Thread"+to_string(t)," Iteration ",i," Line:",line);
            } });
    }
    for(auto& th: threads) th.join();

    log.flush();
}
