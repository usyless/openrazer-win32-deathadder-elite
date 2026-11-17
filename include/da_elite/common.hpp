#pragma once

#include <da_elite_version.h>

#include <usylibpp/usylibpp.hpp>

#include <windows.h>
#include <wil/resource.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace da_elite {

namespace fs = std::filesystem;

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

[[nodiscard]] inline std::string to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

[[nodiscard]] inline std::wstring to_wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
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

[[nodiscard]] inline std::wstring lower_wide(std::wstring_view s) {
    std::wstring out{s};
    if (!out.empty()) {
        CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    }
    return out;
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
    std::wstring out;
    if (len > 0 && msg != nullptr) {
        out.assign(msg, len);
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ')) {
            out.pop_back();
        }
    }
    if (msg != nullptr) LocalFree(msg);
    if (out.empty()) return "WinError " + std::to_string(err);
    return to_utf8(out);
}

[[nodiscard]] inline const fs::path& exe_path() {
    static const fs::path p = [] {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return fs::path{buf};
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
