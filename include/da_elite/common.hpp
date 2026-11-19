#pragma once

#include <da_elite_version.h>

#include <usylibpp/usylibpp.hpp>

#include <windows.h>
#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
    std::string out;
    out.resize_and_overwrite(static_cast<size_t>(n), [&](char* buf, size_t) noexcept {
        const int written = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), buf, n, nullptr, nullptr);
        return written > 0 ? static_cast<size_t>(written) : 0u;
    });
    return out;
}

[[nodiscard]] inline std::wstring to_wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out;
    out.resize_and_overwrite(static_cast<size_t>(n), [&](wchar_t* buf, size_t) noexcept {
        const int written = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), buf, n);
        return written > 0 ? static_cast<size_t>(written) : 0u;
    });
    return out;
}

inline void append_utf8_as_wide(std::wstring& dst, std::string_view s) {
    if (s.empty()) return;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return;
    const size_t old = dst.size();
    dst.resize_and_overwrite(old + static_cast<size_t>(n), [&](wchar_t* buf, size_t) noexcept {
        const int written = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), buf + old, n);
        return written > 0 ? (old + static_cast<size_t>(written)) : old;
    });
}

/// Fixed-capacity stack string with implicit `std::basic_string_view` conversion and zero heap allocations.
template <typename Char, size_t N>
struct FixedBasicString {
    static_assert(N > 0);
    std::array<Char, N> buf; // NOLINT(cppcoreguidelines-pro-type-member-init)
    size_t len = 0;

    constexpr FixedBasicString() noexcept {
        if consteval { buf.fill(Char{}); }
        buf[0] = Char{};
    }
    constexpr FixedBasicString(const FixedBasicString& other) noexcept {
        if consteval { buf.fill(Char{}); }
        assign(other.view());
    }
    constexpr FixedBasicString& operator=(const FixedBasicString& other) noexcept {
        if (this != &other) assign(other.view());
        return *this;
    }
    constexpr FixedBasicString(FixedBasicString&& other) noexcept {
        if consteval { buf.fill(Char{}); }
        assign(other.view());
    }
    constexpr FixedBasicString& operator=(FixedBasicString&& other) noexcept {
        if (this != &other) assign(other.view());
        return *this;
    }
    constexpr ~FixedBasicString() noexcept = default;
    constexpr FixedBasicString(std::basic_string_view<Char> sv) noexcept {
        if consteval { buf.fill(Char{}); }
        assign(sv);
    }
    constexpr FixedBasicString(const Char* s) noexcept
        : FixedBasicString(s ? std::basic_string_view<Char>{s} : std::basic_string_view<Char>{}) {}
    template <size_t M>
    constexpr FixedBasicString(const FixedBasicString<Char, M>& other) noexcept {
        if consteval { buf.fill(Char{}); }
        assign(other.view());
    }

    constexpr void clear() noexcept {
        len = 0;
        buf[0] = Char{};
    }

    constexpr void assign(std::basic_string_view<Char> sv) noexcept {
        const size_t n = std::min(sv.size(), N - 1);
        for (size_t i = 0; i < n; ++i) buf[i] = sv[i];
        buf[n] = Char{};
        len = n;
    }

    constexpr void push_back(Char c) noexcept {
        if (len + 1 < N) {
            buf[len++] = c;
            buf[len] = Char{};
        }
    }

    constexpr void append(std::basic_string_view<Char> sv) noexcept {
        const size_t room = (N - 1) - len;
        const size_t n = std::min(sv.size(), room);
        for (size_t i = 0; i < n; ++i) buf[len + i] = sv[i];
        len += n;
        buf[len] = Char{};
    }

    [[nodiscard]] constexpr const Char* data() const noexcept { return buf.data(); }
    [[nodiscard]] constexpr Char* data() noexcept { return buf.data(); }
    [[nodiscard]] constexpr const Char* c_str() const noexcept { return buf.data(); }
    [[nodiscard]] constexpr size_t size() const noexcept { return len; }
    [[nodiscard]] constexpr size_t length() const noexcept { return len; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return N - 1; }
    [[nodiscard]] constexpr bool empty() const noexcept { return len == 0; }
    [[nodiscard]] constexpr const Char* begin() const noexcept { return buf.data(); }
    [[nodiscard]] constexpr const Char* end() const noexcept { return buf.data() + len; }
    [[nodiscard]] constexpr Char operator[](size_t i) const noexcept { return buf[i]; }
    [[nodiscard]] constexpr std::basic_string_view<Char> view() const noexcept { return {buf.data(), len}; }
    [[nodiscard]] constexpr operator std::basic_string_view<Char>() const noexcept { return view(); }
    [[nodiscard]] std::basic_string<Char> str() const { return std::basic_string<Char>{buf.data(), len}; }

    friend constexpr bool operator==(const FixedBasicString& a, std::basic_string_view<Char> b) noexcept {
        return a.view() == b;
    }
    friend constexpr auto operator<=>(const FixedBasicString& a, std::basic_string_view<Char> b) noexcept {
        return a.view() <=> b;
    }
};

