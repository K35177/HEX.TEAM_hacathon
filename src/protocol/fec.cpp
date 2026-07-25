#include "acoustic/fec.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace acoustic {
namespace {

struct GaloisTables {
    std::array<std::uint8_t, 512> exponent{};
    std::array<std::uint8_t, 256> logarithm{};

    GaloisTables() {
        unsigned value = 1;
        for (std::size_t i = 0; i < 255; ++i) {
            exponent[i] = static_cast<std::uint8_t>(value);
            logarithm[value] = static_cast<std::uint8_t>(i);
            value <<= 1U;
            if ((value & 0x100U) != 0U) value ^= 0x11DU;
        }
        for (std::size_t i = 255; i < exponent.size(); ++i) exponent[i] = exponent[i - 255];
    }
};

const GaloisTables& tables() {
    static const GaloisTables instance;
    return instance;
}

std::uint8_t multiply(std::uint8_t left, std::uint8_t right) {
    if (left == 0 || right == 0) return 0;
    const auto& gf = tables();
    return gf.exponent[static_cast<unsigned>(gf.logarithm[left]) + gf.logarithm[right]];
}

std::uint8_t inverse(std::uint8_t value) {
    if (value == 0) throw std::runtime_error("cannot invert zero in GF(256)");
    const auto& gf = tables();
    return gf.exponent[255U - gf.logarithm[value]];
}

std::uint8_t power(unsigned exponent) {
    return tables().exponent[exponent % 255U];
}

std::vector<std::uint8_t> polynomial_multiply(std::span<const std::uint8_t> left,
                                              std::span<const std::uint8_t> right) {
    std::vector<std::uint8_t> product(left.size() + right.size() - 1U, 0);
    for (std::size_t i = 0; i < left.size(); ++i) {
        for (std::size_t j = 0; j < right.size(); ++j) {
            product[i + j] ^= multiply(left[i], right[j]);
        }
    }
    return product;
}

std::vector<std::uint8_t> polynomial_add(std::span<const std::uint8_t> left,
                                         std::span<const std::uint8_t> right) {
    std::vector<std::uint8_t> result(std::max(left.size(), right.size()), 0);
    const auto left_offset = result.size() - left.size();
    const auto right_offset = result.size() - right.size();
    for (std::size_t i = 0; i < left.size(); ++i) result[left_offset + i] ^= left[i];
    for (std::size_t i = 0; i < right.size(); ++i) result[right_offset + i] ^= right[i];
    return result;
}

std::vector<std::uint8_t> polynomial_scale(std::span<const std::uint8_t> polynomial,
                                           std::uint8_t factor) {
    std::vector<std::uint8_t> result(polynomial.size());
    for (std::size_t i = 0; i < polynomial.size(); ++i) {
        result[i] = multiply(polynomial[i], factor);
    }
    return result;
}

std::uint8_t polynomial_evaluate(std::span<const std::uint8_t> polynomial,
                                 std::uint8_t value) {
    std::uint8_t result = polynomial.front();
    for (std::size_t i = 1; i < polynomial.size(); ++i) {
        result = multiply(result, value) ^ polynomial[i];
    }
    return result;
}

const std::vector<std::uint8_t>& generator() {
    static const std::vector<std::uint8_t> value = [] {
        std::vector<std::uint8_t> result{1};
        for (std::size_t i = 0; i < kFecParityBytes; ++i) {
            const std::array<std::uint8_t, 2> factor{1, power(static_cast<unsigned>(i))};
            result = polynomial_multiply(result, factor);
        }
        return result;
    }();
    return value;
}

std::array<std::uint8_t, kFecParityBytes> syndromes(
    std::span<const std::uint8_t> codeword) {
    std::array<std::uint8_t, kFecParityBytes> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = polynomial_evaluate(codeword, power(static_cast<unsigned>(i)));
    }
    return result;
}

std::vector<std::uint8_t> find_error_locator(
    std::span<const std::uint8_t, kFecParityBytes> syndrome) {
    std::vector<std::uint8_t> locator{1};
    std::vector<std::uint8_t> previous{1};
    for (std::size_t i = 0; i < syndrome.size(); ++i) {
        std::uint8_t discrepancy = syndrome[i];
        for (std::size_t j = 1; j < locator.size(); ++j) {
            discrepancy ^= multiply(locator[locator.size() - 1U - j], syndrome[i - j]);
        }
        previous.push_back(0);
        if (discrepancy == 0) continue;
        if (previous.size() > locator.size()) {
            auto replacement = polynomial_scale(previous, discrepancy);
            previous = polynomial_scale(locator, inverse(discrepancy));
            locator = std::move(replacement);
        }
        locator = polynomial_add(locator, polynomial_scale(previous, discrepancy));
    }
    const auto first_nonzero = std::find_if(locator.begin(), locator.end(),
                                             [](std::uint8_t value) { return value != 0; });
    locator.erase(locator.begin(), first_nonzero);
    const auto errors = locator.size() - 1U;
    if (errors == 0 || errors * 2U > kFecParityBytes) {
        throw std::runtime_error("Reed-Solomon error count exceeds correction capacity");
    }
    return locator;
}

