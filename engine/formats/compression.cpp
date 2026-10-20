#include "formats/compression.h"

#include <cstring>

namespace ra::formats {

bool lcw_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out, size_t max_output)
{
	const size_t start = out.size();
	size_t i = 0;
	const size_t n = in.size();
	auto need = [&](size_t bytes) { return i + bytes <= n; };
	auto room = [&](size_t bytes) { return out.size() - start + bytes <= max_output; };

	while (i < n) {
		const uint8_t c = in[i++];
		if (c == 0x80)
			return true;
		if (!(c & 0x80)) {
			// Copy from earlier output, relative to the current position.
			if (!need(1))
				return false;
			const size_t count = (c >> 4) + 3;
			const size_t pos = (size_t(c & 0x0F) << 8) | in[i++];
			if (pos == 0 || pos > out.size() - start || !room(count))
				return false;
			size_t from = out.size() - pos;
			for (size_t k = 0; k < count; ++k)
				out.push_back(out[from + k]);
		} else if (!(c & 0x40)) {
			// Literal run.
			const size_t count = c & 0x3F;
			if (!need(count) || !room(count))
				return false;
			out.insert(out.end(), in.begin() + i, in.begin() + i + count);
			i += count;
		} else if (c == 0xFE) {
			// Fill.
			if (!need(3))
				return false;
			const size_t count = in[i] | (in[i + 1] << 8);
			const uint8_t value = in[i + 2];
			i += 3;
			if (!room(count))
				return false;
			out.insert(out.end(), count, value);
		} else if (c == 0xFF) {
			// Copy from an absolute position in the output.
			if (!need(4))
				return false;
			const size_t count = in[i] | (in[i + 1] << 8);
			const size_t pos = in[i + 2] | (in[i + 3] << 8);
			i += 4;
			if (!room(count))
				return false;
			for (size_t k = 0; k < count; ++k) {
				if (start + pos + k >= out.size())
					return false;
				out.push_back(out[start + pos + k]);
			}
		} else {
			// Copy from an absolute position in the output (short form).
			if (!need(2))
				return false;
			const size_t count = (c & 0x3F) + 3;
			const size_t pos = in[i] | (in[i + 1] << 8);
			i += 2;
			if (!room(count))
				return false;
			for (size_t k = 0; k < count; ++k) {
				if (start + pos + k >= out.size())
					return false;
				out.push_back(out[start + pos + k]);
			}
		}
	}
	return true;
}

bool lzo1x_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out, size_t max_output)
{
	const size_t start = out.size();
	const uint8_t* ip = in.data();
	const uint8_t* const ip_end = ip + in.size();
	size_t t = 0;

	// Reading past the end of the input, or growing past the limit, is a data error.
	auto have = [&](size_t bytes) { return size_t(ip_end - ip) >= bytes; };
	auto room = [&](size_t bytes) { return out.size() - start + bytes <= max_output; };
	auto copy_literals = [&](size_t count) {
		if (!have(count) || !room(count))
			return false;
		out.insert(out.end(), ip, ip + count);
		ip += count;
		return true;
	};
	// Copies 'count' bytes from 'distance' bytes back.
	auto copy_match = [&](size_t distance, size_t count) {
		if (distance == 0 || distance > out.size() - start || !room(count))
			return false;
		size_t from = out.size() - distance;
		for (size_t k = 0; k < count; ++k)
			out.push_back(out[from + k]);
		return true;
	};
	// Long counts are encoded as zero bytes (255 each) followed by a final byte.
	auto long_count = [&](size_t base) {
		size_t n = 0;
		while (true) {
			if (!have(1))
				return size_t(-1);
			if (*ip != 0)
				break;
			n += 255;
			++ip;
		}
		n += base + *ip++;
		return n;
	};

	if (!have(1))
		return false;

	enum class State { FirstLiteralRun, LiteralRun, Match, MatchNext } state;
	if (*ip > 17) {
		t = size_t(*ip++) - 17;
		if (t < 4) {
			state = State::MatchNext;
		} else {
			if (!copy_literals(t))
				return false;
			state = State::FirstLiteralRun;
		}
	} else {
		state = State::LiteralRun;
	}

	while (true) {
		switch (state) {
		case State::LiteralRun: {
			if (!have(1))
				return false;
			t = *ip++;
			if (t >= 16) {
				state = State::Match;
				break;
			}
			if (t == 0) {
				t = long_count(15);
				if (t == size_t(-1))
					return false;
			}
			if (!copy_literals(t + 3))
				return false;
			state = State::FirstLiteralRun;
			break;
		}
		case State::FirstLiteralRun: {
			if (!have(1))
				return false;
			t = *ip++;
			if (t >= 16) {
				state = State::Match;
				break;
			}
			// A 3-byte match one block back, just after a literal run.
			if (!have(1))
				return false;
			const size_t distance = (1 + 0x0800) + (t >> 2) + (size_t(*ip++) << 2);
			if (!copy_match(distance, 3))
				return false;
			t = ip[-2] & 3;
			state = t == 0 ? State::LiteralRun : State::MatchNext;
			break;
		}
		case State::Match: {
			size_t distance;
			size_t length;
			if (t >= 64) {
				if (!have(1))
					return false;
				distance = 1 + ((t >> 2) & 7) + (size_t(*ip++) << 3);
				length = (t >> 5) - 1 + 2;
			} else if (t >= 32) {
				t &= 31;
				if (t == 0) {
					t = long_count(31);
					if (t == size_t(-1))
						return false;
				}
				if (!have(2))
					return false;
				distance = 1 + ((ip[0] | (ip[1] << 8)) >> 2);
				ip += 2;
				length = t + 2;
			} else if (t >= 16) {
				const size_t far = (t & 8) << 11;
				t &= 7;
				if (t == 0) {
					t = long_count(7);
					if (t == size_t(-1))
						return false;
				}
				if (!have(2))
					return false;
				const size_t d = (ip[0] | (ip[1] << 8)) >> 2;
				ip += 2;
				if (far + d == 0)
					return true; // end-of-stream marker
				distance = far + d + 0x4000;
				length = t + 2;
			} else {
				// A 2-byte match, just after a match.
				if (!have(1))
					return false;
				distance = 1 + (t >> 2) + (size_t(*ip++) << 2);
				length = 2;
			}
			if (!copy_match(distance, length))
				return false;
			t = ip[-2] & 3;
			state = t == 0 ? State::LiteralRun : State::MatchNext;
			break;
		}
		case State::MatchNext: {
			if (!copy_literals(t))
				return false;
			if (!have(1))
				return false;
			t = *ip++;
			state = State::Match;
			break;
		}
		}
	}
}

bool decode_chunks(std::span<const uint8_t> in, ChunkCodec codec, std::vector<uint8_t>& out)
{
	out.clear();
	size_t i = 0;
	while (i + 4 <= in.size()) {
		const size_t compressed = in[i] | (in[i + 1] << 8);
		const size_t expected = in[i + 2] | (in[i + 3] << 8);
		i += 4;
		if (i + compressed > in.size())
			return false;
		const size_t before = out.size();
		const auto chunk = in.subspan(i, compressed);
		const bool ok = codec == ChunkCodec::Lzo ? lzo1x_decompress(chunk, out, expected + 64) : lcw_decompress(chunk, out, expected + 64);
		if (!ok)
			return false;
		// The stored size is authoritative: a chunk that expands further is trimmed.
		if (out.size() - before > expected)
			out.resize(before + expected);
		i += compressed;
	}
	return true;
}

} // namespace ra::formats
