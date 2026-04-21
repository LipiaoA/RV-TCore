
#include "arg_parser.h"
#include "util.h"

void ArgParser::add_flag(char short_name, const std::string &long_name, const std::string &help)
{
	Option o;
	o.takes_value = false;
	o.long_name = long_name;
	o.short_name = short_name;
	o.help = help;
	add_option_internal(o);
}

void ArgParser::add_option(char short_name, const std::string &long_name,
				const std::string &help, const std::string &default_value)
{
	Option o;
	o.takes_value = true;
	o.long_name = long_name;
	o.short_name = short_name;
	o.help = help;
	o.default_value = default_value;
	o.value = default_value;
	add_option_internal(o);
}


void ArgParser::parse(int argc, char *argv[])
{
	if (argc > 0)
		m_prog_name = argv[0];
	m_positionals.clear();

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--") {
			for (int j = i + 1; j < argc; ++j)
				m_positionals.push_back(argv[j]);
			break;
		}
		if (a.rfind("--", 0) == 0) {
			parse_long_option(a, (i + 1 < argc) ? argv[i + 1] : nullptr, i);
		} else if (a.size() >= 2 && a[0] == '-' && a[1] != '\0') {
			parse_short_option(a, (i + 1 < argc) ? argv[i + 1] : nullptr, i);
		} else {
			m_positionals.push_back(a);
		}
	}
}

bool ArgParser::has(const std::string &long_name) const
{
	auto it = m_long_map.find(long_name);
	if (it == m_long_map.end())
		return false;
	return it->second->present;
}

bool ArgParser::has(char short_name) const
{
	auto it = m_short_map.find(short_name);
	if (it == m_short_map.end())
		return false;
	return it->second->present;
}

std::string ArgParser::get_string(const std::string &long_name) const
{
	return trim(get_option_by_long(long_name).value);
}

std::string ArgParser::get_string(char short_name) const
{
	return get_option_by_short(short_name).value;
}

int ArgParser::get_int(const std::string &long_name) const
{
	return stoi(get_string(long_name));
}

double ArgParser::get_double(const std::string &long_name) const
{
	return stod(get_string(long_name));
}

const std::vector<std::string> & ArgParser::get_positionals() const
{
	return m_positionals;
}

void ArgParser::print_help(std::ostream &os) const
{
	os << "Usage: " << m_prog_name << " [options]\n";
	os << "Options:\n";

	size_t width = 0;
	for (auto *opt : m_option_order) {
		std::string label = make_label(*opt);
		if (label.size() > width)
			width = label.size();
	}

	for (auto *opt : m_option_order) {
		std::string label = make_label(*opt);
		os << "  " << std::left << std::setw(static_cast<int>(width) + 2)
		   << label;
		os << opt->help;
		if (opt->takes_value && !opt->default_value.empty())
			os << " (defaul t: " << opt->default_value << ")";
		os << "\n";
	}
}



void ArgParser::add_option_internal(const Option &o)
{
	if (o.long_name.empty() && o.short_name == '\0')
		throw std::invalid_argument("option must have a name");

	m_options.push_back(o);
	Option *ptr = new Option(o);
	m_option_order.push_back(ptr);

	if (!o.long_name.empty()) {
		if (m_long_map.count(o.long_name))
			throw std::invalid_argument("duplicate long option: " + o.long_name);
		m_long_map[o.long_name] = ptr;
	}

	if (o.short_name != '\0') {
		if (m_short_map.count(o.short_name))
			throw std::invalid_argument(std::string("duplicate short option: -") + o.short_name);
		m_short_map[o.short_name] = ptr;
	}
}

std::string ArgParser::make_label(const Option &o)
{
	std::ostringstream ss;
	if (o.short_name != '\0') {
		ss << "-" << o.short_name;
		if (!o.long_name.empty())
			ss << ", ";
		else if (o.takes_value)
			ss << " ";
	} else {
		ss << "    ";
	}
	if (!o.long_name.empty())
		ss << "--" << o.long_name;
	if (o.takes_value)
		ss << " <value>";
	return ss.str();
}

void ArgParser::parse_long_option(const std::string &a, const char *next, int &idx)
{
	auto pos = a.find('=');
	std::string name = (pos == std::string::npos)
						   ? a.substr(2)
						   : a.substr(2, pos - 2);

	auto it = m_long_map.find(name);
	if (it == m_long_map.end())
		throw std::runtime_error("unknown option: --" + name);

	Option *opt = it->second;
	opt->present = true;

	if (opt->takes_value) {
		if (pos != std::string::npos) {
			opt->value = a.substr(pos + 1);
		} else {
			if (!next)
				throw std::runtime_error("option --" + name + " requires a value");
			opt->value = next;
			++idx;
		}
	}
}

void ArgParser::parse_short_option(const std::string &a, const char *next, int &idx)
{
	if (a.size() == 2) {
		char s = a[1];
		auto it = m_short_map.find(s);
		if (it == m_short_map.end())
			throw std::runtime_error(std::string("unknown option: -") + s);

		Option *opt = it->second;
		opt->present = true;

		if (opt->takes_value) {
			if (!next)
				throw std::runtime_error(std::string("option -") + s + " requires a value");
			opt->value = next;
			++idx;
		}
		return;
	}

	if (a[2] == '=') {
		char s = a[1];
		auto it = m_short_map.find(s);
		if (it == m_short_map.end())
			throw std::runtime_error(std::string("unknown option: -") + s);
		Option *opt = it->second;
		if (!opt->takes_value)
			throw std::runtime_error(std::string("option -") + s + " does not take a value");
		opt->present = true;
		opt->value = a.substr(3);
		return;
	}

	char first = a[1];
	auto it = m_short_map.find(first);
	if (it == m_short_map.end())
		throw std::runtime_error(std::string("unknown option: -") + first);

	Option *first_opt = it->second;
	if (first_opt->takes_value) {
		first_opt->present = true;
		first_opt->value = a.substr(2);
		return;
	}

	for (size_t i = 1; i < a.size(); ++i) {
		char c = a[i];
		auto it2 = m_short_map.find(c);
		if (it2 == m_short_map.end())
			throw std::runtime_error(std::string("unknown option: -") + c);

		Option *opt = it2->second;
		if (opt->takes_value) {
			if (i + 1 < a.size()) {
				opt->present = true;
				opt->value = a.substr(i + 1);
				break;
			} else {
				if (!next)
					throw std::runtime_error(std::string("option -") + c + " requires a value");
				opt->present = true;
				opt->value = next;
				++idx;
				break;
			}
		} else {
			opt->present = true;
		}
	}
}

const ArgParser::Option &ArgParser::get_option_by_long(const std::string &name) const
{
	auto it = m_long_map.find(name);
	if (it == m_long_map.end())
		throw std::runtime_error("unknown option: --" + name);
	return *(it->second);
}

const ArgParser::Option &ArgParser::get_option_by_short(char s) const
{
	auto it = m_short_map.find(s);
	if (it == m_short_map.end())
		throw std::runtime_error(std::string("unknown option: -") + s);
	return *(it->second);
}

ArgParser::~ArgParser() {
	for (auto &ptr : m_option_order) {
		delete_ptr(ptr);
	}
}
