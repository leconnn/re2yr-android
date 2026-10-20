#include "vfs/mix_archive.h"

#include "vfs/blowfish.h"
#include "vfs/mix_id.h"
#include "vfs/mix_key.h"

#include <algorithm>
#include <cstring>

namespace ra::vfs {

namespace {

constexpr uint32_t kFlagChecksum = 0x00010000;
constexpr uint32_t kFlagEncrypted = 0x00020000;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

void fail(std::string* error, const char* message)
{
	if (error)
		*error = message;
}

} // namespace

bool MixArchive::looks_like_archive(std::span<const uint8_t> bytes)
{
	if (bytes.size() < 10)
		return false;
	const uint32_t flags = rd32(bytes.data());
	if (flags == 0 || flags == kFlagChecksum || flags == kFlagEncrypted || flags == (kFlagChecksum | kFlagEncrypted))
		return true;
	// The old format has no flags word: u16 count, u32 body size, the table, then the
	// body, so its total size is exactly known.
	const uint64_t expected = 6 + 12ull * rd16(bytes.data()) + rd32(bytes.data() + 2);
	return expected == bytes.size();
}

std::shared_ptr<const MixArchive> MixArchive::open(std::shared_ptr<const ByteSource> source, uint64_t base, uint64_t length, std::string* error)
{
	auto head = source->view(base, std::min<uint64_t>(length, 4));
	if (head.size() < 4) {
		fail(error, "archive too small");
		return nullptr;
	}

	std::shared_ptr<MixArchive> mix(new MixArchive());
	mix->source_ = source;

	const uint32_t flags = rd32(head.data());
	std::vector<uint8_t> table; // decrypted or copied: u16 count, u32 body_size, entries
	uint64_t header_end = 0;    // offset from base where the body starts
	uint16_t count = 0;

	if (flags & 0xFFFF) {
		// Old C&C / Red Alert format: no flags word, the file starts with the count.
		mix->old_format_ = true;
		auto h = source->view(base, std::min<uint64_t>(length, 6));
		if (h.size() < 6) {
			fail(error, "archive too small");
			return nullptr;
		}
		count = rd16(h.data());
		header_end = 6 + 12ull * count;
		auto t = source->view(base, header_end);
		if (t.size() != header_end) {
			fail(error, "truncated archive header");
			return nullptr;
		}
		table.assign(t.begin(), t.end());
	} else if (flags & kFlagEncrypted) {
		mix->encrypted_ = true;
		mix->has_checksum_ = (flags & kFlagChecksum) != 0;
		auto key_source = source->view(base + 4, 80);
		auto first = source->view(base + 84, 8);
		if (key_source.size() != 80 || first.size() != 8) {
			fail(error, "truncated encrypted header");
			return nullptr;
		}
		uint8_t key[56];
		mix_blowfish_key(key_source.data(), key);
		const Blowfish bf(key, sizeof key);

		uint8_t block[8];
		std::memcpy(block, first.data(), 8);
		bf.decrypt_block(block);
		count = rd16(block);
		const uint64_t plain_len = 6 + 12ull * count;
		const uint64_t enc_len = (plain_len + 7) / 8 * 8;
		auto enc = source->view(base + 84, enc_len);
		if (enc.size() != enc_len) {
			fail(error, "truncated encrypted header");
			return nullptr;
		}
		table.resize(enc_len);
		std::memcpy(table.data(), enc.data(), enc_len);
		for (uint64_t i = 0; i < enc_len; i += 8)
			bf.decrypt_block(table.data() + i);
		header_end = 84 + enc_len;
	} else {
		mix->has_checksum_ = (flags & kFlagChecksum) != 0;
		auto h = source->view(base + 4, 6);
		if (h.size() < 6) {
			fail(error, "archive too small");
			return nullptr;
		}
		count = rd16(h.data());
		const uint64_t table_len = 6 + 12ull * count;
		auto t = source->view(base + 4, table_len);
		if (t.size() != table_len) {
			fail(error, "truncated archive header");
			return nullptr;
		}
		table.assign(t.begin(), t.end());
		header_end = 4 + table_len;
	}

	mix->body_size_ = rd32(table.data() + 2);
	mix->body_offset_ = base + header_end;
	mix->entries_.reserve(count);
	for (uint16_t i = 0; i < count; ++i) {
		const uint8_t* e = table.data() + 6 + 12ull * i;
		MixEntry entry { rd32(e), rd32(e + 4), rd32(e + 8) };
		if (uint64_t(entry.offset) + entry.size > length - header_end) {
			fail(error, "entry outside the archive");
			return nullptr;
		}
		mix->entries_.push_back(entry);
	}
	// The format keeps entries sorted by signed id; sort anyway so lookups
	// can't be wrong if a tool wrote them differently.
	std::stable_sort(mix->entries_.begin(), mix->entries_.end(), [](const MixEntry& a, const MixEntry& b) {
		return static_cast<int32_t>(a.id) < static_cast<int32_t>(b.id);
	});
	return mix;
}

const MixEntry* MixArchive::find(uint32_t id) const
{
	const auto key = static_cast<int32_t>(id);
	auto it = std::lower_bound(entries_.begin(), entries_.end(), key, [](const MixEntry& e, int32_t k) {
		return static_cast<int32_t>(e.id) < k;
	});
	if (it == entries_.end() || it->id != id)
		return nullptr;
	return &*it;
}

const MixEntry* MixArchive::find(std::string_view name) const
{
	return find(mix_id(name));
}

std::span<const uint8_t> MixArchive::data(const MixEntry& entry) const
{
	return source_->view(body_offset_ + entry.offset, entry.size);
}

std::shared_ptr<const MixArchive> MixArchive::open_nested(const MixEntry& entry, std::string* error) const
{
	return open(source_, body_offset_ + entry.offset, entry.size, error);
}

} // namespace ra::vfs
