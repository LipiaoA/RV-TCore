
#include "arg_parser.h"

void ArgParser::add_flag(char short_name, const std::string &long_name, const std::string &help)
{
	Option o;
	o.m_takes_value = false;
	o.m_long_name = long_name;
	o.m_short_name = short_name;
	o.m_help = help;
	add_option_internal(o);
}

void ArgParser::add_option(char short_name, const std::string &long_name,
				const std::string &help, const std::string &default_value = "")
{
	Option o;
	o.m_takes_value = true;
	o.m_long_name = long_name;
	o.m_short_name = short_name;
	o.m_help = help;
	o.m_default_value = default_value;
	o.m_value = default_value;
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
	return it->second->m_present;
}

bool ArgParser::has(char short_name) const
{
	auto it = m_short_map.find(short_name);
	if (it == m_short_map.end())
		return false;
	return it->second->m_present;
}

std::string ArgParser::get_string(const std::string &long_name) const
{
	return get_option_by_long(long_name).m_value;
}

std::string ArgParser::get_string(char short_name) const
{
	return get_option_by_short(short_name).m_value;
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

void ArgParser::print_help(std::ostream &os = std::cout) const
{
	os << "Usage: " << m_prog_name << " [options]\n\n";
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
		os << opt->m_help;
		if (opt->m_takes_value && !opt->m_default_value.empty())
			os << " (default: " << opt->m_default_value << ")";
		os << "\n";
	}
}



void ArgParser::add_option_internal(const Option &o)
{
	if (o.m_long_name.empty() && o.m_short_name == '\0')
		throw std::invalid_argument("option must have a name");

	m_options.push_back(o);
	Option *ptr = &m_options.back();
	m_option_order.push_back(ptr);

	if (!o.m_long_name.empty()) {
		if (m_long_map.count(o.m_long_name))
			throw std::invalid_argument("duplicate long option: " + o.m_long_name);
		m_long_map[o.m_long_name] = ptr;
	}

	if (o.m_short_name != '\0') {
		if (m_short_map.count(o.m_short_name))
			throw std::invalid_argument(std::string("duplicate short option: -") + o.m_short_name);
		m_short_map[o.m_short_name] = ptr;
	}
}

static std::string ArgParser::make_label(const Option &o)
{
	std::ostringstream ss;
	if (o.m_short_name != '\0') {
		ss << "-" << o.m_short_name;
		if (!o.m_long_name.empty())
			ss << ", ";
		else if (o.m_takes_value)
			ss << " ";
	} else {
		ss << "    ";
	}
	if (!o.m_long_name.empty())
		ss << "--" << o.m_long_name;
	if (o.m_takes_value)
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
	opt->m_present = true;

	if (opt->m_takes_value) {
		if (pos != std::string::npos) {
			opt->m_value = a.substr(pos + 1);
		} else {
			if (!next)
				throw std::runtime_error("option --" + name + " requires a value");
			opt->m_value = next;
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
		opt->m_present = true;

		if (opt->m_takes_value) {
			if (!next)
				throw std::runtime_error(std::string("option -") + s + " requires a value");
			opt->m_value = next;
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
		if (!opt->m_takes_value)
			throw std::runtime_error(std::string("option -") + s + " does not take a value");
		opt->m_present = true;
		opt->m_value = a.substr(3);
		return;
	}

	char first = a[1];
	auto it = m_short_map.find(first);
	if (it == m_short_map.end())
		throw std::runtime_error(std::string("unknown option: -") + first);

	Option *first_opt = it->second;
	if (first_opt->m_takes_value) {
		first_opt->m_present = true;
		first_opt->m_value = a.substr(2);
		return;
	}

	for (size_t i = 1; i < a.size(); ++i) {
		char c = a[i];
		auto it2 = m_short_map.find(c);
		if (it2 == m_short_map.end())
			throw std::runtime_error(std::string("unknown option: -") + c);

		Option *opt = it2->second;
		if (opt->m_takes_value) {
			if (i + 1 < a.size()) {
				opt->m_present = true;
				opt->m_value = a.substr(i + 1);
				break;
			} else {
				if (!next)
					throw std::runtime_error(std::string("option -") + c + " requires a value");
				opt->m_present = true;
				opt->m_value = next;
				++idx;
				break;
			}
		} else {
			opt->m_present = true;
		}
	}
}

const Option &ArgParser::get_option_by_long(const std::string &name) const
{
	auto it = m_long_map.find(name);
	if (it == m_long_map.end())
		throw std::runtime_error("unknown option: --" + name);
	return *(it->second);
}

const Option &ArgParser::get_option_by_short(char s) const
{
	auto it = m_short_map.find(s);
	if (it == m_short_map.end())
		throw std::runtime_error(std::string("unknown option: -") + s);
	return *(it->second);
}

