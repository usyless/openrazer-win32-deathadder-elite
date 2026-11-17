#pragma once

#include "common.hpp"
#include "razer_protocol.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
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

struct RuntimeBinding {
    ActionKind kind = ActionKind::Default;
    uint32_t vk = 0;
    uint32_t mods = 0;
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
        t.push_back(L'F');
        t.append(to_fixed_wstr(vk - VK_F1 + 1));
        return t;
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE) {
        scan |= KF_EXTENDED;
    }
    std::array<wchar_t, 64> buf{};
    const uint32_t lparam_bits = static_cast<uint32_t>(scan) << 16u;
    const int n = GetKeyNameTextW(static_cast<LONG>(lparam_bits), buf.data(), static_cast<int>(buf.size()));
    if (n > 0) {
        t.append(std::wstring_view{buf.data(), static_cast<size_t>(n)});
        return t;
    }
    t.append(L"VK_");
    t.append(to_fixed_wstr(vk));
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
    if (s.dpi_stages.empty()) {
        s.dpi_stages = {400, 800, 1600, 3200};
    }

    for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); ++i) {
        auto& b = get_binding(s.buttons, static_cast<ButtonId>(i));
        b.kind = action_kind_name(parse_action_kind(b.kind));
    }
}

inline constexpr const wchar_t* RUN_REG_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

[[nodiscard]] inline bool is_startup_enabled() noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_REG_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS st = RegQueryValueExW(key, L"da-elite", nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t);
}

inline bool set_startup_enabled(bool enable) {
    HKEY key = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_REG_KEY,
                                 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (st != ERROR_SUCCESS) {
        SetLastError(static_cast<DWORD>(st));
        return false;
    }
    if (enable) {
        const std::wstring cmd = ulp::str::concat_strings(L"\"", exe_path().native(), L"\" --tray");
        st = RegSetValueExW(key, L"da-elite", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, L"da-elite");
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) {
        SetLastError(static_cast<DWORD>(st));
        return false;
    }
    return true;
}

inline constexpr auto JSON_READ = glz::opts{.error_on_unknown_keys = false};
inline constexpr auto JSON_WRITE = glz::opts{.prettify = true};

[[nodiscard]] inline Settings load_settings(bool* parse_ok = nullptr) {
    Settings s{};
    if (parse_ok != nullptr) *parse_ok = true;
    const fs::path& path = settings_path();
    std::error_code ec;
    const bool existed = fs::exists(path, ec);
    if (auto bytes = usylibpp::files::read_as_bytes(path)) {
        thread_local glz::context ctx{};
        const auto err = glz::read<JSON_READ>(s, *bytes, ctx);
        if (err && parse_ok != nullptr) *parse_ok = false;
    }
    sanitize_settings(s);
    s.launch_on_startup = is_startup_enabled();
    if (!existed) {
        thread_local std::string scratch;
        thread_local glz::context ctx{};
        scratch.clear();
        if (!glz::write<JSON_WRITE>(s, scratch, ctx)) {
            (void)write_file_bytes(path.c_str(), scratch);
        }
    }
    return s;
}

inline bool save_settings(const Settings& s, std::string& json_scratch) {
    const fs::path& path = settings_path();
    json_scratch.clear();
    thread_local glz::context ctx{};
    if (glz::write<JSON_WRITE>(s, json_scratch, ctx)) return false;

    FixedWString<512> tmp_stack;
    std::wstring tmp_heap;
    const auto pid = to_fixed_wstr(GetCurrentProcessId());
    const auto tid = to_fixed_wstr(GetCurrentThreadId());
    const std::wstring_view pview = path.native();
    const wchar_t* tmp_ptr = nullptr;
    if (pview.size() + 5 + pid.size() + 1 + tid.size() <= tmp_stack.capacity()) {
        tmp_stack.assign(pview);
        tmp_stack.append(L".tmp.");
        tmp_stack.append(pid);
        tmp_stack.push_back(L'.');
        tmp_stack.append(tid);
        tmp_ptr = tmp_stack.c_str();
    } else {
        tmp_heap = ulp::str::concat_strings(pview, L".tmp.", pid, L".", tid);
        tmp_ptr = tmp_heap.c_str();
    }

    if (!write_file_bytes(tmp_ptr, json_scratch)) return false;
    if (MoveFileExW(tmp_ptr, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    const DWORD err = GetLastError();
    DeleteFileW(tmp_ptr);
    SetLastError(err);
    return false;
}

inline bool save_settings(const Settings& s) {
    thread_local std::string scratch;
    return save_settings(s, scratch);
}

} // namespace da_elite
