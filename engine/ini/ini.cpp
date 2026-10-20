#include "ini/ini.h"

#include <cerrno>
#include <cstdlib>

namespace ra::ini {

std::string to_lower(std::string_view text)
{
	std::string r(text);
	for (char& c : r) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return r;
}

std::string_view trim(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
		text.remove_suffix(1);
	return text;
}

const std::string* IniSection::find(std::string_view key) const
{
	auto it = index.find(to_lower(key));
	return it == index.end() ? nullptr : &entries[it->second].second;
}

void IniSection::set(std::string_view key, std::string_view value)
{
	const std::string lower = to_lower(key);
	auto it = index.find(lower);
	if (it != index.end()) {
		entries[it->second].second = std::string(value);
	} else {
		index.emplace(lower, entries.size());
		entries.emplace_back(std::string(key), std::string(value));
	}
}

IniSection& IniFile::section_mut(std::string_view name)
{
	const std::string lower = to_lower(name);
	auto it = index_.find(lower);
	if (it != index_.end())
		return sections_[it->second];
	index_.emplace(lower, sections_.size());
	sections_.emplace_back();
	sections_.back().name = std::string(name);
	return sections_.back();
}

IniFile IniFile::parse(std::string_view text)
{
	IniFile ini;
	IniSection* current = nullptr;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view line = text.substr(pos, end - pos);
		pos = end + 1;

		if (size_t semi = line.find(';'); semi != std::string_view::npos)
			line = line.substr(0, semi);
		line = trim(line);
		if (line.empty())
			continue;

		if (line.front() == '[') {
			const size_t close = line.find(']');
			if (close != std::string_view::npos) {
				current = &ini.section_mut(trim(line.substr(1, close - 1)));
				continue;
			}
		}
		if (!current)
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string_view::npos)
			continue;
		const std::string_view key = trim(line.substr(0, eq));
		if (key.empty())
			continue;
		current->set(key, trim(line.substr(eq + 1)));
	}
	return ini;
}

void IniFile::merge(const IniFile& other)
{
	for (const IniSection& s : other.sections_) {
		IniSection& dest = section_mut(s.name);
		for (const auto& [key, value] : s.entries)
			dest.set(key, value);
	}
}

const IniSection* IniFile::section(std::string_view name) const
{
	auto it = index_.find(to_lower(name));
	return it == index_.end() ? nullptr : &sections_[it->second];
}

const std::string* IniFile::get(std::string_view section_name, std::string_view key) const
{
	const IniSection* s = section(section_name);
	return s ? s->find(key) : nullptr;
}

bool parse_int(std::string_view text, int& out)
{
	text = trim(text);
	if (text.empty())
		return false;
	std::string s(text);
	int base = 10;
	size_t start = 0;
	bool negative = false;
	if (s[0] == '-' || s[0] == '+') {
		negative = s[0] == '-';
		start = 1;
	}
	if (start < s.size() && s[start] == '$') {
		base = 16;
		++start;
	}
	errno = 0;
	char* end = nullptr;
	const long v = std::strtol(s.c_str() + start, &end, base);
	if (end == s.c_str() + start)
		return false;
	out = static_cast<int>(negative ? -v : v);
	return true;
}

bool parse_double(std::string_view text, double& out)
{
	text = trim(text);
	if (text.empty())
		return false;
	std::string s(text);
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str())
		return false;
	out = v;
	return true;
}

bool parse_bool(std::string_view text, bool& out)
{
	text = trim(text);
	if (text.empty())
		return false;
	switch (text.front()) {
	case 'Y': case 'y': case 'T': case 't': case '1':
		out = true;
		return true;
	case 'N': case 'n': case 'F': case 'f': case '0':
		out = false;
		return true;
	default:
		return false;
	}
}

std::string IniFile::get_string(std::string_view section_name, std::string_view key, std::string_view fallback) const
{
	const std::string* v = get(section_name, key);
	return v ? *v : std::string(fallback);
}

int IniFile::get_int(std::string_view section_name, std::string_view key, int fallback) const
{
	int out;
	const std::string* v = get(section_name, key);
	return v && parse_int(*v, out) ? out : fallback;
}

double IniFile::get_double(std::string_view section_name, std::string_view key, double fallback) const
{
	double out;
	const std::string* v = get(section_name, key);
	return v && parse_double(*v, out) ? out : fallback;
}

bool IniFile::get_bool(std::string_view section_name, std::string_view key, bool fallback) const
{
	bool out;
	const std::string* v = get(section_name, key);
	return v && parse_bool(*v, out) ? out : fallback;
}

double IniFile::get_percent(std::string_view section_name, std::string_view key, double fallback) const
{
	const std::string* v = get(section_name, key);
	if (!v)
		return fallback;
	double out;
	if (!parse_double(*v, out))
		return fallback;
	return v->find('%') != std::string::npos ? out / 100.0 : out;
}

std::vector<std::string> IniFile::split_list(std::string_view value)
{
	std::vector<std::string> items;
	size_t pos = 0;
	while (pos <= value.size()) {
		size_t comma = value.find(',', pos);
		if (comma == std::string_view::npos)
			comma = value.size();
		items.emplace_back(trim(value.substr(pos, comma - pos)));
		pos = comma + 1;
	}
	return items;
}

} // namespace ra::ini
