#pragma once

#include "common.hpp"
#include "razer_protocol.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace da_elite {

enum class ButtonId : uint8_t {
    Middle = 0,
    X1,
    X2,
    DpiUp,
    DpiDown,
    Count,
};

enum class ActionKind : uint8_t {
    Default = 0,
    Disabled,
    DpiCycleUp,
    DpiCycleDown,
    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseX1,
    MouseX2,
    KeyCombo,
};

struct ButtonBinding {
    std::string kind = "default";
    uint32_t vk = 0;
    uint32_t mods = 0; // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
};

struct ButtonMappings {
    ButtonBinding middle{};
    ButtonBinding x1{};
    ButtonBinding x2{};
    ButtonBinding dpi_up{};
    ButtonBinding dpi_down{};
};

struct Settings {
    uint16_t polling_rate = 1000;
    uint16_t dpi_x = 800;
    uint16_t dpi_y = 800;
    bool link_xy = true;
    std::vector<uint16_t> dpi_stages = {400, 800, 1600, 3200};
    bool launch_on_startup = false;
    ButtonMappings buttons{};
};

[[nodiscard]] inline ActionKind parse_action_kind(std::string_view kind) noexcept {
    if (kind == "disabled") return ActionKind::Disabled;
    if (kind == "dpi_up") return ActionKind::DpiCycleUp;
    if (kind == "dpi_down") return ActionKind::DpiCycleDown;
    if (kind == "mouse_left") return ActionKind::MouseLeft;
    if (kind == "mouse_right") return ActionKind::MouseRight;
    if (kind == "mouse_middle") return ActionKind::MouseMiddle;
    if (kind == "mouse_x1") return ActionKind::MouseX1;
    if (kind == "mouse_x2") return ActionKind::MouseX2;
    if (kind == "key") return ActionKind::KeyCombo;
    return ActionKind::Default;
}

[[nodiscard]] inline std::string_view action_kind_name(ActionKind kind) noexcept {
    switch (kind) {
        case ActionKind::Default: return "default";
        case ActionKind::Disabled: return "disabled";
        case ActionKind::DpiCycleUp: return "dpi_up";
        case ActionKind::DpiCycleDown: return "dpi_down";
        case ActionKind::MouseLeft: return "mouse_left";
        case ActionKind::MouseRight: return "mouse_right";
        case ActionKind::MouseMiddle: return "mouse_middle";
        case ActionKind::MouseX1: return "mouse_x1";
        case ActionKind::MouseX2: return "mouse_x2";
        case ActionKind::KeyCombo: return "key";
    }
    return "default";
}

[[nodiscard]] inline const ButtonBinding& get_binding(const ButtonMappings& m, ButtonId id) noexcept {
    switch (id) {
        case ButtonId::Middle: return m.middle;
        case ButtonId::X1: return m.x1;
        case ButtonId::X2: return m.x2;
        case ButtonId::DpiUp: return m.dpi_up;
        case ButtonId::DpiDown: return m.dpi_down;
        case ButtonId::Count: break;
    }
    return m.middle;
}

[[nodiscard]] inline ButtonBinding& get_binding(ButtonMappings& m, ButtonId id) noexcept {
    switch (id) {
        case ButtonId::Middle: return m.middle;
        case ButtonId::X1: return m.x1;
        case ButtonId::X2: return m.x2;
        case ButtonId::DpiUp: return m.dpi_up;
        case ButtonId::DpiDown: return m.dpi_down;
        case ButtonId::Count: break;
    }
    return m.middle;
}

inline constexpr size_t MAX_DPI_STAGES = 16;

struct RuntimeBinding {
    ActionKind kind = ActionKind::Default;
    uint32_t vk = 0;
    uint32_t mods = 0;

    friend constexpr bool operator==(const RuntimeBinding&, const RuntimeBinding&) noexcept = default;
};

[[nodiscard]] inline RuntimeBinding to_runtime_binding(const ButtonBinding& b) noexcept {
    return RuntimeBinding{
        .kind = parse_action_kind(b.kind),
        .vk = b.vk,
        .mods = b.mods,
    };
}

