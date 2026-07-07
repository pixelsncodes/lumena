#pragma once

#include <string>
#include <vector>

namespace lumen::scales {

/// A musical scale: a human-readable name plus the semitone offsets
/// (intervals) from the root note that belong to the scale.
///
/// Example — major scale: intervals = { 0, 2, 4, 5, 7, 9, 11 }.
///
/// Skeleton only: stores the data, but degree/pitch conversion is not
/// implemented yet.
class Scale {
public:
    Scale() = default;
    Scale(std::string name, std::vector<int> intervals);

    const std::string& name() const noexcept { return name_; }
    const std::vector<int>& intervals() const noexcept { return intervals_; }
    std::size_t degreeCount() const noexcept { return intervals_.size(); }

    // TODO: degreeToMidiNote(int degree, int rootNote), pitch quantisation.

private:
    std::string name_;
    std::vector<int> intervals_;
};

} // namespace lumen::scales
