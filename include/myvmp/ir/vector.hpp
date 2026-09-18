#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace myvmp::ir {

enum class VectorLaneWidth : std::uint8_t {
    Bits8 = 1,
    Bits16 = 2,
    Bits32 = 4,
    Bits64 = 8,
};

enum class VectorBinaryOperation : std::uint8_t {
    Add,
    Subtract,
    And,
    Or,
    Xor,
};

class Vector128 {
public:
    static constexpr std::size_t kBytes = 16;

    constexpr Vector128() = default;
    explicit constexpr Vector128(std::array<std::uint8_t, kBytes> bytes) : bytes_(bytes) {}

    template <typename T>
    [[nodiscard]] T lane(std::size_t index) const {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        constexpr auto width = sizeof(T);
        if (index >= kBytes / width) throw std::out_of_range("SIMD lane index out of range");
        T value = 0;
        for (std::size_t byte = 0; byte < width; ++byte) {
            value |= static_cast<T>(bytes_[index * width + byte]) << (byte * 8u);
        }
        return value;
    }

    template <typename T>
    void set_lane(std::size_t index, T value) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        constexpr auto width = sizeof(T);
        if (index >= kBytes / width) throw std::out_of_range("SIMD lane index out of range");
        for (std::size_t byte = 0; byte < width; ++byte) {
            bytes_[index * width + byte] = static_cast<std::uint8_t>(value >> (byte * 8u));
        }
    }

    template <typename T, typename Operation>
    [[nodiscard]] static Vector128 map_binary(const Vector128& lhs,
                                              const Vector128& rhs,
                                              Operation operation) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        Vector128 result;
        for (std::size_t lane_index = 0; lane_index < kBytes / sizeof(T); ++lane_index) {
            result.set_lane<T>(lane_index, static_cast<T>(operation(lhs.lane<T>(lane_index),
                                                                    rhs.lane<T>(lane_index))));
        }
        return result;
    }

    [[nodiscard]] const std::array<std::uint8_t, kBytes>& bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] static Vector128 apply(VectorBinaryOperation operation,
                                         VectorLaneWidth width,
                                         const Vector128& lhs,
                                         const Vector128& rhs) {
        if (operation == VectorBinaryOperation::And || operation == VectorBinaryOperation::Or ||
            operation == VectorBinaryOperation::Xor) {
            Vector128 result;
            for (std::size_t index = 0; index < kBytes; ++index) {
                if (operation == VectorBinaryOperation::And) result.bytes_[index] = lhs.bytes_[index] & rhs.bytes_[index];
                if (operation == VectorBinaryOperation::Or) result.bytes_[index] = lhs.bytes_[index] | rhs.bytes_[index];
                if (operation == VectorBinaryOperation::Xor) result.bytes_[index] = lhs.bytes_[index] ^ rhs.bytes_[index];
            }
            return result;
        }
        const auto arithmetic = [operation](auto left, auto right) {
            return operation == VectorBinaryOperation::Add ? left + right : left - right;
        };
        switch (width) {
        case VectorLaneWidth::Bits8: return map_binary<std::uint8_t>(lhs, rhs, arithmetic);
        case VectorLaneWidth::Bits16: return map_binary<std::uint16_t>(lhs, rhs, arithmetic);
        case VectorLaneWidth::Bits32: return map_binary<std::uint32_t>(lhs, rhs, arithmetic);
        case VectorLaneWidth::Bits64: return map_binary<std::uint64_t>(lhs, rhs, arithmetic);
        }
        throw std::invalid_argument("invalid SIMD lane width");
    }

    friend constexpr bool operator==(const Vector128&, const Vector128&) = default;

private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

using VectorRegisterFile = std::array<Vector128, 32>;

} // namespace myvmp::ir
