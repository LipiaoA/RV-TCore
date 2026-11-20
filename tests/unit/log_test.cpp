#include <iterator>
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

    log.set_channel_root("logs");
    log.set_max_channels(2);
    auto alu = log.channel("alu");
    auto alu_again = GET_LOG_CH("alu");
    auto bru = log.channel("bru", "bru0.log");
    auto denied = log.channel("lsu");
    if (!alu || !bru || alu != alu_again || !alu->enabled() || !bru->enabled() || denied) {
        cerr << "channel registry check failed" << endl;
        return 1;
    }

    alu->writef("pc:0x%lx inst:%s\n", 0x80000000ul, "add");
    alu->writeln("----");
    bru->write("taken=1\n");
    log.flush();

    ifstream alu_in("logs/alu.log");
    string alu_text((istreambuf_iterator<char>(alu_in)), istreambuf_iterator<char>());
    ifstream bru_in("bru0.log");
    string bru_text((istreambuf_iterator<char>(bru_in)), istreambuf_iterator<char>());
    if (alu_text != "pc:0x80000000 inst:add\n----\n" || bru_text != "taken=1\n") {
        cerr << "channel file content mismatch" << endl;
        return 1;
    }

    log.close_channel("alu");
    if (alu->enabled() || !log.channel("lsu")) {
        cerr << "channel close/reuse check failed" << endl;
        return 1;
    }
    return 0;
}
