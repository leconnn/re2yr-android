#include "vfs/byte_source.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ra::vfs {

namespace {

bool in_range(uint64_t size, uint64_t offset, uint64_t length)
{
	return offset <= size && length <= size - offset;
}

#if defined(_WIN32)

class MappedFile final : public ByteSource {
public:
	~MappedFile() override
	{
		if (data_)
			UnmapViewOfFile(data_);
		if (mapping_)
			CloseHandle(mapping_);
		if (file_ != INVALID_HANDLE_VALUE)
			CloseHandle(file_);
	}

	bool open(const std::string& path, std::string* error)
	{
		// Paths are UTF-8; convert for the wide API.
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
		std::wstring wpath(static_cast<size_t>(wlen > 0 ? wlen : 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
		file_ = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file_ == INVALID_HANDLE_VALUE) {
			if (error)
				*error = "cannot open " + path;
			return false;
		}
		LARGE_INTEGER sz;
		if (!GetFileSizeEx(file_, &sz)) {
			if (error)
				*error = "cannot get size of " + path;
			return false;
		}
		size_ = static_cast<uint64_t>(sz.QuadPart);
		if (size_ == 0)
			return true; // an empty file can't be mapped; view() returns empty
		mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!mapping_) {
			if (error)
				*error = "cannot map " + path;
			return false;
		}
		data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
		if (!data_) {
			if (error)
				*error = "cannot map view of " + path;
			return false;
		}
		return true;
	}

	uint64_t size() const override { return size_; }
	std::span<const uint8_t> view(uint64_t offset, uint64_t length) const override
	{
		if (!data_ || !in_range(size_, offset, length))
			return {};
		return { data_ + offset, static_cast<size_t>(length) };
	}

private:
	HANDLE file_ = INVALID_HANDLE_VALUE;
	HANDLE mapping_ = nullptr;
	const uint8_t* data_ = nullptr;
	uint64_t size_ = 0;
};

#else

class MappedFile final : public ByteSource {
public:
	~MappedFile() override
	{
		if (data_)
			munmap(const_cast<uint8_t*>(data_), static_cast<size_t>(size_));
	}

	bool open(const std::string& path, std::string* error)
	{
		const int fd = ::open(path.c_str(), O_RDONLY);
		if (fd < 0) {
			if (error)
				*error = "cannot open " + path;
			return false;
		}
		struct stat st {};
		if (fstat(fd, &st) != 0) {
			::close(fd);
			if (error)
				*error = "cannot stat " + path;
			return false;
		}
		size_ = static_cast<uint64_t>(st.st_size);
		if (size_ > 0) {
			void* p = mmap(nullptr, static_cast<size_t>(size_), PROT_READ, MAP_PRIVATE, fd, 0);
			if (p == MAP_FAILED) {
				::close(fd);
				if (error)
					*error = "cannot map " + path;
				return false;
			}
			data_ = static_cast<const uint8_t*>(p);
		}
		::close(fd);
		return true;
	}

	uint64_t size() const override { return size_; }
	std::span<const uint8_t> view(uint64_t offset, uint64_t length) const override
	{
		if (!data_ || !in_range(size_, offset, length))
			return {};
		return { data_ + offset, static_cast<size_t>(length) };
	}

private:
	const uint8_t* data_ = nullptr;
	uint64_t size_ = 0;
};

#endif

} // namespace

std::shared_ptr<const ByteSource> map_file(const std::string& path, std::string* error)
{
	auto file = std::make_shared<MappedFile>();
	if (!file->open(path, error))
		return nullptr;
	return file;
}

std::span<const uint8_t> MemorySource::view(uint64_t offset, uint64_t length) const
{
	if (!in_range(data_.size(), offset, length))
		return {};
	return { data_.data() + offset, static_cast<size_t>(length) };
}

std::span<const uint8_t> SliceSource::view(uint64_t offset, uint64_t length) const
{
	if (!in_range(length_, offset, length))
		return {};
	return parent_->view(offset_ + offset, length);
}

} // namespace ra::vfs
