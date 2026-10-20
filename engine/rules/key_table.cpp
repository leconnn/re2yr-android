#include "rules/key_table.h"

#include "ini/ini.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ra::rules {

namespace {

const KeyInfo kKeys[] = {
#include "rules/key_table_data.inc"
};

std::string make_key(std::string_view owner, std::string_view key)
{
	std::string k = ra::ini::to_lower(owner);
	k.push_back('\0');
	k += ra::ini::to_lower(key);
	return k;
}

} // namespace

struct KeyTable::Impl {
	std::unordered_map<std::string, const KeyInfo*> by_owner_and_key;
};

KeyTable::KeyTable() : impl_(new Impl)
{
	for (const KeyInfo& k : kKeys) {
		// Several sections can read the same key for one class; keep the type's own section's entry.
		auto [it, inserted] = impl_->by_owner_and_key.emplace(make_key(k.owner, k.key), &k);
		if (!inserted && std::string_view(k.section) == "<object id>" && std::string_view(it->second->section) != "<object id>")
			it->second = &k;
	}
}

const KeyTable& KeyTable::instance()
{
	static const KeyTable table;
	return table;
}

std::span<const KeyInfo> KeyTable::all() const
{
	return kKeys;
}

std::string_view base_class(std::string_view owner_class)
{
	const std::string c = ra::ini::to_lower(owner_class);
	if (c == "aircrafttypeclass" || c == "unittypeclass" || c == "infantrytypeclass" || c == "buildingtypeclass")
		return "TechnoTypeClass";
	if (c == "technotypeclass")
		return "ObjectTypeClass";
	if (c == "objecttypeclass")
		return "AbstractTypeClass";
	return "";
}

const KeyInfo* KeyTable::find(std::string_view owner_class, std::string_view key) const
{
	std::string_view owner = owner_class;
	for (int depth = 0; depth < 8; ++depth) {
		if (auto it = impl_->by_owner_and_key.find(make_key(owner, key)); it != impl_->by_owner_and_key.end())
			return it->second;
		if (owner.empty())
			return nullptr;
		owner = base_class(owner);
	}
	return nullptr;
}

} // namespace ra::rules
