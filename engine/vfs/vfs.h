#pragma once

// The virtual file system: one lookup across loose files and all mounted archives.
//
// Lookup order (P1 section 5.6): loose files in the game directory first (this is
// how mods override rules), then the mounted archives in the order they were
// mounted, the first archive containing a name winning. mount_standard() mounts
// archives in the same order as the original game.

#include "vfs/mix_archive.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra::vfs {

// Which game's file set to mount.
enum class GameKind {
	RedAlert2,    // ra2.mix and friends, without the "md" archives
	YurisRevenge, // the full set, including the "md" archives and expansion patches
};

// The bytes of a file, valid while this object (or a copy) is alive.
struct FileData {
	std::span<const uint8_t> bytes;
	std::shared_ptr<const void> owner; // keeps the storage alive

	explicit operator bool() const { return owner != nullptr; }
	std::string_view text() const { return { reinterpret_cast<const char*>(bytes.data()), bytes.size() }; }
};

class NameDb;

class Vfs {
public:
	struct Mount {
		std::string label; // for example "ra2md.mix" or "ra2md.mix/conqmd.mix"
		std::shared_ptr<const MixArchive> archive;
	};

	// Loose files in this directory are found first. File names match
	// case-insensitively, as on Windows, even on case-sensitive file systems.
	// Only the directory's own files are indexed, not sub-directories.
	void set_loose_directory(const std::string& directory);
	const std::string& loose_directory() const { return loose_directory_; }

	// Mounts an archive stored as a file on disk, after the ones mounted so far.
	bool mount_disk_archive(const std::string& path, std::string* error = nullptr);

	// Mounts an archive stored in an existing source (an in-memory or APK-embedded archive).
	bool mount_archive(const std::string& label, std::shared_ptr<const MixArchive> archive);

	// Finds 'name' with the normal lookup (a loose file or inside a mounted
	// archive) and mounts it as an archive. Returns false if it isn't found
	// or isn't an archive. Mounting the same label twice does nothing.
	bool mount(std::string_view name, std::string* error = nullptr);

	// Mounts the archives of a game in the original's order (P1 section 5.6).
	// Returns the labels that were mounted.
	std::vector<std::string> mount_standard(GameKind kind);

	bool exists(std::string_view name) const;
	FileData open(std::string_view name) const;

	// The real names of the loose files that start with 'prefix' and end with 'suffix'
	// (compared case-insensitively), sorted by name.
	std::vector<std::string> loose_files_matching(std::string_view prefix, std::string_view suffix) const;

	const std::vector<Mount>& mounts() const { return mounts_; }

	// Removes every archive mounted at position 'count' or later, keeping the first 'count'.
	// Used to swap the theater archives when another map is loaded.
	void unmount_from(size_t count);

	// Calls visit for every file in every mounted archive, including archives
	// inside archives (which are walked, not reported as files). Archives
	// mounted from inside another mounted archive are not visited twice. 'path' is the
	// chain of archive labels followed by the entry's name from 'names', or its
	// hexadecimal id when the name isn't known.
	struct VisitedEntry {
		std::string path;
		uint32_t id;
		uint32_t size;
		std::span<const uint8_t> bytes;
		bool named;
	};
	void visit_all(const NameDb* names, const std::function<void(const VisitedEntry&)>& visit) const;

private:
	FileData open_loose(std::string_view name) const;

	std::string loose_directory_;
	std::unordered_map<std::string, std::string> loose_files_; // lower-case name -> real file name
	std::vector<Mount> mounts_;
};

// Known names for archive ids, for listing archives by name. Archives only
// store hashes, so names must come from somewhere: the game's own INI files,
// strings in the program, or a community database.
class NameDb {
public:
	void add(std::string_view name);
	// One name per line (blank lines and lines starting with ';' are skipped).
	size_t add_lines(std::string_view text);
	const std::string* find(uint32_t id) const;
	size_t size() const { return names_.size(); }

private:
	std::unordered_map<uint32_t, std::string> names_;
};

} // namespace ra::vfs
