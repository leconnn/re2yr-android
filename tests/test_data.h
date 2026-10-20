#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ra::test {

// The tests/data folder. RA_TEST_DATA_DIR in the environment overrides the
// build-time path (used when the tests run on an Android device).
inline std::string data_dir()
{
	if (const char* dir = std::getenv("RA_TEST_DATA_DIR"))
		return dir;
	return RA_TEST_DATA_DIR;
}

// Reads a reference file from tests/data, skipping blank lines and # comments.
// Each returned entry is one line split on whitespace.
inline std::vector<std::vector<std::string>> read_data_file(const std::string& name)
{
	std::vector<std::vector<std::string>> rows;
	std::ifstream in(data_dir() + "/" + name);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream fields(line);
		std::vector<std::string> row;
		std::string field;
		while (fields >> field)
			row.push_back(field);
		if (!row.empty())
			rows.push_back(std::move(row));
	}
	return rows;
}

} // namespace ra::test