[[nodiscard]] inline bool is_binding_remapped(const ButtonBinding& b) noexcept {
    const ActionKind k = parse_action_kind(b.kind);
    if (k == ActionKind::Default) return false;
    if (k == ActionKind::KeyCombo && b.vk == 0) return false;
    return true;
}

[[nodiscard]] inline bool any_mouse_hook_needed(const Settings& s) noexcept {
    return is_binding_remapped(s.buttons.middle) ||
           is_binding_remapped(s.buttons.x1) ||
           is_binding_remapped(s.buttons.x2);
}

[[nodiscard]] inline bool any_driver_mode_needed(const Settings& s) noexcept {
    return is_binding_remapped(s.buttons.dpi_up) ||
           is_binding_remapped(s.buttons.dpi_down);
}

[[nodiscard]] inline FixedWString<96> format_vk_label(uint32_t mods, uint32_t vk) noexcept {
    if (vk == 0) return FixedWString<96>{L"(None)"};
    FixedWString<96> t;
    if ((mods & MOD_CONTROL) != 0) t.append(L"Ctrl+");
    if ((mods & MOD_ALT) != 0) t.append(L"Alt+");
    if ((mods & MOD_SHIFT) != 0) t.append(L"Shift+");
    if ((mods & MOD_WIN) != 0) t.append(L"Win+");

    const auto named_key = [](uint32_t code) noexcept -> std::wstring_view {
        switch (code) {
            case VK_SPACE: return L"Space";
            case VK_RETURN: return L"Enter";
            case VK_TAB: return L"Tab";
            case VK_BACK: return L"Backspace";
            case VK_ESCAPE: return L"Esc";
            case VK_DELETE: return L"Delete";
            case VK_INSERT: return L"Insert";
            case VK_HOME: return L"Home";
            case VK_END: return L"End";
            case VK_PRIOR: return L"Page Up";
            case VK_NEXT: return L"Page Down";
            case VK_UP: return L"Up";
            case VK_DOWN: return L"Down";
            case VK_LEFT: return L"Left";
            case VK_RIGHT: return L"Right";
            case VK_MEDIA_PLAY_PAUSE: return L"Play/Pause";
            case VK_MEDIA_NEXT_TRACK: return L"Next Track";
            case VK_MEDIA_PREV_TRACK: return L"Prev Track";
            case VK_MEDIA_STOP: return L"Stop Media";
            case VK_VOLUME_UP: return L"Volume Up";
            case VK_VOLUME_DOWN: return L"Volume Down";
            case VK_VOLUME_MUTE: return L"Mute";
            case VK_BROWSER_BACK: return L"Browser Back";
            case VK_BROWSER_FORWARD: return L"Browser Forward";
            default: return {};
        }
    };
    if (const std::wstring_view name = named_key(vk); !name.empty()) {
        t.append(name);
        return t;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        t.append(L'F', to_fixed_wstr(vk - VK_F1 + 1));
        return t;
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (vk == VK_DIVIDE || vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_NUMLOCK ||
        vk == VK_APPS || vk == VK_SNAPSHOT) {
        scan |= KF_EXTENDED;
    }
    std::array<wchar_t, 64> buf{};
    const uint32_t lparam_bits = static_cast<uint32_t>(scan) << 16u;
    const int n = GetKeyNameTextW(static_cast<LONG>(lparam_bits), buf.data(), static_cast<int>(buf.size()));
    if (n > 0) {
        t.append(std::wstring_view{buf.data(), static_cast<size_t>(n)});
        return t;
    }
    t.append(L"VK_", to_fixed_wstr(vk));
    return t;
}

inline void sanitize_settings(Settings& s) {
    if (!protocol::is_valid_poll_rate(s.polling_rate)) {
        s.polling_rate = 1000;
    }
    s.dpi_x = protocol::clamp_dpi(s.dpi_x);
    s.dpi_y = s.link_xy ? s.dpi_x : protocol::clamp_dpi(s.dpi_y);

    for (uint16_t& stage : s.dpi_stages) {
        stage = protocol::clamp_dpi(stage);
    }
    std::ranges::sort(s.dpi_stages);
    const auto dup = std::ranges::unique(s.dpi_stages);
    s.dpi_stages.erase(dup.begin(), dup.end());
    if (s.dpi_stages.size() > MAX_DPI_STAGES) {
        s.dpi_stages.resize(MAX_DPI_STAGES);
    }
    if (s.dpi_stages.empty()) {
        s.dpi_stages = {400, 800, 1600, 3200};
    }

    constexpr uint32_t VALID_MODS = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); ++i) {
        auto& b = get_binding(s.buttons, static_cast<ButtonId>(i));
        b.kind = action_kind_name(parse_action_kind(b.kind));
        b.mods &= VALID_MODS;
        if (b.vk > 0xFEu) b.vk = 0;
    }
}

