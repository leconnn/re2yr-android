#include "rules/rule_object.h"

#include "core/fpu.h"
#include "rules/key_table.h"

#include <cstdlib>

namespace ra::rules {

bool read_int(std::string_view text, int& out)
{
	return ra::ini::parse_int(text, out);
}

bool read_bool(std::string_view text, bool& out)
{
	return ra::ini::parse_bool(text, out);
}

bool read_double(std::string_view text, double& out)
{
	text = ra::ini::trim(text);
	if (text.empty())
		return false;
	std::string s(text);
	char* end = nullptr;
	const float f = std::strtof(s.c_str(), &end); // the original reads with sscanf("%f")
	if (end == s.c_str())
		return false;
	double v = f;
	if (s.find('%') != std::string::npos)
		v = ra::fpu::mul(v, 0.01);
	out = v;
	return true;
}

bool read_leptons(std::string_view text, int& out)
{
	double cells;
	if (!read_double(text, cells) || cells == -1.0)
		return false;
	out = ra::fpu::ftol(ra::fpu::mul(cells, 256.0));
	return true;
}

bool read_rgb(std::string_view text, ra::formats::Rgb& out)
{
	const auto items = ra::ini::IniFile::split_list(text);
	if (items.size() < 3)
		return false;
	int v[3];
	for (int i = 0; i < 3; ++i) {
		if (!ra::ini::parse_int(items[i], v[i]))
			return false;
	}
	auto clamp = [](int c) { return static_cast<uint8_t>(c < 0 ? 0 : (c > 255 ? 255 : c)); };
	out = { clamp(v[0]), clamp(v[1]), clamp(v[2]) };
	return true;
}

bool read_rect(std::string_view text, RuleRect& out)
{
	const auto items = ra::ini::IniFile::split_list(text);
	if (items.size() < 4)
		return false;
	int v[4];
	for (int i = 0; i < 4; ++i) {
		if (!ra::ini::parse_int(items[i], v[i]))
			return false;
	}
	out = { v[0], v[1], v[2], v[3] };
	return true;
}

RuleObject::RuleObject(const ra::ini::IniSection* section, std::string owner_class)
	: section_(section), owner_(std::move(owner_class))
{
}

const std::string* RuleObject::find_text(std::string_view key) const
{
	return section_ ? section_->find(key) : nullptr;
}

const KeyInfo* RuleObject::info(std::string_view key) const
{
	return KeyTable::instance().find(owner_, key);
}

bool RuleObject::has(std::string_view key) const
{
	return find_text(key) != nullptr;
}

namespace {

// The default text of a key, or null when the key has no known default.
const std::string_view* default_text_holder(const KeyInfo* k, std::string_view& storage)
{
	if (!k || !k->has_default)
		return nullptr;
	storage = k->default_text;
	return &storage;
}

} // namespace

int RuleObject::get_int(std::string_view key) const
{
	int v;
	if (const std::string* t = find_text(key); t && read_int(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage); d && read_int(*d, v))
		return v;
	return 0;
}

bool RuleObject::get_bool(std::string_view key) const
{
	bool v;
	if (const std::string* t = find_text(key); t && read_bool(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage)) {
		// Two defaults are derived from other keys: "!NoRotate" (the opposite of a bool key)
		// and "SpeedType==Float" (a string key equals a value).
		if (!d->empty() && d->front() == '!')
			return !get_bool(d->substr(1));
		if (const size_t eq = d->find("=="); eq != std::string_view::npos)
			return ra::ini::to_lower(get_string(d->substr(0, eq))) == ra::ini::to_lower(d->substr(eq + 2));
		if (read_bool(*d, v))
			return v;
	}
	return false;
}

double RuleObject::get_double(std::string_view key) const
{
	double v;
	if (const std::string* t = find_text(key); t && read_double(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage); d && read_double(*d, v))
		return v;
	return 0.0;
}

int RuleObject::get_leptons(std::string_view key) const
{
	int v;
	if (const std::string* t = find_text(key); t && read_leptons(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage); d && read_leptons(*d, v))
		return v;
	return 0;
}

ra::formats::Rgb RuleObject::get_rgb(std::string_view key) const
{
	ra::formats::Rgb v;
	if (const std::string* t = find_text(key); t && read_rgb(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage); d && read_rgb(*d, v))
		return v;
	return {};
}

std::string RuleObject::get_string(std::string_view key) const
{
	if (const std::string* t = find_text(key))
		return *t;
	if (const KeyInfo* k = info(key); k && k->has_default)
		return k->default_text;
	return {};
}

std::vector<std::string> RuleObject::get_list(std::string_view key) const
{
	std::vector<std::string> items;
	for (std::string& item : ra::ini::IniFile::split_list(get_string(key))) {
		if (!item.empty())
			items.push_back(std::move(item));
	}
	return items;
}

RuleRect RuleObject::get_rect(std::string_view key) const
{
	RuleRect v;
	if (const std::string* t = find_text(key); t && read_rect(*t, v))
		return v;
	std::string_view storage;
	if (auto* d = default_text_holder(info(key), storage); d && read_rect(*d, v))
		return v;
	return {};
}

} // namespace ra::rules
