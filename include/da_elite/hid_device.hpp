#pragma once

#include "common.hpp"
#include "razer_protocol.hpp"

#include <setupapi.h>
#include <wil/resource.h>

extern "C" {
#include <hidsdi.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace da_elite {

struct HidCollectionInfo {
    std::wstring path;
    int interface_num = -1;
    int collection_num = -1;
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    uint16_t feature_len = 0;
    uint16_t input_len = 0;
    uint16_t output_len = 0;
};

namespace detail {

[[nodiscard]] inline int parse_hex_tag(std::wstring_view lower_path, std::wstring_view tag) noexcept {
    const size_t pos = lower_path.find(tag);
    if (pos == std::wstring_view::npos) return -1;
    const size_t start = pos + tag.size();
    if (start + 2 > lower_path.size()) return -1;
    unsigned int value = 0;
    for (size_t i = 0; i < 2; ++i) {
        const wchar_t c = lower_path[start + i];
        unsigned int digit = 0;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<unsigned int>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            digit = 10u + static_cast<unsigned int>(c - L'a');
        } else {
            return -1;
        }
        value = (value << 4u) | digit;
    }
    return static_cast<int>(value);
}

[[nodiscard]] inline wil::unique_hfile open_hid_handle(const std::wstring& path, bool writable) noexcept {
    std::array<DWORD, 3> access_modes{};
    size_t count = 0;
    if (writable) {
        access_modes[count++] = GENERIC_READ | GENERIC_WRITE;
        access_modes[count++] = GENERIC_WRITE;
    }
    access_modes[count++] = 0; // FILE_ANY_ACCESS fallback for OS-claimed mouse/kbd collections

    for (size_t i = 0; i < count; ++i) {
        wil::unique_hfile h{CreateFileW(
            path.c_str(), access_modes[i], FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr)};
        if (h) return h;
    }
    return {};
}

[[nodiscard]] inline std::vector<std::wstring> enumerate_deathadder_paths() {
    std::vector<std::wstring> paths;
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);

    const HDEVINFO raw_set = SetupDiGetClassDevsW(
        &hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (raw_set == INVALID_HANDLE_VALUE) return paths;

    const auto cleanup = wil::scope_exit([raw_set] { SetupDiDestroyDeviceInfoList(raw_set); });

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(if_data);

    for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(raw_set, nullptr, &hid_guid, idx, &if_data); ++idx) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(raw_set, &if_data, nullptr, 0, &required, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        std::vector<uint8_t> buf(required, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(raw_set, &if_data, detail, required, nullptr, nullptr)) {
            continue;
        }

        const std::wstring path{detail->DevicePath};
        const std::wstring lower = lower_wide(path);
        if (!lower.contains(L"vid_1532") || !lower.contains(L"pid_005c")) {
            continue;
        }
        paths.push_back(path);
    }
    return paths;
}

[[nodiscard]] inline std::optional<HidCollectionInfo> inspect_hid_path(const std::wstring& path) {
    const wil::unique_hfile h = open_hid_handle(path, false);
    if (!h) return std::nullopt;

    HIDD_ATTRIBUTES attrs{};
    attrs.Size = sizeof(attrs);
    if (!HidD_GetAttributes(h.get(), &attrs)) return std::nullopt;
    if (attrs.VendorID != RAZER_VID || attrs.ProductID != DEATHADDER_ELITE_PID) return std::nullopt;

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(h.get(), &preparsed) || preparsed == nullptr) return std::nullopt;
    const auto free_pp = wil::scope_exit([preparsed] { HidD_FreePreparsedData(preparsed); });

    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) return std::nullopt;

    const std::wstring lower = lower_wide(path);
    HidCollectionInfo info;
    info.path = path;
    info.interface_num = parse_hex_tag(lower, L"&mi_");
    info.collection_num = parse_hex_tag(lower, L"&col");
    info.usage_page = caps.UsagePage;
    info.usage = caps.Usage;
    info.feature_len = caps.FeatureReportByteLength;
    info.input_len = caps.InputReportByteLength;
    info.output_len = caps.OutputReportByteLength;
    return info;
}

} // namespace detail

