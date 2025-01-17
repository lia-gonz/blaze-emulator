#pragma once

#include <cstdint>
#include <sstream>

namespace Blaze {
	// NOLINTBEGIN(readability-magic-numbers, readability-identifier-length, bugprone-easily-swappable-parameters)
	static constexpr uint16_t concat16(uint8_t hi, uint8_t lo) {
		return (static_cast<uint16_t>(hi) << 8) | lo;
	};

	static constexpr uint32_t concat24(uint8_t hi, uint16_t lo) {
		return (static_cast<uint32_t>(hi) << 16) | lo;
	};

	static constexpr uint32_t concat24(uint8_t hi, uint8_t mid, uint8_t lo) {
		return concat24(hi, concat16(mid, lo));
	};

	static constexpr void split16(uint16_t val, uint8_t& hi, uint8_t& lo) {
		hi = static_cast<uint8_t>((val >> 8) & 0xff);
		lo = static_cast<uint8_t>(val & 0xff);
	};

	static constexpr void split24(uint32_t val, uint8_t& hi, uint16_t& lo) {
		hi = static_cast<uint8_t>((val >> 16) & 0xff);
		lo = static_cast<uint16_t>(val & 0xffff);
	};

	static constexpr void split24(uint32_t val, uint8_t hi, uint8_t mid, uint8_t lo) {
		uint16_t tmp = 0;
		split24(val, hi, tmp);
		split16(tmp, mid, lo);
	};

	static constexpr bool msb8(uint8_t value) {
		return (value & 0x80) != 0;
	};

	static constexpr bool msb16(uint16_t value) {
		return (value & 0x8000) != 0;
	};

	/**
	 * Like `msb8()` and `msb16()`, except you can choose whether the value is 8-bit or 16-bit at runtime.
	 *
	 * If `get8BitMSB` is false, returns the 16-bit MSB.
	 * If `get8BitMSB` is true, returns the 8-bit MSB.
	 */
	static constexpr bool msb(uint16_t value, bool get8BitMSB) {
		return get8BitMSB ? msb8(value) : msb16(value);
	};

	template<typename T>
	static constexpr T lo8(T value) {
		return value & static_cast<T>(0xff);
	};

	template<typename T>
	static constexpr T lo16(T value) {
		return value & static_cast<T>(0xffff);
	};

	template<typename T>
	static constexpr T lo(T value, bool limitTo8Bits) {
		return limitTo8Bits ? lo8(value) : lo16(value);
	};

	template<typename T>
	static constexpr T hi8(T value, bool shift) {
		size_t typeBits = sizeof(T) * 8;
		auto shiftBits = typeBits - 8;
		auto unshifted = value & (static_cast<T>(0xff) << shiftBits);
		return shift ? (unshifted >> shiftBits) : unshifted;
	};

	template<typename T>
	static constexpr T hi16(T value, bool shift) {
		size_t typeBits = sizeof(T) * 8;
		auto shiftBits = typeBits - 16;
		auto unshifted = value & (static_cast<T>(0xffff) << shiftBits);
		return shift ? (unshifted >> shiftBits) : unshifted;
	};

	static std::string valueToHexString(uint32_t value) {
		std::stringstream stream;
		stream << std::hex << std::nouppercase << value;
		return stream.str();
	};
	// NOLINTEND(readability-magic-numbers, readability-identifier-length, bugprone-easily-swappable-parameters)
}; // namespace Blaze