inline constexpr const wchar_t* STARTUP_RUN_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* STARTUP_APPROVED_SUBKEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
inline constexpr const wchar_t* STARTUP_VALUE_NAME = L"da-elite";

/// Evaluates the REG_BINARY value stored under
/// `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run`.
/// Windows uses bit 0 of the first byte to mark a Run entry disabled (`0x03`, `0x01`, `0x05`, `0x07`)
/// vs enabled (`0x02`, `0x06`, `0x00`). Empty/missing data defaults to approved.
[[nodiscard]] constexpr bool is_startup_approved_bytes(std::span<const uint8_t> data) noexcept {
    if (data.empty()) return true;
    return (data[0] & 0x01u) == 0u;
}

/// Candidate executable paths extracted from a Windows `Run` registry command string.
struct StartupCommandCandidates {
    std::wstring_view primary;
    std::wstring_view fallback; // non-empty only for unquoted commands containing whitespace
};

/// Extracts candidate executable path(s) from a `CurrentVersion\Run` command line.
/// - Quoted commands (`"C:\path with spaces\da-elite.exe" --tray`) return the quoted path as `primary`.
/// - Unclosed quotes return empty candidates.
/// - Unquoted commands return the trimmed full string as `primary`, and (if whitespace is present)
///   the leading whitespace-delimited token as `fallback` so both unquoted paths with spaces and
///   unquoted paths with arguments can be checked against the filesystem.
[[nodiscard]] constexpr StartupCommandCandidates extract_startup_command_exe(std::wstring_view cmd) noexcept {
    if (const size_t nul = cmd.find(L'\0'); nul != std::wstring_view::npos) {
        cmd = cmd.substr(0, nul);
    }
    constexpr auto is_ws = [](wchar_t c) noexcept {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!cmd.empty() && is_ws(cmd.front())) cmd.remove_prefix(1);
    while (!cmd.empty() && is_ws(cmd.back())) cmd.remove_suffix(1);
    if (cmd.empty()) return {};

    if (cmd.front() == L'"') {
        const size_t close = cmd.find(L'"', 1);
        if (close == std::wstring_view::npos || close == 1) return {};
        std::wstring_view inside = cmd.substr(1, close - 1);
        while (!inside.empty() && is_ws(inside.front())) inside.remove_prefix(1);
        while (!inside.empty() && is_ws(inside.back())) inside.remove_suffix(1);
        return {.primary = inside, .fallback = {}};
    }

    size_t ws_pos = std::wstring_view::npos;
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (is_ws(cmd[i])) {
            ws_pos = i;
            break;
        }
    }
    if (ws_pos == std::wstring_view::npos) {
        return {.primary = cmd, .fallback = {}};
    }
    return {.primary = cmd, .fallback = cmd.substr(0, ws_pos)};
}

