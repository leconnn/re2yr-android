#pragma once

// Audio containers (P1 section 6.8): RIFF WAV with IMA-ADPCM (music, speech),
// and the "GABA" index (audio.idx) over a raw sample bank (audio.bag).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ra::formats {

// Decoded audio: interleaved signed 16-bit samples.
struct PcmAudio {
	uint32_t sample_rate = 0;
	uint16_t channels = 0;
	std::vector<int16_t> samples; // frames * channels
};

// Decodes a RIFF/WAVE file in format 1 (PCM, 8 or 16 bit) or 0x11 (IMA ADPCM).
bool decode_wav(std::span<const uint8_t> data, PcmAudio& out);

// Header information without decoding.
struct WavInfo {
	uint16_t format = 0, channels = 0, block_align = 0, bits = 0;
	uint32_t sample_rate = 0;
	std::span<const uint8_t> data;
};
bool parse_wav_header(std::span<const uint8_t> data, WavInfo& out);

// Standard IMA ADPCM block decoding (per-channel 4-byte headers, then
// nibbles in groups of 4 bytes per channel).
bool decode_ima_adpcm_blocks(std::span<const uint8_t> data, uint16_t channels, uint16_t block_align, std::vector<int16_t>& out);

struct AudioIndexEntry {
	std::string name;
	uint32_t offset = 0, size = 0, sample_rate = 0, flags = 0, chunk_size = 0;
};

struct AudioIndex {
	std::vector<AudioIndexEntry> entries;
	static bool parse(std::span<const uint8_t> idx, AudioIndex& out);
	// True if every entry lies inside a bank of 'bag_size' bytes.
	bool fits_bank(uint64_t bag_size) const;
};

} // namespace ra::formats
