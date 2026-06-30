#include <iostream>

#include "func/func_model.h"
#include "arg_parser.h"
#include "log.h"

static bool parse_args(int argc, char* argv[], std::string& elf_path, u64& max_inst)
{
    ArgParser parser(argv[0]);
    parser.add_flag('h', "help", "show help message");
    parser.add_option('f', "file", "execute elf", "");
    parser.add_option('n', "max-inst", "max instruction count", "1000000");
    parser.add_option('l', "log", "log level", "0");

    try {
        parser.parse(argc, argv);
    } catch (const std::exception& e) {
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
            return false;
        }
        auto& log = LogSystem::instance();
        log.set_level(static_cast<LogLevel>(level));
        log.set_mode(LogMode::SYNC);
        log.add_writer(std::make_shared<FileSink>("func.log"));
    }

    if (parser.has('f') || parser.has("file")) {
        elf_path = parser.get_string("file");
    } else {
        std::cout << "the input file is missing!" << std::endl;
        parser.print_help();
        return false;
    }

    max_inst = static_cast<u64>(parser.get_int("max-inst"));
    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "argument is missing ..." << std::endl;
        return 1;
    }

    std::string elf_path;
    u64 max_inst = 0;
    if (!parse_args(argc, argv, elf_path, max_inst)) {
        return 1;
    }

    std::cout << "Execute: " << elf_path << std::endl;

    FuncModel model;
    if (!model.init(elf_path)) {
        return 1;
    }
    model.run(max_inst);

    std::cout << "inst_cnt=" << model.inst_cnt()
              << " pc=0x" << std::hex << model.arch().pc
              << " a0=0x" << model.arch().gpr[10]
              << std::dec << std::endl;
    return 0;
}