/// True when `HKCU\...\CurrentVersion\Run\da-elite` exists, points to this installation's
/// executable on disk, and is not disabled under `Explorer\StartupApproved\Run`.
[[nodiscard]] inline bool is_startup_enabled() noexcept {
    try {
        wil::unique_hkey key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_RUN_SUBKEY, 0, KEY_READ, key.put()) != ERROR_SUCCESS) {
            return false;
        }
        std::array<wchar_t, MAX_PATH * 2> stack_buf; // NOLINT(cppcoreguidelines-pro-type-member-init)
        auto sz = static_cast<DWORD>(stack_buf.size() * sizeof(wchar_t));
        DWORD type = 0;
        LSTATUS st = RegQueryValueExW(key.get(), STARTUP_VALUE_NAME, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(stack_buf.data()), &sz);
        std::wstring dyn_buf;
        std::wstring_view raw;
        if (st == ERROR_MORE_DATA && (type == REG_SZ || type == REG_EXPAND_SZ) && sz >= sizeof(wchar_t)) {
            dyn_buf.resize((sz + sizeof(wchar_t) - 1) / sizeof(wchar_t));
            st = RegQueryValueExW(key.get(), STARTUP_VALUE_NAME, nullptr, &type,
                                  reinterpret_cast<LPBYTE>(dyn_buf.data()), &sz);
            if (st == ERROR_SUCCESS) raw = std::wstring_view{dyn_buf.data(), sz / sizeof(wchar_t)};
        } else if (st == ERROR_SUCCESS) {
            raw = std::wstring_view{stack_buf.data(), sz / sizeof(wchar_t)};
        }
        key.reset();
        if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || raw.empty()) return false;

        std::wstring expanded;
        std::wstring_view cmd = raw;
        if (type == REG_EXPAND_SZ) {
            std::wstring nul_terminated;
            if (const size_t nul = raw.find(L'\0'); nul != std::wstring_view::npos) {
                nul_terminated.assign(raw.substr(0, nul));
            } else {
                nul_terminated.assign(raw);
            }
            const DWORD need = ExpandEnvironmentStringsW(nul_terminated.c_str(), nullptr, 0);
            if (need == 0) return false;
            expanded.resize(need);
            const DWORD written = ExpandEnvironmentStringsW(nul_terminated.c_str(), expanded.data(), need);
            if (written == 0 || written > need) return false;
            cmd = std::wstring_view{expanded.data(), written};
        }

        const StartupCommandCandidates cands = extract_startup_command_exe(cmd);
        if (cands.primary.empty()) return false;

        const fs::path& expected = exe_path();
        const auto matches_expected = [&](std::wstring_view candidate) -> bool {
            if (candidate.empty()) return false;
            std::error_code ec;
            const fs::path p{candidate};
            if (!fs::is_regular_file(p, ec) || ec) return false;
            return fs::equivalent(p, expected, ec) && !ec;
        };
        if (!matches_expected(cands.primary) && !matches_expected(cands.fallback)) return false;

        wil::unique_hkey approved;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_APPROVED_SUBKEY, 0, KEY_READ, approved.put()) == ERROR_SUCCESS) {
            std::array<uint8_t, 32> stack_bin; // NOLINT(cppcoreguidelines-pro-type-member-init)
            auto bsz = static_cast<DWORD>(stack_bin.size());
            DWORD btype = 0;
            LSTATUS bst = RegQueryValueExW(approved.get(), STARTUP_VALUE_NAME, nullptr, &btype,
                                           stack_bin.data(), &bsz);
            std::vector<uint8_t> dyn_bin;
            std::span<const uint8_t> bin;
            if (bst == ERROR_MORE_DATA && btype == REG_BINARY && bsz > 0) {
                dyn_bin.resize(bsz);
                bst = RegQueryValueExW(approved.get(), STARTUP_VALUE_NAME, nullptr, &btype,
                                       dyn_bin.data(), &bsz);
                if (bst == ERROR_SUCCESS) bin = std::span<const uint8_t>{dyn_bin.data(), std::min<size_t>(bsz, dyn_bin.size())};
            } else if (bst == ERROR_SUCCESS && btype == REG_BINARY && bsz > 0) {
                bin = std::span<const uint8_t>{stack_bin.data(), std::min<size_t>(bsz, stack_bin.size())};
            }
            if (bst == ERROR_SUCCESS && btype == REG_BINARY && !is_startup_approved_bytes(bin)) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

inline bool set_startup_enabled(bool enable) {
    wil::unique_hkey key;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, STARTUP_RUN_SUBKEY,
                                 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.put(), nullptr);
    if (st != ERROR_SUCCESS) {
        SetLastError(static_cast<DWORD>(st));
        return false;
    }
    if (enable) {
        const std::wstring cmd = concat_wide(L"\"", exe_path(), L"\" --tray");
        st = RegSetValueExW(key.get(), STARTUP_VALUE_NAME, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key.get(), STARTUP_VALUE_NAME);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    key.reset();
    if (st != ERROR_SUCCESS) {
        SetLastError(static_cast<DWORD>(st));
        return false;
    }

    // Clear any Windows Task Manager / Settings StartupApproved override so enabling genuinely activates
    // the Run entry and disabling leaves no stale registry state behind.
    wil::unique_hkey approved;
    const LSTATUS open_st = RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_APPROVED_SUBKEY, 0, KEY_SET_VALUE, approved.put());
    if (open_st == ERROR_SUCCESS) {
        LSTATUS del_st = RegDeleteValueW(approved.get(), STARTUP_VALUE_NAME);
        if (del_st == ERROR_FILE_NOT_FOUND) del_st = ERROR_SUCCESS;
        if (del_st != ERROR_SUCCESS) {
            SetLastError(static_cast<DWORD>(del_st));
            return false;
        }
    } else if (open_st != ERROR_FILE_NOT_FOUND) {
        SetLastError(static_cast<DWORD>(open_st));
        return false;
    }
    return true;
}

