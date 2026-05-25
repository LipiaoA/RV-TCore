#include <iostream>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>

#include "model.h"
#include "arg_parser.h"
#include "log.h"

u64 g_cycles = 0;
u64 g_max_cycles = 1000;

static Model *model;
static std::string elf_path = "";

static void signal_handler(int sig);
static bool args_init(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    // signal(SIGSEGV, signal_handler);
    if (argc < 2) {
        std::cout << "argument is missing ..." << std::endl;
        return 0;
    }
    if (!args_init(argc, argv)) {
        return 1;
    }

    model = new Model("top model");

    model->build();
    model->init(elf_path);
    model->run();

    delete model;
    return 0;
}

bool args_init(int argc, char *argv[])
{
    ArgParser parser(argv[0]);
    parser.add_flag('h', "help", "show help message");
    parser.add_option('l', "log", "log level", "0");
    parser.add_option('f', "file", "execute elf", "");

    try {
        parser.parse(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "Argument error: " << e.what() << "\n";
        parser.print_help();
        return false;
    }

    if (parser.has('h') || parser.has("help")) {
        parser.print_help();
        return false;
    }
    if (parser.has('l') || parser.has("log")) {
        int level = parser.get_int("log");
        if (level >= TOTAL_LEVEL || level < LEVEL1) {
            std::cout << "log level " << level << " is not supported!" << std::endl;
            return   1;
        }

        auto &log = LogSystem::instance();
        log.set_level(static_cast<LogLevel>(level));
        log.set_mode(LogMode::SYNC);
        log.add_writer(std::make_shared<FileSink>("system.log"));
    }

    if (parser.has('f') || parser.has("file")) {
        elf_path = parser.get_string("file");
        std::cout << "Execute: " << elf_path << std::endl;
    } else {
        std::cout << "the input file is missing!" << std::endl;
        return false;
    }
    return true;
}


static void signal_handler(int sig) {
    void *array[50];
    int size = backtrace(array, 50);

    fprintf(stderr, "segmentation fault: %d\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}