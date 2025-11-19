#ifndef TCORE_ARGPARSER_H
#define TCORE_ARGPARSER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <iomanip>

class ArgParser {
public:
    struct Option {
        bool takes_value = false;
        std::string long_name;
        char short_name = '\0';
        std::string help;
        std::string default_value;
        bool present = false;
        std::string value;
    };
    explicit ArgParser(std::string prog_name = "")
        : m_prog_name(std::move(prog_name)) {
            std::cout << m_prog_name << " running ..." << std::endl;
    }
    ~ArgParser();

public:
    void                            add_flag(char short_name, const std::string &long_name, const std::string &help);
    void                            add_option(char short_name, const std::string &long_name,
    const std::string &help, const std::string &default_value = "");

    void                            parse(int argc, char *argv[]);
    bool                            has(const std::string &long_name) const;
    bool                            has(char short_name) const;
    std::string                     get_string(const std::string &long_name) const;
    std::string                     get_string(char short_name) const;
    int                             get_int(const std::string &long_name) const;
    double                          get_double(const std::string &long_name) const;
    const std::vector<std::string> &get_positionals() const;
    void print_help(std::ostream &os = std::cout) const;

private:
    static std::string              make_label(const Option &o);
    void                            add_option_internal(const Option &o);
    void                            parse_long_option(const std::string &a, const char *next, int &idx);
    void                            parse_short_option(const std::string &a, const char *next, int &idx);
    const Option &                  get_option_by_long(const std::string &name) const;
    const Option &                  get_option_by_short(char s) const;

private:
    std::string                     m_prog_name;
    std::vector<Option>             m_options;
    std::vector<Option *>           m_option_order;
    std::unordered_map<std::string, Option *> m_long_map;
    std::unordered_map<char, Option *> m_short_map;
    std::vector<std::string>        m_positionals;
};

#endif
