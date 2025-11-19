
#include "arg_parser.h"
#include <iostream>

int main(int argc, char *argv[])
{
    ArgParser parser(argv[0]);
    parser.add_flag('h', "help", "show help message");
    parser.add_flag('v', "verbose", "enable verbose mode");
    parser.add_option('o', "output", "output file path", "out.txt");
    parser.add_option('n', "num", "number of iterations", "10");
    parser.add_option('\0', "threshold", "threshold value", "0.5");

    try {
        parser.parse(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "Argument error: " << e.what() << "\n";
        parser.print_help();
        return 1;
    }

    if (parser.has('h') || parser.has("help")) {
        parser.print_help();
        return 0;
    }

    bool verbose = parser.has('v');
    std::string output = parser.get_string("output");
    int num = parser.has("num") ? parser.get_int("num") : 0;
    double thr = parser.get_double("threshold");

    std::cout << "verbose=" << verbose << "\n";
    std::cout << "output=" << output << "\n";
    std::cout << "num=" << num << "\n";
    std::cout << "threshold=" << thr << "\n";

    for (auto &p : parser.get_positionals())
        std::cout << "positional: " << p << "\n";

    return 0;
}
