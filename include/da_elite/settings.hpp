#pragma once

#include "common.hpp"
#include "razer_protocol.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
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

[[nodiscard]] inline std::wstring format_vk_label(uint32_t mods, uint32_t vk) {
    if (vk == 0) return L"(None)";
    std::wstring t;
    if ((mods & MOD_CONTROL) != 0) t += L"Ctrl+";
    if ((mods & MOD_ALT) != 0) t += L"Alt+";
    if ((mods & MOD_SHIFT) != 0) t += L"Shift+";
    if ((mods & MOD_WIN) != 0) t += L"Win+";

    switch (vk) {
        case VK_SPACE: return t + L"Space";
        case VK_RETURN: return t + L"Enter";
        case VK_TAB: return t + L"Tab";
        case VK_BACK: return t + L"Backspace";
        case VK_ESCAPE: return t + L"Esc";
        case VK_DELETE: return t + L"Delete";
        case VK_INSERT: return t + L"Insert";
        case VK_HOME: return t + L"Home";
        case VK_END: return t + L"End";
        case VK_PRIOR: return t + L"Page Up";
        case VK_NEXT: return t + L"Page Down";
        case VK_UP: return t + L"Up";
        case VK_DOWN: return t + L"Down";
        case VK_LEFT: return t + L"Left";
        case VK_RIGHT: return t + L"Right";
        case VK_MEDIA_PLAY_PAUSE: return t + L"Play/Pause";
        case VK_MEDIA_NEXT_TRACK: return t + L"Next Track";
        case VK_MEDIA_PREV_TRACK: return t + L"Prev Track";
        case VK_MEDIA_STOP: return t + L"Stop Media";
        case VK_VOLUME_UP: return t + L"Volume Up";
        case VK_VOLUME_DOWN: return t + L"Volume Down";
        case VK_VOLUME_MUTE: return t + L"Mute";
        case VK_BROWSER_BACK: return t + L"Browser Back";
        case VK_BROWSER_FORWARD: return t + L"Browser Forward";
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return t + L"F" + std::to_wstring(vk - VK_F1 + 1);
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE) {
        scan |= KF_EXTENDED;
    }
    std::array<wchar_t, 64> buf{};
    const uint32_t lparam_bits = static_cast<uint32_t>(scan) << 16u;
    if (GetKeyNameTextW(static_cast<LONG>(lparam_bits), buf.data(), static_cast<int>(buf.size())) > 0) {
        return t + buf.data();
    }
    return t + L"VK_" + std::to_wstring(vk);
}

inline void sanitize_settings(Settings& s) {
    if (!protocol::is_valid_poll_rate(s.polling_rate)) {
        s.polling_rate = 1000;
    }
    s.dpi_x = protocol::clamp_dpi(s.dpi_x);
    s.dpi_y = s.link_xy ? s.dpi_x : protocol::clamp_dpi(s.dpi_y);

    std::vector<uint16_t> clean_stages;
    clean_stages.reserve(s.dpi_stages.size());
    for (const uint16_t stage : s.dpi_stages) {
        const uint16_t clamped = protocol::clamp_dpi(stage);
        if (std::ranges::find(clean_stages, clamped) == clean_stages.end()) {
            clean_stages.push_back(clamped);
        }
    }
    if (clean_stages.empty()) {
        clean_stages = {400, 800, 1600, 3200};
    }
    std::ranges::sort(clean_stages);
    s.dpi_stages = std::move(clean_stages);

    for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); ++i) {
        auto& b = get_binding(s.buttons, static_cast<ButtonId>(i));
        b.kind = std::string{action_kind_name(parse_action_kind(b.kind))};
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
        const std::wstring cmd = L"\"" + exe_path().wstring() + L"\" --tray";
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
    const fs::path path = settings_path();
    std::error_code ec;
    const bool existed = fs::exists(path, ec);
    if (auto bytes = usylibpp::files::read_as_bytes(path)) {
        const auto err = glz::read<JSON_READ>(s, *bytes);
        if (err && parse_ok != nullptr) *parse_ok = false;
    }
    sanitize_settings(s);
    s.launch_on_startup = is_startup_enabled();
    if (!existed) {
        if (auto out = glz::write<JSON_WRITE>(s)) {
            (void)usylibpp::files::write(path, std::string_view{*out});
        }
    }
    return s;
}

inline bool save_settings(const Settings& s) {
    const fs::path path = settings_path();
    const auto out = glz::write<JSON_WRITE>(s);
    if (!out) return false;

    const fs::path tmp = path.wstring() + L".tmp." +
                         std::to_wstring(GetCurrentProcessId()) + L"." +
                         std::to_wstring(GetCurrentThreadId());
    if (!usylibpp::files::write(tmp, std::string_view{*out})) return false;
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    const DWORD err = GetLastError();
    std::error_code ec;
    fs::remove(tmp, ec);
    SetLastError(err);
    return false;
}

} // namespace da_elite