template <size_t N>
using FixedString = FixedBasicString<char, N>;

template <size_t N>
using FixedWString = FixedBasicString<wchar_t, N>;

template <typename Char, size_t N, size_t M>
[[nodiscard]] constexpr bool operator==(const FixedBasicString<Char, N>& a,
                                        const FixedBasicString<Char, M>& b) noexcept {
    return a.view() == b.view();
}

template <typename Char, size_t N, size_t M>
[[nodiscard]] constexpr auto operator<=>(const FixedBasicString<Char, N>& a,
                                         const FixedBasicString<Char, M>& b) noexcept {
    return a.view() <=> b.view();
}

/// Appends `parts...` onto `dst` in one capacity check without creating intermediate strings.
template <typename Char, typename... Ts>
constexpr void append_to(std::basic_string<Char>& dst, const Ts&... parts) {
    if constexpr (sizeof...(Ts) == 0) {
        return;
    } else {
        const std::array<std::basic_string_view<Char>, sizeof...(Ts)> views{std::basic_string_view<Char>{parts}...};
        size_t add = 0;
        for (const auto sv : views) add += sv.size();
        if (add == 0) return;

        const size_t old = dst.size();
        if consteval {
            std::basic_string<Char> tmp;
            tmp.reserve(old + add);
            tmp.append(dst);
            for (const auto sv : views) tmp.append(sv);
            dst = std::move(tmp);
            return;
        }

        // Snapshot views that alias `dst` before resize_and_overwrite may reallocate `dst`'s buffer.
        const Char* const old_begin = dst.data();
        const Char* const old_end = old_begin + old;
        std::array<bool, sizeof...(Ts)> self_alias{};
        std::array<size_t, sizeof...(Ts)> self_off{};
        for (size_t i = 0; i < views.size(); ++i) {
            const Char* p = views[i].data();
            if (!views[i].empty() && !std::less<const Char*>{}(p, old_begin) && std::less<const Char*>{}(p, old_end)) {
                self_alias[i] = true;
                self_off[i] = static_cast<size_t>(p - old_begin);
            }
        }

        dst.resize_and_overwrite(old + add, [&](Char* p, size_t) noexcept {
            Char* cur = p + old;
            for (size_t i = 0; i < views.size(); ++i) {
                if (!views[i].empty()) {
                    const Char* src = self_alias[i] ? (p + self_off[i]) : views[i].data();
                    std::char_traits<Char>::copy(cur, src, views[i].size());
                    cur += views[i].size();
                }
            }
            return old + add;
        });
    }
}

template <size_t N = 32, typename T>
    requires(std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline FixedString<N> to_fixed_str(T val) noexcept {
    FixedString<N> out;
    const auto [ptr, ec] = std::to_chars(out.buf.data(), out.buf.data() + out.buf.size() - 1, val);
    out.len = (ec == std::errc{}) ? static_cast<size_t>(ptr - out.buf.data()) : 0u;
    out.buf[out.len] = '\0';
    return out;
}

template <size_t N = 48, std::floating_point T>
[[nodiscard]] inline FixedString<N> to_fixed_str(T val, int precision) noexcept {
    FixedString<N> out;
    const auto [ptr, ec] = std::to_chars(out.buf.data(), out.buf.data() + out.buf.size() - 1, val, std::chars_format::fixed, precision);
    out.len = (ec == std::errc{}) ? static_cast<size_t>(ptr - out.buf.data()) : 0u;
    out.buf[out.len] = '\0';
    return out;
}

template <size_t N = 32, typename T>
    requires(std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline FixedWString<N> to_fixed_wstr(T val) noexcept {
    std::array<char, N> tmp; // NOLINT(cppcoreguidelines-pro-type-member-init)
    const auto [ptr, ec] = std::to_chars(tmp.data(), tmp.data() + tmp.size() - 1, val);
    FixedWString<N> out;
    if (ec == std::errc{}) {
        const auto n = static_cast<size_t>(ptr - tmp.data());
        for (size_t i = 0; i < n; ++i) out.buf[i] = static_cast<wchar_t>(tmp[i]);
        out.len = n;
        out.buf[n] = L'\0';
    }
    return out;
}

template <size_t N = 48, std::floating_point T>
[[nodiscard]] inline FixedWString<N> to_fixed_wstr(T val, int precision) noexcept {
    std::array<char, N> tmp; // NOLINT(cppcoreguidelines-pro-type-member-init)
    const auto [ptr, ec] = std::to_chars(tmp.data(), tmp.data() + tmp.size() - 1, val, std::chars_format::fixed, precision);
    FixedWString<N> out;
    if (ec == std::errc{}) {
        const auto n = static_cast<size_t>(ptr - tmp.data());
        for (size_t i = 0; i < n; ++i) out.buf[i] = static_cast<wchar_t>(tmp[i]);
        out.len = n;
        out.buf[n] = L'\0';
    }
    return out;
}

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
    return sv.empty() ? ulp::str::concat_strings("WinError ", to_fixed_str(err)) : to_utf8(sv);
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
            if (done) break;
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
