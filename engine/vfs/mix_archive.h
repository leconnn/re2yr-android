#pragma once

// MIX archives, the TS / RA2 variant (P1 section 5.2).
//
// An archive is a sorted table of (id, offset, size) followed by the file
// data. Names are not stored: the id is a hash of the upper-cased name
// (mix_id). The header may be Blowfish-encrypted with a key that is itself
// RSA-wrapped inside the archive. Archives nest: a file inside an archive can
// itself be an archive.

#include "vfs/byte_source.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ra::vfs {

struct MixEntry {
	uint32_t id = 0;
	uint32_t offset = 0; // from the start of the body
	uint32_t size = 0;
};

class MixArchive {
public:
	// Opens the archive stored in source[base, base + length). Returns null and
	// sets *error if the data isn't a valid archive.
	static std::shared_ptr<const MixArchive> open(std::shared_ptr<const ByteSource> source, uint64_t base, uint64_t length, std::string* error = nullptr);

	// Opens a whole source.
	static std::shared_ptr<const MixArchive> open(std::shared_ptr<const ByteSource> source, std::string* error = nullptr)
	{
		return open(source, 0, source->size(), error);
	}

	// Entries in file order (sorted by signed id).
	const std::vector<MixEntry>& entries() const { return entries_; }

	const MixEntry* find(uint32_t id) const;
	const MixEntry* find(std::string_view name) const;

	// The bytes of an entry, valid while the archive is alive.
	std::span<const uint8_t> data(const MixEntry& entry) const;

	// Opens an entry that is itself an archive. Null if it isn't one.
	std::shared_ptr<const MixArchive> open_nested(const MixEntry& entry, std::string* error = nullptr) const;

	bool encrypted() const { return encrypted_; }
	bool has_checksum() const { return has_checksum_; }
	bool old_format() const { return old_format_; }
	uint32_t body_size() const { return body_size_; }

	// True if the bytes look like the start of an archive.
	static bool looks_like_archive(std::span<const uint8_t> bytes);

private:
	MixArchive() = default;

	std::shared_ptr<const ByteSource> source_;
	uint64_t body_offset_ = 0; // in source_
	std::vector<MixEntry> entries_;
	uint32_t body_size_ = 0;
	bool encrypted_ = false;
	bool has_checksum_ = false;
	bool old_format_ = false;
};

} // namespace ra::vfs
