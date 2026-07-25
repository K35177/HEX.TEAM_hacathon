#pragma once

#include "acoustic/framing.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace acoustic {

struct TransferProfile {
    std::string name;
    std::string description;
    FskConfig modem;
    FrameConfig frame;
    std::size_t block_size = 128;
};

std::vector<TransferProfile> builtin_profiles();
TransferProfile load_profile(std::string_view name_or_path);
void validate_profile(const TransferProfile& profile);

}  // namespace acoustic