/// Thread-safe HID control channel to a Razer DeathAdder Elite (1532:005C).
class DeathAdderEliteDevice {
public:
    DeathAdderEliteDevice() = default;
    ~DeathAdderEliteDevice() = default;
    DeathAdderEliteDevice(const DeathAdderEliteDevice&) = delete;
    DeathAdderEliteDevice& operator=(const DeathAdderEliteDevice&) = delete;
    DeathAdderEliteDevice(DeathAdderEliteDevice&&) = delete;
    DeathAdderEliteDevice& operator=(DeathAdderEliteDevice&&) = delete;

    [[nodiscard]] static std::vector<HidCollectionInfo> list_collections() {
        std::vector<HidCollectionInfo> out;
        for (const auto& path : detail::enumerate_deathadder_paths()) {
            if (auto info = detail::inspect_hid_path(path)) {
                out.push_back(std::move(*info));
            }
        }
        return out;
    }

    bool connect() {
        const std::scoped_lock lock{mu_};
        return connect_locked();
    }

    void disconnect() noexcept {
        const std::scoped_lock lock{mu_};
        handle_.reset();
        control_info_.reset();
    }

    [[nodiscard]] bool is_connected() const noexcept {
        const std::scoped_lock lock{mu_};
        return static_cast<bool>(handle_);
    }

    [[nodiscard]] std::optional<HidCollectionInfo> control_info() const {
        const std::scoped_lock lock{mu_};
        return control_info_;
    }

    [[nodiscard]] std::string last_error() const {
        const std::scoped_lock lock{mu_};
        return last_error_;
    }

    [[nodiscard]] std::optional<std::string> get_firmware_version() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_firmware_version(), resp)) return std::nullopt;
        return "v" + std::to_string(resp.arguments[0]) + "." + std::to_string(resp.arguments[1]);
    }

    [[nodiscard]] std::optional<std::string> get_serial() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_serial(), resp)) return std::nullopt;
        std::string serial;
        for (size_t i = 0; i < 22; ++i) {
            const uint8_t ch = resp.arguments[i];
            if (ch == 0) break;
            if (ch >= 0x20 && ch <= 0x7E) serial.push_back(static_cast<char>(ch));
        }
        if (serial.empty()) return std::nullopt;
        return serial;
    }

    [[nodiscard]] std::optional<uint16_t> get_polling_rate() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_polling_rate(), resp)) return std::nullopt;
        return protocol::decode_poll_rate(resp.arguments[0]);
    }

    bool set_polling_rate(uint16_t hz) {
        if (!protocol::is_valid_poll_rate(hz)) return false;
        protocol::RazerReport resp{};
        return send_command(protocol::make_set_polling_rate(hz), resp);
    }

    [[nodiscard]] std::optional<std::pair<uint16_t, uint16_t>> get_dpi() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_dpi_xy(), resp)) return std::nullopt;
        const auto x = static_cast<uint16_t>((static_cast<unsigned>(resp.arguments[1]) << 8u) | resp.arguments[2]);
        const auto y = static_cast<uint16_t>((static_cast<unsigned>(resp.arguments[3]) << 8u) | resp.arguments[4]);
        return std::pair{x, y};
    }

    bool set_dpi(uint16_t dpi_x, uint16_t dpi_y) {
        protocol::RazerReport resp{};
        return send_command(protocol::make_set_dpi_xy(dpi_x, dpi_y), resp);
    }

    [[nodiscard]] std::optional<protocol::DeviceMode> get_device_mode() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_device_mode(), resp)) return std::nullopt;
        return (resp.arguments[0] == 0x03) ? protocol::DeviceMode::Driver : protocol::DeviceMode::Normal;
    }

    bool set_device_mode(protocol::DeviceMode mode) {
        protocol::RazerReport resp{};
        return send_command(protocol::make_set_device_mode(mode), resp);
    }

