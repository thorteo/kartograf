#include "core/game/LineSplitter.h"

namespace tarkov::game {

std::vector<std::string> LineSplitter::feed(std::string_view chunk) {
    std::vector<std::string> lines;
    partial_.append(chunk);

    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = partial_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        lines.emplace_back(partial_, start, newline - start);
        start = newline + 1;
    }
    partial_.erase(0, start);
    return lines;
}

}  // namespace tarkov::game
