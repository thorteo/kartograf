#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace tarkov::test {

inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Splits on '\n' and keeps any '\r' in place, so tests see exactly the bytes
/// the game wrote. The log fixture is stored with CRLF on purpose.
inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

inline std::string fixturePath(const std::string& relative) {
    return std::string(TARKOV_FIXTURE_DIR) + "/" + relative;
}

inline std::string dataPath(const std::string& relative) {
    return std::string(TARKOV_DATA_DIR) + "/" + relative;
}

inline std::vector<std::string> screenshotFixtureNames() {
    auto lines = splitLines(readFile(fixturePath("screenshots/filenames.txt")));
    std::vector<std::string> names;
    for (auto& line : lines) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            names.push_back(line);
        }
    }
    return names;
}

}  // namespace tarkov::test
