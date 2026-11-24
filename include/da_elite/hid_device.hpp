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
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace da_elite {

inline constexpr size_t MAX_HID_PATH_CHARS = 320;
inline constexpr size_t MAX_HID_COLLECTIONS = 16;

struct HidCollectionInfo {
    FixedWString<MAX_HID_PATH_CHARS> path;
    int interface_num = -1;
    int collection_num = -1;
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    uint16_t feature_len = 0;
    uint16_t input_len = 0;
    uint16_t output_len = 0;

    [[nodiscard]] const wchar_t* c_str() const noexcept { return path.c_str(); }
    [[nodiscard]] std::wstring_view path_view() const noexcept { return path.view(); }
};

struct HidCollectionList {
    std::array<HidCollectionInfo, MAX_HID_COLLECTIONS> items{};
    size_t count = 0;

    [[nodiscard]] std::span<const HidCollectionInfo> span() const noexcept { return {items.data(), count}; }
    [[nodiscard]] std::span<HidCollectionInfo> span() noexcept { return {items.data(), count}; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

namespace detail {

[[nodiscard]] inline int parse_hex_tag(std::wstring_view path, std::wstring_view tag) noexcept {
    const size_t pos = find_ascii_icase(path, tag);
    if (pos == std::wstring_view::npos) return -1;
    const size_t start = pos + tag.size();
    if (start + 2 > path.size()) return -1;
    unsigned int value = 0;
    for (size_t i = 0; i < 2; ++i) {
        const wchar_t c = fold_ascii_lower(path[start + i]);
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

[[nodiscard]] inline wil::unique_hfile open_hid_handle(const wchar_t* path, bool writable) noexcept {
    std::array<DWORD, 3> access_modes{};
    size_t count = 0;
    if (writable) {
        access_modes[count++] = GENERIC_READ | GENERIC_WRITE;
        access_modes[count++] = GENERIC_WRITE;
    }
    access_modes[count++] = 0; // FILE_ANY_ACCESS fallback for OS-claimed mouse/kbd collections

    for (size_t i = 0; i < count; ++i) {
        wil::unique_hfile h{CreateFileW(
            path, access_modes[i], FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr)};
        if (h) return h;
    }
    return {};
}

[[nodiscard]] inline bool inspect_hid_path(const wchar_t* path, HidCollectionInfo& info) noexcept {
    const std::wstring_view view{path};
    if (view.size() >= MAX_HID_PATH_CHARS) return false;

    const wil::unique_hfile h = open_hid_handle(path, false);
    if (!h) return false;

    HIDD_ATTRIBUTES attrs{};
    attrs.Size = sizeof(attrs);
    if (!HidD_GetAttributes(h.get(), &attrs)) return false;
    if (attrs.VendorID != RAZER_VID || attrs.ProductID != DEATHADDER_ELITE_PID) return false;

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(h.get(), &preparsed) || preparsed == nullptr) return false;
    const auto free_pp = wil::scope_exit([preparsed] { HidD_FreePreparsedData(preparsed); });

    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) return false;

    info.path.assign(view);
    info.interface_num = parse_hex_tag(view, L"&mi_");
    info.collection_num = parse_hex_tag(view, L"&col");
    info.usage_page = caps.UsagePage;
    info.usage = caps.Usage;
    info.feature_len = caps.FeatureReportByteLength;
    info.input_len = caps.InputReportByteLength;
    info.output_len = caps.OutputReportByteLength;
    return true;
}

inline void enumerate_deathadder_collections(HidCollectionList& out) {
    out.count = 0;
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);

    const HDEVINFO raw_set = SetupDiGetClassDevsW(
        &hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (raw_set == INVALID_HANDLE_VALUE) return;

    const auto cleanup = wil::scope_exit([raw_set] { SetupDiDestroyDeviceInfoList(raw_set); });

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(if_data);

    alignas(SP_DEVICE_INTERFACE_DETAIL_DATA_W) std::array<uint8_t, 1024> stack_buf; // NOLINT(cppcoreguidelines-pro-type-member-init)
    std::vector<uint8_t> heap_buf;

    for (DWORD idx = 0;
         out.count < MAX_HID_COLLECTIONS && SetupDiEnumDeviceInterfaces(raw_set, nullptr, &hid_guid, idx, &if_data);
         ++idx) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(raw_set, &if_data, nullptr, 0, &required, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        uint8_t* raw_buf = stack_buf.data();
        if (required > stack_buf.size()) {
            heap_buf.resize(required);
            raw_buf = heap_buf.data();
        }
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(raw_buf);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(raw_set, &if_data, detail, required, nullptr, nullptr)) {
            continue;
        }

        const size_t max_chars =
            (required - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(wchar_t);
        size_t path_len = 0;
        while (path_len < max_chars && detail->DevicePath[path_len] != L'\0') {
            ++path_len;
        }
        if (path_len == max_chars) continue;

        const std::wstring_view path{detail->DevicePath, path_len};
        if (!contains_ascii_icase(path, L"vid_1532") || !contains_ascii_icase(path, L"pid_005c")) {
            continue;
        }
        if (inspect_hid_path(detail->DevicePath, out.items[out.count])) {
            ++out.count;
        }
    }
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

    static void list_collections(HidCollectionList& out) {
        detail::enumerate_deathadder_collections(out);
    }

    bool connect() {
        const std::scoped_lock lock{mu_};
        return connect_locked();
    }

    void disconnect() noexcept {
        const std::scoped_lock lock{mu_};
        handle_.reset();
        collections_.count = 0;
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

    void copy_collections(HidCollectionList& out) const noexcept {
        const std::scoped_lock lock{mu_};
        out.count = collections_.count;
        for (size_t i = 0; i < collections_.count; ++i) {
            out.items[i] = collections_.items[i];
        }
    }

    [[nodiscard]] std::string last_error() const {
        const std::scoped_lock lock{mu_};
        return last_error_;
    }

    [[nodiscard]] std::optional<FixedString<24>> get_firmware_version() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_firmware_version(), resp)) return std::nullopt;
        FixedString<24> out;
        out.assign("v", to_fixed_str(resp.arguments[0]), '.', to_fixed_str(resp.arguments[1]));
        return out;
    }

    [[nodiscard]] std::optional<FixedString<32>> get_serial() {
        protocol::RazerReport resp{};
        if (!send_command(protocol::make_get_serial(), resp)) return std::nullopt;
        FixedString<32> serial;
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
        list_collections(collections_);

        // Prefer USB interface 0 (&mi_00) and lowest collection number, matching OpenRazer.
        std::ranges::sort(collections_.span(), [](const HidCollectionInfo& a, const HidCollectionInfo& b) {
            const bool a_mi0 = (a.interface_num == 0);
            const bool b_mi0 = (b.interface_num == 0);
            if (a_mi0 != b_mi0) return a_mi0 > b_mi0;
            if (a.interface_num != b.interface_num) return a.interface_num < b.interface_num;
            return a.collection_num < b.collection_num;
        });

        bool any_control = false;
        DWORD last_open_err = ERROR_ACCESS_DENIED;
        for (const auto& candidate : collections_.span()) {
            if (candidate.feature_len != protocol::FEATURE_BUFFER_SIZE) continue;
            any_control = true;
            wil::unique_hfile h = detail::open_hid_handle(candidate.c_str(), true);
            if (h) {
                handle_ = std::move(h);
                control_info_ = candidate;
                last_error_.clear();
                return true;
            }
            last_open_err = GetLastError();
        }

        handle_.reset();
        control_info_.reset();
        if (!any_control) {
            last_error_ = "Razer DeathAdder Elite (1532:005C) not found.";
        } else {
            overwrite_to(last_error_, "Could not open HID control handle: ", win32_error_utf8(last_open_err));
        }
        return false;
    }

    bool send_command(const protocol::RazerReport& request, protocol::RazerReport& response) {
        const std::scoped_lock lock{mu_};
        if (!handle_ && !connect_locked()) return false;

        const auto packed = protocol::pack_feature_report(request);
        const bool is_write_cmd = (request.command_id & 0x80u) == 0;
        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
            if (!handle_ && !connect_locked()) return false;

            std::array<uint8_t, protocol::FEATURE_BUFFER_SIZE> out_buf = packed;
            if (!HidD_SetFeature(handle_.get(), out_buf.data(), static_cast<ULONG>(out_buf.size()))) {
                overwrite_to(last_error_, "HidD_SetFeature failed: ", win32_error_utf8(GetLastError()));
                handle_.reset();
                control_info_.reset();
                if (attempt + 1 < MAX_ATTEMPTS) Sleep(10);
                continue;
            }

            Sleep(2);

            bool got_reply = false;
            for (int poll = 0; poll < MAX_ATTEMPTS; ++poll) {
                std::array<uint8_t, protocol::FEATURE_BUFFER_SIZE> in_buf{};
                in_buf[0] = 0x00;
                if (!HidD_GetFeature(handle_.get(), in_buf.data(), static_cast<ULONG>(in_buf.size()))) {
                    overwrite_to(last_error_, "HidD_GetFeature failed: ", win32_error_utf8(GetLastError()));
                    handle_.reset();
                    control_info_.reset();
                    got_reply = false;
                    break;
                }

                response = protocol::unpack_feature_report(in_buf);
                got_reply = true;
                if (!protocol::response_matches(request, response) ||
                    response.status != static_cast<uint8_t>(protocol::Status::Busy)) {
                    break;
                }
                if (poll + 1 < MAX_ATTEMPTS) Sleep(10);
            }

            const bool matched = got_reply && protocol::response_matches(request, response);
            if (matched &&
                (response.status == static_cast<uint8_t>(protocol::Status::Successful) ||
                 (is_write_cmd && response.status == static_cast<uint8_t>(protocol::Status::Busy)))) {
                last_error_.clear();
                return true;
            }

            if (got_reply) {
                if (matched && response.status == static_cast<uint8_t>(protocol::Status::NotSupported)) {
                    last_error_ = "Command not supported by device firmware.";
                    return false;
                }
                if (!matched) {
                    last_error_ = "Device response did not match request.";
                } else {
                    // Clear any earlier transport error so a final protocol status failure isn't masked.
                    last_error_.clear();
                }
            }

            if (attempt + 1 < MAX_ATTEMPTS) Sleep(10);
        }

        if (last_error_.empty()) {
            append_to(last_error_, "Device command failed or timed out (status=", to_fixed_str(response.status), ").");
        }
        return false;
    }

    mutable std::mutex mu_;
    wil::unique_hfile handle_;
    HidCollectionList collections_{};
    std::optional<HidCollectionInfo> control_info_;
    std::string last_error_;
};

} // namespace da_elite
