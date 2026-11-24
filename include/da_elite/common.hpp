#pragma once

#include <da_elite_version.h>

#include <usylibpp/usylibpp.hpp>

#include <windows.h>
#include <wil/resource.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace da_elite {

namespace fs = std::filesystem;

using ulp::str::FixedBasicString;
using ulp::str::FixedString;
using ulp::str::FixedWString;
using ulp::str::append_to;
using ulp::str::concat_to;
using ulp::str::concat_utf8;
using ulp::str::concat_wide;
using ulp::str::overwrite_to;
using ulp::str::to_fixed_str;
using ulp::str::to_fixed_wstr;
using ulp::str::to_utf8;
using ulp::str::to_wide;

inline constexpr const wchar_t* APP_NAME = L"DeathAdder Elite";
inline constexpr const wchar_t* APP_VERSION = DA_ELITE_VERSION_WSTR;
inline constexpr const wchar_t* SERVICE_CLASS = L"da_elite.service";
inline constexpr const wchar_t* CONFIG_CLASS = L"da_elite.config";
inline constexpr const wchar_t* INSTANCE_MUTEX = L"Local\\da_elite.instance";

inline constexpr uint16_t RAZER_VID = 0x1532;
inline constexpr uint16_t DEATHADDER_ELITE_PID = 0x005C;

enum class AppCmd : uint32_t {
    ShowConfig = 0x4441'0001,
    ApplySettings,
    Quit,
};

[[nodiscard]] inline std::optional<long> parse_long(std::wstring_view sv) noexcept {
    if (sv.empty() || sv.size() >= 32) return std::nullopt;
    FixedString<32> ascii;
    for (const wchar_t c : sv) {
        if (c > 0x7F) return std::nullopt;
        ascii.push_back(static_cast<char>(c));
    }
    long val = 0;
    const auto [ptr, ec] = std::from_chars(ascii.data(), ascii.data() + ascii.size(), val);
    if (ec != std::errc{} || ptr != ascii.data() + ascii.size()) return std::nullopt;
    return val;
}

[[nodiscard]] constexpr wchar_t fold_ascii_lower(wchar_t c) noexcept {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}

[[nodiscard]] constexpr size_t find_ascii_icase(std::wstring_view haystack, std::wstring_view needle) noexcept {
    if (needle.empty()) return 0;
    if (needle.size() > haystack.size()) return std::wstring_view::npos;
    const size_t limit = haystack.size() - needle.size();
    for (size_t i = 0; i <= limit; ++i) {
        size_t j = 0;
        while (j < needle.size() && fold_ascii_lower(haystack[i + j]) == fold_ascii_lower(needle[j])) {
            ++j;
        }
        if (j == needle.size()) return i;
    }
    return std::wstring_view::npos;
}

[[nodiscard]] constexpr bool contains_ascii_icase(std::wstring_view haystack, std::wstring_view needle) noexcept {
    return find_ascii_icase(haystack, needle) != std::wstring_view::npos;
}

inline bool write_file_bytes(const wchar_t* path, std::string_view data) noexcept {
    if (path == nullptr) return false;
    const wil::unique_hfile h{CreateFileW(
        path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!h) return false;
    size_t written_total = 0;
    while (written_total < data.size()) {
        const size_t remaining = data.size() - written_total;
        const auto chunk = static_cast<DWORD>(remaining > 0x7FFF'FFFFu ? 0x7FFF'FFFFu : remaining);
        DWORD wrote = 0;
        if (!WriteFile(h.get(), data.data() + written_total, chunk, &wrote, nullptr) || wrote == 0) {
            return false;
        }
        written_total += wrote;
    }
    return true;
}

[[nodiscard]] inline std::string win32_error_utf8(DWORD err) {
    LPWSTR msg = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, nullptr, err, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    const auto free_msg = wil::scope_exit([msg] noexcept {
        if (msg != nullptr) LocalFree(msg);
    });
    std::wstring_view sv;
    if (len > 0 && msg != nullptr) {
        sv = std::wstring_view{msg, len};
        while (!sv.empty() && (sv.back() == L'\r' || sv.back() == L'\n' || sv.back() == L' ')) {
            sv.remove_suffix(1);
        }
    }
    return sv.empty() ? concat_utf8("WinError ", to_fixed_str(err)) : to_utf8(sv);
}

[[nodiscard]] inline const fs::path& exe_path() {
    static const fs::path p = [] {
        std::wstring buf;
        size_t cap = MAX_PATH;
        for (;;) {
            bool done = false;
            buf.resize_and_overwrite(cap, [&](wchar_t* ptr, size_t n) noexcept {
                const DWORD len = GetModuleFileNameW(nullptr, ptr, static_cast<DWORD>(n));
                if (len < n) {
                    done = true;
                    return static_cast<size_t>(len);
                }
                return n;
            });
            if (done || cap >= 65536u) break;
            cap *= 2;
        }
        return fs::path{std::move(buf)};
    }();
    return p;
}

[[nodiscard]] inline const fs::path& exe_dir() {
    static const fs::path p = exe_path().parent_path();
    return p;
}

[[nodiscard]] inline const fs::path& data_dir() {
    static const fs::path p = [] {
        const fs::path d = exe_dir() / L"data";
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }();
    return p;
}

[[nodiscard]] inline const fs::path& settings_path() {
    static const fs::path p = data_dir() / L"settings.json";
    return p;
}

[[nodiscard]] inline HWND find_service_window() noexcept {
    return FindWindowW(SERVICE_CLASS, nullptr);
}

inline bool send_app_cmd(HWND target, AppCmd cmd) noexcept {
    if (target == nullptr) return false;
    COPYDATASTRUCT cds{};
    cds.dwData = static_cast<ULONG_PTR>(cmd);
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                               SMTO_ABORTIFHUNG, 2000, &result) != 0;
}

} // namespace da_elite