std::vector<std::size_t> find_error_positions(std::span<const std::uint8_t> locator) {
    std::vector<std::uint8_t> reversed(locator.rbegin(), locator.rend());
    std::vector<std::size_t> positions;
    positions.reserve(locator.size() - 1U);
    for (std::size_t i = 0; i < kFecCodewordBytes; ++i) {
        if (polynomial_evaluate(reversed, power(static_cast<unsigned>(i))) == 0) {
            positions.push_back(kFecCodewordBytes - 1U - i);
        }
    }
    if (positions.size() != locator.size() - 1U) {
        throw std::runtime_error("Reed-Solomon could not locate all errors");
    }
    return positions;
}

std::vector<std::uint8_t> solve_magnitudes(
    std::span<const std::size_t> positions,
    std::span<const std::uint8_t, kFecParityBytes> syndrome) {
    const auto count = positions.size();
    std::vector<std::vector<std::uint8_t>> matrix(
        count, std::vector<std::uint8_t>(count + 1U, 0));
    for (std::size_t row = 0; row < count; ++row) {
        for (std::size_t column = 0; column < count; ++column) {
            const auto degree = kFecCodewordBytes - 1U - positions[column];
            matrix[row][column] = power(static_cast<unsigned>((row * degree) % 255U));
        }
        matrix[row][count] = syndrome[row];
    }
    for (std::size_t pivot = 0; pivot < count; ++pivot) {
        auto selected = pivot;
        while (selected < count && matrix[selected][pivot] == 0) ++selected;
        if (selected == count) throw std::runtime_error("Reed-Solomon correction matrix is singular");
        if (selected != pivot) std::swap(matrix[selected], matrix[pivot]);
        const auto divisor = inverse(matrix[pivot][pivot]);
        for (std::size_t column = pivot; column <= count; ++column) {
            matrix[pivot][column] = multiply(matrix[pivot][column], divisor);
        }
        for (std::size_t row = 0; row < count; ++row) {
            if (row == pivot || matrix[row][pivot] == 0) continue;
            const auto factor = matrix[row][pivot];
            for (std::size_t column = pivot; column <= count; ++column) {
                matrix[row][column] ^= multiply(factor, matrix[pivot][column]);
            }
        }
    }
    std::vector<std::uint8_t> magnitudes(count);
    for (std::size_t i = 0; i < count; ++i) magnitudes[i] = matrix[i][count];
    return magnitudes;
}

}  // namespace

std::array<std::uint8_t, kFecCodewordBytes> encode_fec_block(
    std::span<const std::uint8_t> data) {
    if (data.size() > kFecDataBytes) {
        throw std::invalid_argument("Reed-Solomon data block is too large");
    }
    std::array<std::uint8_t, kFecCodewordBytes> result{};
    std::copy(data.begin(), data.end(), result.begin());
    auto remainder = result;
    const auto& polynomial = generator();
    for (std::size_t i = 0; i < kFecDataBytes; ++i) {
        const auto coefficient = remainder[i];
        if (coefficient == 0) continue;
        for (std::size_t j = 1; j < polynomial.size(); ++j) {
            remainder[i + j] ^= multiply(polynomial[j], coefficient);
        }
    }
    std::copy(remainder.begin() + kFecDataBytes, remainder.end(),
              result.begin() + kFecDataBytes);
    return result;
}

std::vector<std::uint8_t> decode_fec_block(
    std::span<const std::uint8_t> codeword,
    std::size_t* corrected_errors) {
    if (codeword.size() != kFecCodewordBytes) {
        throw std::invalid_argument("Reed-Solomon codeword must contain 255 bytes");
    }
    std::array<std::uint8_t, kFecCodewordBytes> corrected{};
    std::copy(codeword.begin(), codeword.end(), corrected.begin());
    const auto syndrome = syndromes(corrected);
    if (std::all_of(syndrome.begin(), syndrome.end(),
                    [](std::uint8_t value) { return value == 0; })) {
        if (corrected_errors != nullptr) *corrected_errors = 0;
        return {corrected.begin(), corrected.begin() + kFecDataBytes};
    }
    const auto locator = find_error_locator(syndrome);
    const auto positions = find_error_positions(locator);
    const auto magnitudes = solve_magnitudes(positions, syndrome);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        corrected[positions[i]] ^= magnitudes[i];
    }
    const auto verified = syndromes(corrected);
    if (!std::all_of(verified.begin(), verified.end(),
                     [](std::uint8_t value) { return value == 0; })) {
        throw std::runtime_error("Reed-Solomon correction failed");
    }
    if (corrected_errors != nullptr) *corrected_errors = positions.size();
    return {corrected.begin(), corrected.begin() + kFecDataBytes};
}

}  // namespace acoustic
