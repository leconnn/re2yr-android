#include "vfs/vfs.h"

#include "core/log.h"
#include "vfs/mix_id.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace ra::vfs {

namespace {

// UTF-8 text to a file system path and back. C++20 made u8string() return char8_t.
std::filesystem::path to_path(std::string_view utf8)
{
	return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string to_utf8(const std::filesystem::path& path)
{
	const std::u8string s = path.u8string();
	return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string lower(std::string_view s)
{
	std::string r(s);
	for (char& c : r) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return r;
}

std::string hex_id(uint32_t id)
{
	char buf[16];
	std::snprintf(buf, sizeof buf, "%08X", id);
	return buf;
}

std::string two_digits(int n)
{
	char buf[8];
	std::snprintf(buf, sizeof buf, "%02d", n);
	return buf;
}

} // namespace

void Vfs::set_loose_directory(const std::string& directory)
{
	loose_directory_ = directory;
	loose_files_.clear();
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(to_path(directory), ec)) {
		if (!entry.is_regular_file(ec))
			continue;
		const std::string name = to_utf8(entry.path().filename());
		loose_files_.emplace(lower(name), name);
	}
}

bool Vfs::mount_disk_archive(const std::string& path, std::string* error)
{
	auto source = map_file(path, error);
	if (!source)
		return false;
	auto archive = MixArchive::open(source, error);
	if (!archive)
		return false;
	const std::string label = to_utf8(to_path(path).filename());
	return mount_archive(label, std::move(archive));
}

bool Vfs::mount_archive(const std::string& label, std::shared_ptr<const MixArchive> archive)
{
	const std::string key = lower(label);
	for (const Mount& m : mounts_) {
		if (lower(m.label) == key)
			return true;
	}
	mounts_.push_back({ label, std::move(archive) });
	return true;
}

void Vfs::unmount_from(size_t count)
{
	if (count < mounts_.size())
		mounts_.resize(count);
}

bool Vfs::mount(std::string_view name, std::string* error)
{
	const std::string key = lower(name);
	for (const Mount& m : mounts_) {
		if (lower(m.label) == key)
			return true;
	}

	// A loose archive file next to the game files.
	if (auto it = loose_files_.find(key); it != loose_files_.end())
		return mount_disk_archive(loose_directory_ + "/" + it->second, error);

	for (const Mount& m : mounts_) {
		const MixEntry* entry = m.archive->find(name);
		if (!entry)
			continue;
		if (!MixArchive::looks_like_archive(m.archive->data(*entry)))
			continue;
		auto nested = m.archive->open_nested(*entry, error);
		if (!nested)
			return false;
		mounts_.push_back({ m.label + "/" + std::string(name), std::move(nested) });
		return true;
	}
	if (error)
		*error = std::string(name) + " not found";
	return false;
}

std::vector<std::string> Vfs::mount_standard(GameKind kind)
{
	const size_t first = mounts_.size();
	auto try_mount = [this](const std::string& name) { mount(name); };

	if (kind == GameKind::YurisRevenge) {
		try_mount("langmd.mix");
		try_mount("language.mix");
		// Expansion patches, newest first.
		for (int i = 99; i >= 0; --i)
			try_mount("expandmd" + two_digits(i) + ".mix");
		for (const char* n : { "ra2md.mix", "ra2.mix", "cachemd.mix", "cache.mix", "localmd.mix", "local.mix" })
			try_mount(n);
	} else {
		try_mount("language.mix");
		for (const char* n : { "ra2.mix", "cache.mix", "local.mix" })
			try_mount(n);
	}

	// Map-pack add-ons: ELOCAL*.MIX and ECACHE*.MIX, wildcard search on disk.
	std::vector<std::string> extras;
	for (const auto& [key, real] : loose_files_) {
		if (key.size() > 4 && key.substr(key.size() - 4) == ".mix" && (key.rfind("elocal", 0) == 0 || key.rfind("ecache", 0) == 0))
			extras.push_back(real);
	}
	std::sort(extras.begin(), extras.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
	for (const std::string& e : extras)
		try_mount(e);

	if (kind == GameKind::YurisRevenge) {
		try_mount("thememd.mix");
		try_mount("theme.mix");
		try_mount("multimd.mix");
		try_mount("multi.mix");
		for (int i = 0; i <= 99; ++i)
			try_mount("mapsmd" + two_digits(i) + ".mix");
		for (int i = 0; i <= 99; ++i)
			try_mount("maps" + two_digits(i) + ".mix");
		for (const char* n : { "conqmd.mix", "conquer.mix", "genermd.mix", "generic.mix", "isogenmd.mix", "isogen.mix", "cameomd.mix", "cameo.mix", "audiomd.mix", "audio.mix" })
			try_mount(n);
	} else {
		try_mount("theme.mix");
		try_mount("multi.mix");
		for (int i = 0; i <= 99; ++i)
			try_mount("maps" + two_digits(i) + ".mix");
		for (const char* n : { "conquer.mix", "generic.mix", "isogen.mix", "cameo.mix", "audio.mix" })
			try_mount(n);
	}

	std::vector<std::string> labels;
	for (size_t i = first; i < mounts_.size(); ++i)
		labels.push_back(mounts_[i].label);
	return labels;
}

std::vector<std::string> Vfs::loose_files_matching(std::string_view prefix, std::string_view suffix) const
{
	const std::string p = lower(prefix), s = lower(suffix);
	std::vector<std::string> names;
	for (const auto& [key, real] : loose_files_) {
		if (key.size() >= p.size() + s.size() && key.compare(0, p.size(), p) == 0 && key.compare(key.size() - s.size(), s.size(), s) == 0)
			names.push_back(real);
	}
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
	return names;
}

FileData Vfs::open_loose(std::string_view name) const
{
	auto it = loose_files_.find(lower(name));
	if (it == loose_files_.end())
		return {};
	std::ifstream in(to_path(loose_directory_ + "/" + it->second), std::ios::binary);
	if (!in)
		return {};
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	auto owner = std::make_shared<std::vector<uint8_t>>(std::move(data));
	return { std::span<const uint8_t>(owner->data(), owner->size()), owner };
}

bool Vfs::exists(std::string_view name) const
{
	if (loose_files_.count(lower(name)))
		return true;
	const uint32_t id = mix_id(name);
	for (const Mount& m : mounts_) {
		if (m.archive->find(id))
			return true;
	}
	return false;
}

FileData Vfs::open(std::string_view name) const
{
	if (FileData loose = open_loose(name))
		return loose;
	const uint32_t id = mix_id(name);
	for (const Mount& m : mounts_) {
		if (const MixEntry* entry = m.archive->find(id))
			return { m.archive->data(*entry), m.archive };
	}
	return {};
}

namespace {

void walk(const std::string& prefix, const std::shared_ptr<const MixArchive>& archive, const NameDb* names, const std::function<void(const Vfs::VisitedEntry&)>& visit)
{
	for (const MixEntry& entry : archive->entries()) {
		const std::string* name = names ? names->find(entry.id) : nullptr;
		const std::string label = name ? *name : hex_id(entry.id);
		auto bytes = archive->data(entry);
		if (MixArchive::looks_like_archive(bytes)) {
			if (auto nested = archive->open_nested(entry); nested && !nested->entries().empty()) {
				walk(prefix + label + "/", nested, names, visit);
				continue;
			}
		}
		visit({ prefix + label, entry.id, entry.size, bytes, name != nullptr });
	}
}

} // namespace

void Vfs::visit_all(const NameDb* names, const std::function<void(const VisitedEntry&)>& visit) const
{
	for (const Mount& m : mounts_) {
		// A nested mount ("ra2md.mix/localmd.mix") is already walked as part of its parent.
		if (m.label.find('/') != std::string::npos)
			continue;
		walk(m.label + "/", m.archive, names, visit);
	}
}

void NameDb::add(std::string_view name)
{
	names_.emplace(mix_id(name), std::string(name));
}

size_t NameDb::add_lines(std::string_view text)
{
	size_t added = 0;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view line = text.substr(pos, end - pos);
		pos = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.remove_suffix(1);
		if (line.empty() || line.front() == ';')
			continue;
		add(line);
		++added;
	}
	return added;
}

const std::string* NameDb::find(uint32_t id) const
{
	auto it = names_.find(id);
	return it == names_.end() ? nullptr : &it->second;
}

} // namespace ra::vfs
