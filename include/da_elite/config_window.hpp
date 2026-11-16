#pragma once

#include "common.hpp"
#include "hid_device.hpp"
#include "settings.hpp"

#include "resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wil/resource.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace da_elite {

namespace detail {

[[nodiscard]] inline bool is_apps_dark_theme() noexcept {
    DWORD val = 1;
    DWORD sz = sizeof(val);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &val, &sz) == ERROR_SUCCESS) {
        return val == 0;
    }
    return false;
}

[[nodiscard]] inline std::wstring format_stages_text(const std::vector<uint16_t>& stages) {
    std::wstring out;
    for (size_t i = 0; i < stages.size(); ++i) {
        if (i > 0) out += L", ";
        out += std::to_wstring(stages[i]);
    }
    return out;
}

[[nodiscard]] inline std::vector<uint16_t> parse_stages_text(std::wstring_view text) {
    std::vector<uint16_t> out;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == L' ' || text[pos] == L',' || text[pos] == L';')) {
            ++pos;
        }
        if (pos >= text.size()) break;
        size_t end = pos;
        while (end < text.size() && text[end] >= L'0' && text[end] <= L'9') {
            ++end;
        }
        if (end > pos) {
            const long v = std::wcstol(std::wstring{text.substr(pos, end - pos)}.c_str(), nullptr, 10);
            if (v > 0) {
                out.push_back(protocol::clamp_dpi(static_cast<uint16_t>(std::min<long>(v, 65535))));
            }
            pos = end;
        } else {
            ++pos;
        }
    }
    return out;
}

[[nodiscard]] inline uint16_t read_uint16_edit(HWND edit, uint16_t fallback) {
    std::array<wchar_t, 32> buf{};
    GetWindowTextW(edit, buf.data(), static_cast<int>(buf.size()));
    wchar_t* end = nullptr;
    const long v = std::wcstol(buf.data(), &end, 10);
    if (end == buf.data() || v <= 0) return fallback;
    return static_cast<uint16_t>(std::min<long>(v, 65535));
}

inline constexpr std::array<ActionKind, 10> ACTION_ORDER = {
    ActionKind::Default,
    ActionKind::Disabled,
    ActionKind::DpiCycleUp,
    ActionKind::DpiCycleDown,
    ActionKind::MouseLeft,
    ActionKind::MouseRight,
    ActionKind::MouseMiddle,
    ActionKind::MouseX1,
    ActionKind::MouseX2,
    ActionKind::KeyCombo,
};

[[nodiscard]] inline const wchar_t* action_label(ActionKind k) noexcept {
    switch (k) {
        case ActionKind::Default: return L"Default";
        case ActionKind::Disabled: return L"Disabled";
        case ActionKind::DpiCycleUp: return L"DPI Cycle Up";
        case ActionKind::DpiCycleDown: return L"DPI Cycle Down";
        case ActionKind::MouseLeft: return L"Mouse Left";
        case ActionKind::MouseRight: return L"Mouse Right";
        case ActionKind::MouseMiddle: return L"Mouse Middle";
        case ActionKind::MouseX1: return L"Mouse Back (X1)";
        case ActionKind::MouseX2: return L"Mouse Forward (X2)";
        case ActionKind::KeyCombo: return L"Key / Shortcut";
    }
    return L"Default";
}

[[nodiscard]] inline int action_to_combo_index(ActionKind k) noexcept {
    for (size_t i = 0; i < ACTION_ORDER.size(); ++i) {
        if (ACTION_ORDER[i] == k) return static_cast<int>(i);
    }
    return 0;
}

} // namespace detail

/// Pure Win32 native-controls configuration window (created on demand, destroyed on close).
class ConfigWindow {
public:
    using ApplyCallback = std::function<bool(const Settings&, std::wstring& status_out)>;
    using StatusCallback = std::function<std::wstring()>;
    using StartupCallback = std::function<bool(bool)>;

    ConfigWindow() = default;
    ~ConfigWindow() { close(); }
    ConfigWindow(const ConfigWindow&) = delete;
    ConfigWindow& operator=(const ConfigWindow&) = delete;
    ConfigWindow(ConfigWindow&&) = delete;
    ConfigWindow& operator=(ConfigWindow&&) = delete;