inline constexpr auto JSON_READ = glz::opts{.error_on_unknown_keys = false};
inline constexpr auto JSON_WRITE = glz::opts{.prettify = true};

inline bool save_settings(const Settings& s, std::string& json_scratch) {
    const fs::path& path = settings_path();
    json_scratch.clear();
    if (glz::write<JSON_WRITE>(s, json_scratch)) return false;

    FixedWString<512> tmp_stack;
    std::wstring tmp_heap;
    const auto pid = to_fixed_wstr(GetCurrentProcessId());
    const auto tid = to_fixed_wstr(GetCurrentThreadId());
    const std::wstring_view pview = path.native();
    const wchar_t* tmp_ptr = nullptr;
    if (pview.size() + 5 + pid.size() + 1 + tid.size() <= FixedWString<512>::capacity()) {
        tmp_stack.assign(pview, L".tmp.", pid, L'.', tid);
        tmp_ptr = tmp_stack.c_str();
    } else {
        tmp_heap = concat_wide(pview, L".tmp.", pid, L'.', tid);
        tmp_ptr = tmp_heap.c_str();
    }

    if (!write_file_bytes(tmp_ptr, json_scratch)) {
        const DWORD err = GetLastError();
        DeleteFileW(tmp_ptr);
        SetLastError(err);
        return false;
    }
    if (MoveFileExW(tmp_ptr, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    const DWORD err = GetLastError();
    DeleteFileW(tmp_ptr);
    SetLastError(err);
    return false;
}

inline bool save_settings(const Settings& s) {
    std::string scratch;
    return save_settings(s, scratch);
}

[[nodiscard]] inline Settings load_settings(bool* parse_ok = nullptr) {
    Settings s{};
    if (parse_ok != nullptr) *parse_ok = true;
    const fs::path& path = settings_path();
    std::error_code ec;
    const bool existed = fs::exists(path, ec);
    if (auto bytes = usylibpp::files::read_as_bytes(path)) {
        const auto err = glz::read<JSON_READ>(s, *bytes);
        if (err) {
            s = Settings{};
            if (parse_ok != nullptr) *parse_ok = false;
        }
    }
    sanitize_settings(s);
    s.launch_on_startup = is_startup_enabled();
    if (!existed) {
        (void)save_settings(s);
    }
    return s;
}

} // namespace da_elite
