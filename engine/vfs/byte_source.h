#pragma once

// Read-only byte storage that archives and files are read from.
//
// A source hands out views into memory that stay valid while the source is
// alive. Files on disk are memory-mapped, so a 280 MB archive costs address
// space, not RAM. Other sources (for example an Android APK asset) can be
// added by implementing this interface.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ra::vfs {

class ByteSource {
public:
	virtual ~ByteSource() = default;
	virtual uint64_t size() const = 0;

	// The bytes [offset, offset + length). Empty if the range is outside the source.
	virtual std::span<const uint8_t> view(uint64_t offset, uint64_t length) const = 0;

	std::span<const uint8_t> all() const { return view(0, size()); }
};

// A file mapped into memory. Returns null (and sets error) if it can't be opened.
std::shared_ptr<const ByteSource> map_file(const std::string& path, std::string* error = nullptr);

// Owns its bytes.
class MemorySource final : public ByteSource {
public:
	explicit MemorySource(std::vector<uint8_t> data) : data_(std::move(data)) {}
	uint64_t size() const override { return data_.size(); }
	std::span<const uint8_t> view(uint64_t offset, uint64_t length) const override;

private:
	std::vector<uint8_t> data_;
};

// A window into another source, used for archives nested inside archives.
class SliceSource final : public ByteSource {
public:
	SliceSource(std::shared_ptr<const ByteSource> parent, uint64_t offset, uint64_t length)
		: parent_(std::move(parent)), offset_(offset), length_(length) {}
	uint64_t size() const override { return length_; }
	std::span<const uint8_t> view(uint64_t offset, uint64_t length) const override;

private:
	std::shared_ptr<const ByteSource> parent_;
	uint64_t offset_;
	uint64_t length_;
};

} // namespace ra::vfs
