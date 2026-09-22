#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tarkov::game {

/// Turns a byte stream arriving in arbitrary chunks into complete lines.
///
/// The game appends to its logs while we read them, so a read can end in the
/// middle of a line. That partial line is held back until its newline arrives.
/// A trailing '\r' is kept: the log parsers strip it, and the fixtures keep
/// CRLF for exactly that reason.
///
/// The half of LogTail that does not need Windows, so it can be tested.
class LineSplitter {
public:
    /// Appends a chunk and returns the lines it completed, without their '\n'.
    std::vector<std::string> feed(std::string_view chunk);

    /// Drops any partial line, as when the file is reopened.
    void reset() { partial_.clear(); }

    /// Bytes held back, waiting for their newline.
    std::size_t pending() const { return partial_.size(); }

private:
    std::string partial_;
};

}  // namespace tarkov::game
