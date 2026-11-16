#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace da_elite::protocol {

inline constexpr size_t REPORT_SIZE = 90;
inline constexpr size_t FEATURE_BUFFER_SIZE = REPORT_SIZE + 1;
inline constexpr size_t ARGUMENT_SIZE = 80;

inline constexpr uint8_t DEATHADDER_ELITE_TX_ID = 0x3F;
inline constexpr uint16_t MIN_DPI = 100;
inline constexpr uint16_t MAX_DPI = 16000;

enum class Status : uint8_t {
    NewCommand = 0x00,
    Busy = 0x01,
    Successful = 0x02,
    Failure = 0x03,
    Timeout = 0x04,
    NotSupported = 0x05,
};

enum class VarStore : uint8_t {
    NoStore = 0x00,
    VarStore = 0x01,
};

enum class DeviceMode : uint8_t {
    Normal = 0x00,
    Driver = 0x03,
};

enum class PollRateCode : uint8_t {
    Hz1000 = 0x01,
    Hz500 = 0x02,
    Hz125 = 0x08,
};

#pragma pack(push, 1)
struct RazerReport {
    uint8_t status = 0x00;
    uint8_t transaction_id = DEATHADDER_ELITE_TX_ID;
    uint16_t remaining_packets = 0x0000; // big endian
    uint8_t protocol_type = 0x00;
    uint8_t data_size = 0x00;
    uint8_t command_class = 0x00;
    uint8_t command_id = 0x00;
    std::array<uint8_t, ARGUMENT_SIZE> arguments{};
    uint8_t crc = 0x00;
    uint8_t reserved = 0x00;
};
#pragma pack(pop)

static_assert(sizeof(RazerReport) == REPORT_SIZE);

/// Computes the Razer protocol checksum: XOR of bytes [2..87] inclusive.
[[nodiscard]] constexpr uint8_t calculate_crc(std::span<const uint8_t, REPORT_SIZE> raw) noexcept {
    uint8_t crc = 0;
    for (size_t i = 2; i < 88; ++i) {
        crc ^= raw[i];
    }
    return crc;
}

[[nodiscard]] inline std::array<uint8_t, FEATURE_BUFFER_SIZE> pack_feature_report(RazerReport report) noexcept {
    std::array<uint8_t, REPORT_SIZE> body{};
    report.crc = 0;
    std::memcpy(body.data(), &report, REPORT_SIZE);
    body[88] = calculate_crc(body);

    std::array<uint8_t, FEATURE_BUFFER_SIZE> out{};
    out[0] = 0x00; // HID Report ID 0
    std::ranges::copy(body, out.begin() + 1);
    return out;
}

[[nodiscard]] inline RazerReport unpack_feature_report(std::span<const uint8_t, FEATURE_BUFFER_SIZE> raw) noexcept {
    RazerReport report{};
    std::memcpy(&report, raw.data() + 1, REPORT_SIZE);
    return report;
}

[[nodiscard]] constexpr bool response_matches(const RazerReport& request, const RazerReport& response) noexcept {
    return response.remaining_packets == request.remaining_packets &&
           response.command_class == request.command_class &&
           response.command_id == request.command_id;
}

[[nodiscard]] constexpr uint16_t clamp_dpi(uint16_t dpi) noexcept {
    return std::clamp(dpi, MIN_DPI, MAX_DPI);
}

[[nodiscard]] constexpr uint8_t encode_poll_rate(uint16_t hz) noexcept {
    switch (hz) {
        case 1000: return static_cast<uint8_t>(PollRateCode::Hz1000);
        case 500: return static_cast<uint8_t>(PollRateCode::Hz500);
        case 125: return static_cast<uint8_t>(PollRateCode::Hz125);
        default: return static_cast<uint8_t>(PollRateCode::Hz500);
    }
}

[[nodiscard]] constexpr std::optional<uint16_t> decode_poll_rate(uint8_t code) noexcept {
    switch (code) {
        case 0x01: return uint16_t{1000};
        case 0x02: return uint16_t{500};
        case 0x08: return uint16_t{125};
        default: return std::nullopt;
    }
}

[[nodiscard]] constexpr bool is_valid_poll_rate(uint16_t hz) noexcept {
    return hz == 125 || hz == 500 || hz == 1000;
}

[[nodiscard]] constexpr RazerReport make_report(uint8_t command_class, uint8_t command_id, uint8_t data_size) noexcept {
    RazerReport r{};
    r.transaction_id = DEATHADDER_ELITE_TX_ID;
    r.command_class = command_class;
    r.command_id = command_id;
    r.data_size = data_size;
    return r;
}

[[nodiscard]] constexpr RazerReport make_get_firmware_version() noexcept {
    return make_report(0x00, 0x81, 0x02);
}

[[nodiscard]] constexpr RazerReport make_get_serial() noexcept {
    return make_report(0x00, 0x82, 0x16);
}

[[nodiscard]] constexpr RazerReport make_get_device_mode() noexcept {
    return make_report(0x00, 0x84, 0x02);
}

[[nodiscard]] constexpr RazerReport make_set_device_mode(DeviceMode mode) noexcept {
    RazerReport r = make_report(0x00, 0x04, 0x02);
    r.arguments[0] = (mode == DeviceMode::Driver) ? 0x03 : 0x00;
    r.arguments[1] = 0x00;
    return r;
}

[[nodiscard]] constexpr RazerReport make_get_polling_rate() noexcept {
    return make_report(0x00, 0x85, 0x01);
}

[[nodiscard]] constexpr RazerReport make_set_polling_rate(uint16_t hz) noexcept {
    RazerReport r = make_report(0x00, 0x05, 0x01);
    r.arguments[0] = encode_poll_rate(hz);
    return r;
}

[[nodiscard]] constexpr RazerReport make_get_dpi_xy() noexcept {
    RazerReport r = make_report(0x04, 0x85, 0x07);
    r.arguments[0] = static_cast<uint8_t>(VarStore::NoStore);
    return r;
}

[[nodiscard]] constexpr RazerReport make_set_dpi_xy(uint16_t dpi_x, uint16_t dpi_y) noexcept {
    const unsigned x = clamp_dpi(dpi_x);
    const unsigned y = clamp_dpi(dpi_y);
    RazerReport r = make_report(0x04, 0x05, 0x07);
    r.arguments[0] = static_cast<uint8_t>(VarStore::VarStore);
    r.arguments[1] = static_cast<uint8_t>((x >> 8u) & 0xFFu);
    r.arguments[2] = static_cast<uint8_t>(x & 0xFFu);
    r.arguments[3] = static_cast<uint8_t>((y >> 8u) & 0xFFu);
    r.arguments[4] = static_cast<uint8_t>(y & 0xFFu);
    r.arguments[5] = 0x00;
    r.arguments[6] = 0x00;
    return r;
}

} // namespace da_elite::protocol
