#pragma once

#include <cstddef>
#include <span>

namespace acoustic {

// Applies either an explicit gain or automatic gain based on short-window RMS.
// Returns the actual multiplier. A requested_gain of 0 enables auto mode.
double apply_receive_gain(std::span<float> samples,
                          double requested_gain = 0.0,
                          std::size_t analysis_window = 240);

}  // namespace acoustic
