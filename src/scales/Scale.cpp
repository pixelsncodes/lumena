#include "scales/Scale.h"

#include <utility>

namespace lumena::scales {

Scale::Scale(std::string name, std::vector<int> intervals)
    : name_(std::move(name)), intervals_(std::move(intervals)) {}

} // namespace lumena::scales