private:
    static constexpr int MAX_ATTEMPTS = 5;

    bool connect_locked() {
        const auto collections = list_collections();
        std::vector<HidCollectionInfo> controls;
        for (const auto& c : collections) {
            if (c.feature_len == protocol::FEATURE_BUFFER_SIZE) {
                controls.push_back(c);
            }
        }
        if (controls.empty()) {
            handle_.reset();
            control_info_.reset();
            last_error_ = "Razer DeathAdder Elite (1532:005C) not found.";
            return false;
        }

        // Prefer USB interface 0 (&mi_00) and lowest collection number, matching OpenRazer.
        std::ranges::sort(controls, [](const HidCollectionInfo& a, const HidCollectionInfo& b) {
            const bool a_mi0 = (a.interface_num == 0);
            const bool b_mi0 = (b.interface_num == 0);
            if (a_mi0 != b_mi0) return a_mi0 > b_mi0;
            if (a.interface_num != b.interface_num) return a.interface_num < b.interface_num;
            return a.collection_num < b.collection_num;
        });

        for (const auto& candidate : controls) {
            wil::unique_hfile h = detail::open_hid_handle(candidate.path, true);
            if (h) {
                handle_ = std::move(h);
                control_info_ = candidate;
                last_error_.clear();
                return true;
            }
        }

        handle_.reset();
        control_info_.reset();
        last_error_ = "Could not open HID control handle: " + win32_error_utf8(GetLastError());
        return false;
    }

    bool send_command(const protocol::RazerReport& request, protocol::RazerReport& response) {
        const std::scoped_lock lock{mu_};
        if (!handle_ && !connect_locked()) return false;

        const auto packed = protocol::pack_feature_report(request);
        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
            if (!handle_ && !connect_locked()) return false;

            std::array<uint8_t, protocol::FEATURE_BUFFER_SIZE> out_buf = packed;
            if (!HidD_SetFeature(handle_.get(), out_buf.data(), static_cast<ULONG>(out_buf.size()))) {
                last_error_ = "HidD_SetFeature failed: " + win32_error_utf8(GetLastError());
                handle_.reset();
                Sleep(10);
                continue;
            }

            Sleep(2);

            bool got_reply = false;
            for (int poll = 0; poll < MAX_ATTEMPTS; ++poll) {
                std::array<uint8_t, protocol::FEATURE_BUFFER_SIZE> in_buf{};
                in_buf[0] = 0x00;
                if (!HidD_GetFeature(handle_.get(), in_buf.data(), static_cast<ULONG>(in_buf.size()))) {
                    last_error_ = "HidD_GetFeature failed: " + win32_error_utf8(GetLastError());
                    handle_.reset();
                    got_reply = false;
                    break;
                }

                response = protocol::unpack_feature_report(in_buf);
                got_reply = true;
                if (!protocol::response_matches(request, response) ||
                    response.status != static_cast<uint8_t>(protocol::Status::Busy)) {
                    break;
                }
                Sleep(2);
            }

            if (got_reply && protocol::response_matches(request, response) &&
                (response.status == static_cast<uint8_t>(protocol::Status::Successful) ||
                 response.status == static_cast<uint8_t>(protocol::Status::Busy))) {
                last_error_.clear();
                return true;
            }

            if (got_reply && response.status == static_cast<uint8_t>(protocol::Status::NotSupported)) {
                last_error_ = "Command not supported by device firmware.";
                return false;
            }

            Sleep(10);
        }

        if (last_error_.empty()) {
            last_error_ = "Device command failed or timed out (status=" + std::to_string(response.status) + ").";
        }
        return false;
    }

    mutable std::mutex mu_;
    wil::unique_hfile handle_;
    std::optional<HidCollectionInfo> control_info_;
    std::string last_error_;
};

} // namespace da_elite
