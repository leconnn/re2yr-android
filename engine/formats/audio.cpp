#include "formats/audio.h"

#include <algorithm>
#include <cstring>

namespace ra::formats {

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

constexpr int kIndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
constexpr int kStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
	143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
	1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
	9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

struct AdpcmState {
	int predictor = 0;
	int index = 0;

	int16_t decode(unsigned nibble)
	{
		const int step = kStepTable[index];
		int diff = step >> 3;
		if (nibble & 1)
			diff += step >> 2;
		if (nibble & 2)
			diff += step >> 1;
		if (nibble & 4)
			diff += step;
		predictor += (nibble & 8) ? -diff : diff;
		predictor = std::clamp(predictor, -32768, 32767);
		index = std::clamp(index + kIndexTable[nibble], 0, 88);
		return static_cast<int16_t>(predictor);
	}
};

} // namespace

bool decode_ima_adpcm_blocks(std::span<const uint8_t> data, uint16_t channels, uint16_t block_align, std::vector<int16_t>& out)
{
	if (channels == 0 || channels > 2 || block_align < 4u * channels)
		return false;
	out.clear();
	for (size_t block = 0; block + block_align <= data.size(); block += block_align) {
		const uint8_t* b = data.data() + block;
		AdpcmState state[2];
		const size_t header = 4u * channels;
		const size_t first = out.size();
		for (unsigned c = 0; c < channels; ++c) {
			state[c].predictor = static_cast<int16_t>(rd16(b + 4 * c));
			state[c].index = std::min<int>(b[4 * c + 2], 88);
			out.push_back(static_cast<int16_t>(state[c].predictor));
		}
		// After the headers, each channel contributes 4 bytes (8 samples) in turn.
		const size_t groups = (block_align - header) / (4u * channels);
		const size_t frames = 1 + groups * 8;
		out.resize(first + frames * channels);
		for (size_t g = 0; g < groups; ++g) {
			for (unsigned c = 0; c < channels; ++c) {
				const uint8_t* src = b + header + (g * channels + c) * 4;
				for (unsigned k = 0; k < 4; ++k) {
					const size_t f = 1 + g * 8 + k * 2;
					out[first + f * channels + c] = state[c].decode(src[k] & 0x0F);
					out[first + (f + 1) * channels + c] = state[c].decode(src[k] >> 4);
				}
			}
		}
	}
	return true;
}

bool parse_wav_header(std::span<const uint8_t> data, WavInfo& out)
{
	if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0)
		return false;
	bool have_fmt = false, have_data = false;
	size_t p = 12;
	while (p + 8 <= data.size()) {
		const uint32_t size = rd32(data.data() + p + 4);
		const size_t body = p + 8;
		const size_t avail = std::min<size_t>(size, data.size() - body);
		if (std::memcmp(data.data() + p, "fmt ", 4) == 0 && avail >= 16) {
			out.format = rd16(data.data() + body);
			out.channels = rd16(data.data() + body + 2);
			out.sample_rate = rd32(data.data() + body + 4);
			out.block_align = rd16(data.data() + body + 12);
			out.bits = rd16(data.data() + body + 14);
			have_fmt = true;
		} else if (std::memcmp(data.data() + p, "data", 4) == 0) {
			out.data = data.subspan(body, avail);
			have_data = true;
		}
		p = body + size + (size & 1);
	}
	return have_fmt && have_data;
}

bool decode_wav(std::span<const uint8_t> data, PcmAudio& out)
{
	WavInfo info;
	if (!parse_wav_header(data, info) || info.channels == 0)
		return false;
	out.sample_rate = info.sample_rate;
	out.channels = info.channels;
	out.samples.clear();
	if (info.format == 0x11)
		return decode_ima_adpcm_blocks(info.data, info.channels, info.block_align, out.samples);
	if (info.format == 1 && info.bits == 16) {
		out.samples.resize(info.data.size() / 2);
		for (size_t i = 0; i < out.samples.size(); ++i)
			out.samples[i] = static_cast<int16_t>(rd16(info.data.data() + 2 * i));
		return true;
	}
	if (info.format == 1 && info.bits == 8) {
		out.samples.resize(info.data.size());
		for (size_t i = 0; i < out.samples.size(); ++i)
			out.samples[i] = static_cast<int16_t>((int(info.data[i]) - 128) << 8);
		return true;
	}
	return false;
}

bool AudioIndex::parse(std::span<const uint8_t> idx, AudioIndex& out)
{
	if (idx.size() < 12 || std::memcmp(idx.data(), "GABA", 4) != 0)
		return false;
	const uint32_t count = rd32(idx.data() + 8);
	if (idx.size() < 12 + 36ull * count)
		return false;
	out.entries.clear();
	out.entries.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		const uint8_t* e = idx.data() + 12 + 36ull * i;
		AudioIndexEntry entry;
		entry.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 16));
		entry.offset = rd32(e + 16);
		entry.size = rd32(e + 20);
		entry.sample_rate = rd32(e + 24);
		entry.flags = rd32(e + 28);
		entry.chunk_size = rd32(e + 32);
		out.entries.push_back(std::move(entry));
	}
	return true;
}

bool AudioIndex::fits_bank(uint64_t bag_size) const
{
	for (const auto& e : entries) {
		if (uint64_t(e.offset) + e.size > bag_size)
			return false;
	}
	return true;
}

} // namespace ra::formats