    void show(HINSTANCE inst, const Settings& current, ApplyCallback on_apply,
              StatusCallback on_status, StartupCallback on_startup = {}) {
        inst_ = inst;
        on_apply_ = std::move(on_apply);
        on_status_ = std::move(on_status);
        on_startup_ = std::move(on_startup);
        draft_ = current;

        if (hwnd_ != nullptr && IsWindow(hwnd_)) {
            sync_controls_from_draft();
            refresh_status();
            ShowWindow(hwnd_, SW_SHOWNORMAL);
            SetForegroundWindow(hwnd_);
            return;
        }

        register_class(inst);
        dark_ = detail::is_apps_dark_theme();

        const UINT sys_dpi = GetDpiForSystem();
        const int w = MulDiv(500, static_cast<int>(sys_dpi), 96);
        const int h = MulDiv(470, static_cast<int>(sys_dpi), 96);

        RECT rc{.left = 0, .top = 0, .right = w, .bottom = h};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectExForDpi(&rc, style, FALSE, 0, sys_dpi);

        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;
        const int sx = (GetSystemMetrics(SM_CXSCREEN) - cw) / 2;
        const int sy = (GetSystemMetrics(SM_CYSCREEN) - ch) / 2;

        hwnd_ = CreateWindowExW(
            0, CONFIG_CLASS, L"Razer DeathAdder Elite — Settings",
            style, sx, sy, cw, ch, nullptr, nullptr, inst, this);
        if (hwnd_ == nullptr) return;

        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
        SetForegroundWindow(hwnd_);
    }

    void close() noexcept {
        if (hwnd_ != nullptr && IsWindow(hwnd_)) {
            DestroyWindow(hwnd_);
        }
        hwnd_ = nullptr;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return hwnd_ != nullptr && IsWindow(hwnd_) != FALSE;
    }

    void update_status_from_service(const Settings& latest) {
        if (!is_open()) return;
        draft_.dpi_x = latest.dpi_x;
        draft_.dpi_y = latest.dpi_y;
        draft_.polling_rate = latest.polling_rate;
        draft_.launch_on_startup = latest.launch_on_startup;
        SetWindowTextW(edit_dpi_x_, std::to_wstring(draft_.dpi_x).c_str());
        SetWindowTextW(edit_dpi_y_, std::to_wstring(draft_.dpi_y).c_str());
        SendMessageW(chk_startup_, BM_SETCHECK, draft_.launch_on_startup ? BST_CHECKED : BST_UNCHECKED, 0);
        select_poll_combo(draft_.polling_rate);
        refresh_status();
    }

    /// Intercepts key presses while recording a button shortcut. Returns true if consumed.
    bool handle_message_if_recording(const MSG& msg) {
        if (!recording_btn_.has_value()) return false;
        if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;

        const auto vk = static_cast<uint32_t>(msg.wParam);
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) {
            return true;
        }

        const auto idx = static_cast<size_t>(*recording_btn_);
        auto& b = get_binding(draft_.buttons, *recording_btn_);
        uint32_t mods = 0;
        const auto is_down = [](int key) noexcept {
            return (static_cast<uint16_t>(GetKeyState(key)) & 0x8000u) != 0;
        };
        if (is_down(VK_CONTROL)) mods |= MOD_CONTROL;
        if (is_down(VK_MENU)) mods |= MOD_ALT;
        if (is_down(VK_SHIFT)) mods |= MOD_SHIFT;
        if (is_down(VK_LWIN) || is_down(VK_RWIN)) mods |= MOD_WIN;

        b.kind = "key";
        b.vk = vk;
        b.mods = mods;
        recording_btn_.reset();
        update_key_button_text(idx);
        return true;
    }

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    enum CtrlId : uint16_t {
        IdRefresh = 1001,
        IdPollCombo = 1002,
        IdDpiX = 1003,
        IdDpiY = 1004,
        IdLinkXy = 1005,
        IdPreset400 = 1006,
        IdPreset800 = 1007,
        IdPreset1600 = 1008,
        IdPreset3200 = 1009,
        IdStages = 1010,
        IdStartup = 1011,
        IdApply = 1012,
        IdClose = 1013,
        IdBtnActionBase = 1100, // +0..4
        IdBtnKeyBase = 1200,    // +0..4
    };

    static void register_class(HINSTANCE inst) {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{.cbSize = sizeof(wc)};
        wc.lpfnWndProc = &ConfigWindow::wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = CONFIG_CLASS;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* self = reinterpret_cast<ConfigWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != nullptr) return self->handle(hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    [[nodiscard]] int sc(int px) const noexcept {
        return MulDiv(px, static_cast<int>(dpi_), 96);
    }

    HWND make_ctrl(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style,
                   int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style,
                                 sc(x), sc(y), sc(w), sc(h), hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
        if (c != nullptr) {
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_.get()), TRUE);
            if (dark_) {
                const bool is_combo = (std::wstring_view{cls} == WC_COMBOBOXW);
                const bool is_group = (std::wstring_view{cls} == WC_BUTTONW) &&
                                      ((style & static_cast<DWORD>(BS_TYPEMASK)) == static_cast<DWORD>(BS_GROUPBOX));
                if (is_group) {
                    SetWindowTheme(c, L"", L"");
                } else {
                    SetWindowTheme(c, is_combo ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
                }
            }
        }
        return c;
    }

    void on_create(HWND hwnd) {
        hwnd_ = hwnd;
        dpi_ = GetDpiForWindow(hwnd);
        if (dpi_ == 0) dpi_ = 96;

        if (dark_) {
            const BOOL dark_val = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_val, sizeof(dark_val));
            usylibpp::windows::darkmode::allow_dark_mode_for_window(hwnd, true);
            bg_brush_.reset(CreateSolidBrush(RGB(32, 32, 32)));
            edit_brush_.reset(CreateSolidBrush(RGB(45, 45, 45)));
        }

        NONCLIENTMETRICSW ncm{.cbSize = sizeof(ncm)};
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_)) {
            font_.reset(CreateFontIndirectW(&ncm.lfMessageFont));
        }

        // Status header
        lbl_status_ = make_ctrl(WC_STATICW, L"Status: Checking...", SS_LEFT | SS_CENTERIMAGE, 0, 14, 12, 386, 24, 0);
        make_ctrl(WC_BUTTONW, L"Refresh", BS_PUSHBUTTON, 0, 408, 12, 78, 24, IdRefresh);

        // Polling Rate & DPI
        make_ctrl(WC_BUTTONW, L"Sensor && Polling", BS_GROUPBOX, 0, 14, 44, 472, 134, 0);

        make_ctrl(WC_STATICW, L"Polling Rate:", SS_LEFT | SS_CENTERIMAGE, 0, 28, 68, 86, 22, 0);
        cmb_poll_ = make_ctrl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 118, 66, 150, 120, IdPollCombo);
        SendMessageW(cmb_poll_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1000 Hz (1 ms)"));
        SendMessageW(cmb_poll_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"500 Hz (2 ms)"));
        SendMessageW(cmb_poll_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"125 Hz (8 ms)"));

        make_ctrl(WC_STATICW, L"DPI X:", SS_LEFT | SS_CENTERIMAGE, 0, 28, 100, 44, 22, 0);
        edit_dpi_x_ = make_ctrl(WC_EDITW, L"800", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 76, 99, 64, 23, IdDpiX);
        make_ctrl(WC_STATICW, L"DPI Y:", SS_LEFT | SS_CENTERIMAGE, 0, 152, 100, 44, 22, 0);
        edit_dpi_y_ = make_ctrl(WC_EDITW, L"800", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 200, 99, 64, 23, IdDpiY);
        chk_link_xy_ = make_ctrl(WC_BUTTONW, L"Link X/Y", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 276, 100, 74, 22, IdLinkXy);

        make_ctrl(WC_BUTTONW, L"400", BS_PUSHBUTTON, 0, 354, 98, 30, 24, IdPreset400);
        make_ctrl(WC_BUTTONW, L"800", BS_PUSHBUTTON, 0, 386, 98, 30, 24, IdPreset800);
        make_ctrl(WC_BUTTONW, L"1600", BS_PUSHBUTTON, 0, 418, 98, 34, 24, IdPreset1600);
        make_ctrl(WC_BUTTONW, L"3200", BS_PUSHBUTTON, 0, 454, 98, 34, 24, IdPreset3200);

        make_ctrl(WC_STATICW, L"DPI Stages:", SS_LEFT | SS_CENTERIMAGE, 0, 28, 134, 86, 22, 0);
        edit_stages_ = make_ctrl(WC_EDITW, L"400, 800, 1600, 3200", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 118, 133, 352, 23, IdStages);

        // Button Assignment
        make_ctrl(WC_BUTTONW, L"Button Assignment", BS_GROUPBOX, 0, 14, 188, 472, 222, 0);
        static constexpr std::array<const wchar_t*, 5> BTN_NAMES = {
            L"Middle Click:",
            L"Side Back (X1):",
            L"Side Forward (X2):",
            L"DPI Up (Top):",
            L"DPI Down (Top):",
        };
        for (size_t i = 0; i < 5; ++i) {
            const int y = 214 + (static_cast<int>(i) * 32);
            make_ctrl(WC_STATICW, BTN_NAMES[i], SS_LEFT | SS_CENTERIMAGE, 0, 28, y, 116, 22, 0);
            HWND cmb = make_ctrl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                                 148, y - 1, 176, 180, IdBtnActionBase + static_cast<int>(i));
            for (const ActionKind k : detail::ACTION_ORDER) {
                SendMessageW(cmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(detail::action_label(k)));
            }
            cmb_actions_[i] = cmb;
            btn_keys_[i] = make_ctrl(WC_BUTTONW, L"Record Key", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                     332, y - 1, 138, 24, IdBtnKeyBase + static_cast<int>(i));
        }

        make_ctrl(WC_STATICW,
                  L"Remapping DPI Up/Down puts the mouse in Driver Mode while this app is running.",
                  SS_LEFT, 0, 28, 378, 444, 24, 0);

        // Footer
        chk_startup_ = make_ctrl(WC_BUTTONW, L"Launch on Windows startup", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 16, 424, 220, 24, IdStartup);
        make_ctrl(WC_BUTTONW, L"Apply && Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 286, 422, 108, 28, IdApply);
        make_ctrl(WC_BUTTONW, L"Hide to Tray", BS_PUSHBUTTON | WS_TABSTOP, 0, 400, 422, 86, 28, IdClose);

        sync_controls_from_draft();
        refresh_status();
    }

    void select_poll_combo(uint16_t hz) const noexcept {
        int idx = 0;
        if (hz == 500) { idx = 1; }
        else if (hz == 125) { idx = 2; }
        SendMessageW(cmb_poll_, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
    }

    void sync_controls_from_draft() {
        recording_btn_.reset();
        select_poll_combo(draft_.polling_rate);
        SetWindowTextW(edit_dpi_x_, std::to_wstring(draft_.dpi_x).c_str());
        SetWindowTextW(edit_dpi_y_, std::to_wstring(draft_.dpi_y).c_str());
        SendMessageW(chk_link_xy_, BM_SETCHECK, draft_.link_xy ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(edit_dpi_y_, draft_.link_xy ? FALSE : TRUE);
        SetWindowTextW(edit_stages_, detail::format_stages_text(draft_.dpi_stages).c_str());
        SendMessageW(chk_startup_, BM_SETCHECK, draft_.launch_on_startup ? BST_CHECKED : BST_UNCHECKED, 0);

        for (size_t i = 0; i < 5; ++i) {
            const auto& b = get_binding(draft_.buttons, static_cast<ButtonId>(i));
            const ActionKind k = parse_action_kind(b.kind);
            SendMessageW(cmb_actions_[i], CB_SETCURSEL, static_cast<WPARAM>(detail::action_to_combo_index(k)), 0);
            update_key_button_text(i);
        }
    }

    void update_key_button_text(size_t i) {
        const auto btn_id = static_cast<ButtonId>(i);
        const auto& b = get_binding(draft_.buttons, btn_id);
        const ActionKind k = parse_action_kind(b.kind);
        const bool is_key = (k == ActionKind::KeyCombo);
        EnableWindow(btn_keys_[i], is_key ? TRUE : FALSE);
        if (recording_btn_.has_value() && *recording_btn_ == btn_id) {
            SetWindowTextW(btn_keys_[i], L"Press any key...");
        } else if (is_key) {
            const std::wstring label = (b.vk != 0) ? format_vk_label(b.mods, b.vk) : L"Click to Record";
            SetWindowTextW(btn_keys_[i], label.c_str());
        } else {
            SetWindowTextW(btn_keys_[i], L"—");
        }
    }

    void read_controls_into_draft() {
        const LRESULT poll_idx = SendMessageW(cmb_poll_, CB_GETCURSEL, 0, 0);
        if (poll_idx == 1) { draft_.polling_rate = 500; }
        else if (poll_idx == 2) { draft_.polling_rate = 125; }
        else { draft_.polling_rate = 1000; }

        draft_.link_xy = (SendMessageW(chk_link_xy_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        draft_.dpi_x = detail::read_uint16_edit(edit_dpi_x_, draft_.dpi_x);
        draft_.dpi_y = draft_.link_xy ? draft_.dpi_x : detail::read_uint16_edit(edit_dpi_y_, draft_.dpi_y);

        std::array<wchar_t, 256> stage_buf{};
        GetWindowTextW(edit_stages_, stage_buf.data(), static_cast<int>(stage_buf.size()));
        draft_.dpi_stages = detail::parse_stages_text(stage_buf.data());
        draft_.launch_on_startup = (SendMessageW(chk_startup_, BM_GETCHECK, 0, 0) == BST_CHECKED);

        for (size_t i = 0; i < 5; ++i) {
            const LRESULT sel = SendMessageW(cmb_actions_[i], CB_GETCURSEL, 0, 0);
            const size_t idx = (sel >= 0 && static_cast<size_t>(sel) < detail::ACTION_ORDER.size())
                                   ? static_cast<size_t>(sel)
                                   : 0;
            auto& b = get_binding(draft_.buttons, static_cast<ButtonId>(i));
            b.kind = std::string{action_kind_name(detail::ACTION_ORDER[idx])};
        }

        sanitize_settings(draft_);
    }

    void refresh_status() {
        if (lbl_status_ == nullptr || !on_status_) return;
        SetWindowTextW(lbl_status_, on_status_().c_str());
    }

    void apply_preset(uint16_t dpi) {
        SetWindowTextW(edit_dpi_x_, std::to_wstring(dpi).c_str());
        if (SendMessageW(chk_link_xy_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            SetWindowTextW(edit_dpi_y_, std::to_wstring(dpi).c_str());
        }
    }

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE:
                on_create(hwnd);
                return 0;

            case WM_ERASEBKGND: {
                auto* hdc = reinterpret_cast<HDC>(wp);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, (dark_ && bg_brush_) ? bg_brush_.get() : GetSysColorBrush(COLOR_BTNFACE));
                return 1;
            }

            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
                if (dark_ && bg_brush_) {
                    auto* hdc = reinterpret_cast<HDC>(wp);
                    SetTextColor(hdc, RGB(240, 240, 240));
                    SetBkColor(hdc, RGB(32, 32, 32));
                    return reinterpret_cast<LRESULT>(bg_brush_.get());
                }
                break;

            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
                if (dark_ && edit_brush_) {
                    auto* hdc = reinterpret_cast<HDC>(wp);
                    SetTextColor(hdc, RGB(240, 240, 240));
                    SetBkColor(hdc, RGB(45, 45, 45));
                    return reinterpret_cast<LRESULT>(edit_brush_.get());
                }
                break;

            case WM_COMMAND: {
                const int id = LOWORD(wp);
                const int code = HIWORD(wp);
                if (id == IdRefresh) {
                    refresh_status();
                    return 0;
                }
                if (id == IdLinkXy && code == BN_CLICKED) {
                    const bool linked = (SendMessageW(chk_link_xy_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    EnableWindow(edit_dpi_y_, linked ? FALSE : TRUE);
                    if (linked) {
                        const uint16_t x = detail::read_uint16_edit(edit_dpi_x_, draft_.dpi_x);
                        SetWindowTextW(edit_dpi_y_, std::to_wstring(x).c_str());
                    }
                    return 0;
                }
                if (id == IdDpiX && code == EN_CHANGE &&
                    SendMessageW(chk_link_xy_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    std::array<wchar_t, 32> buf{};
                    GetWindowTextW(edit_dpi_x_, buf.data(), static_cast<int>(buf.size()));
                    SetWindowTextW(edit_dpi_y_, buf.data());
                    return 0;
                }
                if (id == IdPreset400) { apply_preset(400); return 0; }
                if (id == IdPreset800) { apply_preset(800); return 0; }
                if (id == IdPreset1600) { apply_preset(1600); return 0; }
                if (id == IdPreset3200) { apply_preset(3200); return 0; }

                if (id >= IdBtnActionBase && id < IdBtnActionBase + 5 && code == CBN_SELCHANGE) {
                    const auto idx = static_cast<size_t>(id - IdBtnActionBase);
                    const LRESULT sel = SendMessageW(cmb_actions_[idx], CB_GETCURSEL, 0, 0);
                    const ActionKind k = (sel >= 0 && static_cast<size_t>(sel) < detail::ACTION_ORDER.size())
                                             ? detail::ACTION_ORDER[static_cast<size_t>(sel)]
                                             : ActionKind::Default;
                    get_binding(draft_.buttons, static_cast<ButtonId>(idx)).kind = std::string{action_kind_name(k)};
                    if (recording_btn_.has_value() && *recording_btn_ == static_cast<ButtonId>(idx) && k != ActionKind::KeyCombo) {
                        recording_btn_.reset();
                    }
                    update_key_button_text(idx);
                    return 0;
                }

                if (id >= IdBtnKeyBase && id < IdBtnKeyBase + 5 && code == BN_CLICKED) {
                    const auto idx = static_cast<size_t>(id - IdBtnKeyBase);
                    const auto btn_id = static_cast<ButtonId>(idx);
                    if (recording_btn_.has_value() && *recording_btn_ == btn_id) {
                        recording_btn_.reset();
                    } else {
                        const std::optional<ButtonId> prev = recording_btn_;
                        recording_btn_ = btn_id;
                        if (prev.has_value()) update_key_button_text(static_cast<size_t>(*prev));
                    }
                    update_key_button_text(idx);
                    SetFocus(hwnd);
                    return 0;
                }

                if (id == IdStartup && code == BN_CLICKED) {
                    const bool want = (SendMessageW(chk_startup_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (on_startup_) {
                        const bool actual = on_startup_(want);
                        draft_.launch_on_startup = actual;
                        SendMessageW(chk_startup_, BM_SETCHECK, actual ? BST_CHECKED : BST_UNCHECKED, 0);
                    }
                    return 0;
                }
                if (id == IdApply) {
                    read_controls_into_draft();
                    sync_controls_from_draft();
                    if (on_apply_) {
                        std::wstring status;
                        on_apply_(draft_, status);
                        if (!status.empty()) SetWindowTextW(lbl_status_, status.c_str());
                    }
                    return 0;
                }
                if (id == IdClose) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                recording_btn_.reset();
                font_.reset();
                bg_brush_.reset();
                edit_brush_.reset();
                hwnd_ = nullptr;
                SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    bool dark_ = false;
    Settings draft_{};
    ApplyCallback on_apply_;
    StatusCallback on_status_;
    StartupCallback on_startup_;
    std::optional<ButtonId> recording_btn_;

    wil::unique_hfont font_;
    wil::unique_hbrush bg_brush_;
    wil::unique_hbrush edit_brush_;

    HWND lbl_status_ = nullptr;
    HWND cmb_poll_ = nullptr;
    HWND edit_dpi_x_ = nullptr;
    HWND edit_dpi_y_ = nullptr;
    HWND chk_link_xy_ = nullptr;
    HWND edit_stages_ = nullptr;
    HWND chk_startup_ = nullptr;
    std::array<HWND, 5> cmb_actions_{};
    std::array<HWND, 5> btn_keys_{};
};

} // namespace da_elite
