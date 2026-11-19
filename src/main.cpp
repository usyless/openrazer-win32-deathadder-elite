#include <da_elite/common.hpp>
#include <da_elite/config_window.hpp>
#include <da_elite/hid_device.hpp>
#include <da_elite/razer_protocol.hpp>
#include <da_elite/remapper.hpp>
#include <da_elite/settings.hpp>

#include "resource.h"

#include <commctrl.h>
#include <dbt.h>
#include <shellapi.h>
#include <wil/resource.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace da_elite;

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT TRAY_ID = 1;
constexpr UINT_PTR TIMER_REAPPLY = 1;

// GUID_DEVINTERFACE_HID = {4D1E55B2-F16F-11CF-88CB-001111000030}
constexpr GUID HID_INTERFACE_GUID = {
    .Data1 = 0x4D1E55B2,
    .Data2 = 0xF16F,
    .Data3 = 0x11CF,
    .Data4 = {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30},
};

enum MenuId : uint8_t {
    IdConfigure = 100,
    IdReapply = 101,
    IdPoll1000 = 102,
    IdPoll500 = 103,
    IdPoll125 = 104,
    IdStartup = 105,
    IdQuit = 106,
    IdStageBase = 200,
};

class ServiceApp {
public:
    ServiceApp() = default;
    ~ServiceApp() {
        try {
            shutdown();
        } catch (...) {
            OutputDebugStringA("da-elite: exception during shutdown\n");
        }
    }
    ServiceApp(const ServiceApp&) = delete;
    ServiceApp& operator=(const ServiceApp&) = delete;
    ServiceApp(ServiceApp&&) = delete;
    ServiceApp& operator=(ServiceApp&&) = delete;

    bool init(HINSTANCE inst, bool show_ui) {
        inst_ = inst;
        json_scratch_.reserve(2048);
        settings_ = load_settings();
        taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

        WNDCLASSEXW wc{.cbSize = sizeof(wc)};
        wc.lpfnWndProc = &ServiceApp::wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = SERVICE_CLASS;
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, SERVICE_CLASS, APP_NAME, WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, inst, this);
        if (hwnd_ == nullptr) return false;

        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = HID_INTERFACE_GUID;
        dev_notify_ = RegisterDeviceNotificationW(hwnd_, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

        add_tray();
        apply_all_to_device(false);

        if (show_ui) {
            open_config();
        }
        return true;
    }

    int run() {
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
        MSG msg{};
        BOOL ret = 0;
        while ((ret = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (config_.is_open()) {
                if (config_.handle_message_if_recording(msg)) {
                    continue;
                }
                if (IsDialogMessageW(config_.hwnd(), &msg)) {
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return ret == -1 ? 1 : static_cast<int>(msg.wParam);
    }

    void shutdown() {
        if (shut_down_) return;
        shut_down_ = true;
        if (hwnd_ != nullptr && IsWindow(hwnd_)) {
            KillTimer(hwnd_, TIMER_REAPPLY);
        }
        remapper_.stop();
        if (driver_mode_applied_) {
            (void)mouse_.set_device_mode(protocol::DeviceMode::Normal);
            driver_mode_applied_ = false;
        }
        if (dev_notify_ != nullptr) {
            UnregisterDeviceNotification(dev_notify_);
            dev_notify_ = nullptr;
        }
        remove_tray();
        config_.close();
        mouse_.disconnect();
        if (!in_destroy_ && hwnd_ != nullptr && IsWindow(hwnd_)) {
            DestroyWindow(hwnd_);
        }
    }

private:
    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* self = reinterpret_cast<ServiceApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != nullptr) return self->handle(hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == taskbar_created_ && taskbar_created_ != 0) {
            add_tray();
            return 0;
        }
        switch (msg) {
            case WM_TRAY:
                switch (LOWORD(lp)) {
                    case NIN_SELECT:
                    case NIN_KEYSELECT:
                    case WM_LBUTTONDBLCLK:
                        open_config();
                        break;
                    case WM_CONTEXTMENU:
                        show_menu(POINT{.x = GET_X_LPARAM(wp), .y = GET_Y_LPARAM(wp)});
                        break;
                    default:
                        break;
                }
                return 0;

            case WM_COPYDATA: {
                const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
                if (cds == nullptr) return FALSE;
                switch (static_cast<AppCmd>(cds->dwData)) {
                    case AppCmd::ShowConfig:
                        open_config();
                        return TRUE;
                    case AppCmd::ApplySettings:
                        settings_ = load_settings();
                        apply_all_to_device(true);
                        return TRUE;
                    case AppCmd::Quit:
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return TRUE;
                }
                return FALSE;
            }

            case ButtonRemapper::WM_REMAP_EVENT: {
                const auto btn = static_cast<ButtonId>(wp);
                const bool is_down = (lp != 0);
                const int dpi_delta = remapper_.dispatch_event(btn, is_down);
                if (dpi_delta != 0) {
                    step_dpi_stage(dpi_delta);
                }
                return 0;
            }

            case WM_INPUT:
                remapper_.handle_raw_input(reinterpret_cast<HRAWINPUT>(lp));
                return 0;

            case WM_DEVICECHANGE:
                on_device_change(wp, lp);
                return TRUE;

            case WM_POWERBROADCAST:
                if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                    SetTimer(hwnd_, TIMER_REAPPLY, 600, nullptr);
                }
                return TRUE;

            case WM_ENDSESSION:
                if (wp != FALSE) {
                    shutdown();
                }
                return 0;

            case WM_TIMER:
                if (wp == TIMER_REAPPLY) {
                    KillTimer(hwnd_, TIMER_REAPPLY);
                    mouse_.disconnect();
                    apply_all_to_device(true);
                }
                return 0;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                in_destroy_ = true;
                shutdown();
                PostQuitMessage(0);
                return 0;

            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void on_device_change(WPARAM wp, LPARAM lp) {
        if (wp != DBT_DEVICEARRIVAL && wp != DBT_DEVICEREMOVECOMPLETE) return;
        const auto* hdr = reinterpret_cast<const DEV_BROADCAST_HDR*>(lp);
        if (hdr == nullptr || hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE ||
            hdr->dbch_size <= offsetof(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name)) {
            return;
        }
        const auto* dev = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(lp);
        const std::wstring_view dev_name{dev->dbcc_name};
        if (!contains_ascii_icase(dev_name, L"vid_1532") || !contains_ascii_icase(dev_name, L"pid_005c")) {
            return;
        }
        if (wp == DBT_DEVICEREMOVECOMPLETE) {
            remapper_.on_device_removed();
            mouse_.disconnect();
            driver_mode_applied_ = false;
            update_tip();
            config_.update_status_from_service(settings_);
        } else if (wp == DBT_DEVICEARRIVAL) {
            SetTimer(hwnd_, TIMER_REAPPLY, 250, nullptr);
        }
    }

    void apply_all_to_device(bool force_reconnect, bool refresh_config_status = true) {
        if (force_reconnect) mouse_.disconnect();

        const bool connected = mouse_.connect();
        if (connected) {
            const bool poll_ok = mouse_.set_polling_rate(settings_.polling_rate);
            const bool dpi_ok = mouse_.set_dpi(settings_.dpi_x, settings_.dpi_y);
            const bool want_driver = any_driver_mode_needed(settings_);
            if (want_driver) {
                driver_mode_applied_ = mouse_.set_device_mode(protocol::DeviceMode::Driver);
            } else if (driver_mode_applied_) {
                (void)mouse_.set_device_mode(protocol::DeviceMode::Normal);
                driver_mode_applied_ = false;
            }
            if (!poll_ok || !dpi_ok) {
                last_device_status_.assign(L"Device write warning: ");
                append_utf8_as_wide(last_device_status_, mouse_.last_error());
            } else {
                last_device_status_.clear();
            }
        } else {
            driver_mode_applied_ = false;
            last_device_status_.clear();
            append_utf8_as_wide(last_device_status_, mouse_.last_error());
        }

        HidCollectionList cols; // NOLINT(cppcoreguidelines-pro-type-member-init)
        mouse_.copy_collections(cols);
        remapper_.configure(hwnd_, settings_, cols.span(), force_reconnect);
        update_tip();
        config_.update_status_from_service(settings_, refresh_config_status);
    }

    void step_dpi_stage(int delta) {
        if (settings_.dpi_stages.empty()) return;
        const auto& stages = settings_.dpi_stages;
        size_t best_idx = 0;
        int best_dist = std::abs(static_cast<int>(stages[0]) - static_cast<int>(settings_.dpi_x));
        for (size_t i = 1; i < stages.size(); ++i) {
            const int d = std::abs(static_cast<int>(stages[i]) - static_cast<int>(settings_.dpi_x));
            if (d < best_dist) {
                best_dist = d;
                best_idx = i;
            }
        }
        const int next = std::clamp(static_cast<int>(best_idx) + delta, 0, static_cast<int>(stages.size()) - 1);
        const uint16_t target_dpi = stages[static_cast<size_t>(next)];
        set_quick_dpi(target_dpi);
    }

    void set_quick_dpi(uint16_t dpi) {
        settings_.dpi_x = protocol::clamp_dpi(dpi);
        settings_.dpi_y = settings_.link_xy ? settings_.dpi_x : settings_.dpi_y;
        (void)save_settings(settings_, json_scratch_);
        if (mouse_.is_connected() || mouse_.connect()) {
            (void)mouse_.set_dpi(settings_.dpi_x, settings_.dpi_y);
        }
        update_tip();
        config_.update_status_from_service(settings_);
    }

    void set_quick_poll(uint16_t hz) {
        if (!protocol::is_valid_poll_rate(hz)) return;
        settings_.polling_rate = hz;
        (void)save_settings(settings_, json_scratch_);
        if (mouse_.is_connected() || mouse_.connect()) {
            (void)mouse_.set_polling_rate(hz);
        }
        update_tip();
        config_.update_status_from_service(settings_);
    }

    void build_status_text(std::wstring& out) {
        out.clear();
        if (!mouse_.is_connected() && !mouse_.connect()) {
            out.assign(L"Disconnected — ");
            append_utf8_as_wide(out, mouse_.last_error());
            return;
        }
        const auto fw = mouse_.get_firmware_version();
        const auto poll = mouse_.get_polling_rate();
        const auto dpi = mouse_.get_dpi();
        if (out.capacity() < 96) out.reserve(96);
        out = L"Connected: DeathAdder Elite";
        if (fw.has_value()) {
            FixedWString<24> fw_w;
            for (const char c : fw->view()) fw_w.push_back(static_cast<wchar_t>(c));
            append_to(out, L" (", fw_w, L")");
        }
        if (dpi.has_value()) append_to(out, L" • ", to_fixed_wstr(dpi->first), L" DPI");
        if (poll.has_value()) append_to(out, L" • ", to_fixed_wstr(*poll), L" Hz");
        if (!last_device_status_.empty()) append_to(out, L" [", last_device_status_, L"]");
    }

    void open_config() {
        config_.show(
            inst_,
            settings_,
            [this](const Settings& next, std::wstring& status_out) {
                settings_ = next;
                if (!set_startup_enabled(settings_.launch_on_startup)) {
                    const DWORD err = GetLastError();
                    settings_.launch_on_startup = is_startup_enabled();
                    const std::wstring msg = ulp::str::concat_strings(
                        L"Failed to update Windows startup setting: ", to_wide(win32_error_utf8(err)));
                    MessageBoxW(config_.hwnd(), msg.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
                }
                (void)save_settings(settings_, json_scratch_);
                apply_all_to_device(false, false);
                build_status_text(status_out);
                return true;
            },
            [this](std::wstring& out) { build_status_text(out); },
            [this](bool enable) {
                if (!set_startup_enabled(enable)) {
                    const DWORD err = GetLastError();
                    const std::wstring msg = ulp::str::concat_strings(
                        L"Failed to update Windows startup setting: ", to_wide(win32_error_utf8(err)));
                    MessageBoxW(config_.hwnd(), msg.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
                } else {
                    settings_.launch_on_startup = enable;
                    (void)save_settings(settings_, json_scratch_);
                }
                settings_.launch_on_startup = is_startup_enabled();
                return settings_.launch_on_startup;
            });
    }

    [[nodiscard]] NOTIFYICONDATAW tray_data() const noexcept {
        NOTIFYICONDATAW nid{.cbSize = sizeof(nid)};
        nid.hWnd = hwnd_;
        nid.uID = TRAY_ID;
        return nid;
    }

    void add_tray() {
        last_tip_.clear();
        NOTIFYICONDATAW nid = tray_data();
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAY;
        tray_icon_.reset(static_cast<HICON>(LoadImageW(
            inst_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR)));
        nid.hIcon = tray_icon_.get();
        wcscpy_s(nid.szTip, APP_NAME);
        Shell_NotifyIconW(NIM_ADD, &nid);
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        update_tip();
    }

    void remove_tray() noexcept {
        NOTIFYICONDATAW nid = tray_data();
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void update_tip() {
        FixedWString<128> tip;
        if (mouse_.is_connected()) {
            tip.assign(L"DeathAdder Elite — ");
            tip.append(to_fixed_wstr(settings_.dpi_x));
            tip.append(L" DPI • ");
            tip.append(to_fixed_wstr(settings_.polling_rate));
            tip.append(L" Hz");
        } else {
            tip.assign(L"DeathAdder Elite — Disconnected");
        }
        if (tip.view() == last_tip_.view()) return;
        last_tip_ = tip;

        NOTIFYICONDATAW nid = tray_data();
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcscpy_s(nid.szTip, tip.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void show_menu(POINT pt) {
        const wil::unique_hmenu menu{CreatePopupMenu()};
        FixedWString<128> header;
        if (mouse_.is_connected()) {
            header.assign(L"DeathAdder Elite • ");
            header.append(to_fixed_wstr(settings_.dpi_x));
            header.append(L" DPI • ");
            header.append(to_fixed_wstr(settings_.polling_rate));
            header.append(L" Hz");
        } else {
            header.assign(L"DeathAdder Elite (Disconnected)");
        }
        AppendMenuW(menu.get(), MF_STRING | MF_DISABLED, 0, header.c_str());
        AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);

        wil::unique_hmenu poll_menu{CreatePopupMenu()};
        const auto add_poll = [&](UINT id, uint16_t hz, const wchar_t* label) {
            const UINT flags = MF_STRING | (settings_.polling_rate == hz ? MF_CHECKED : 0);
            AppendMenuW(poll_menu.get(), flags, id, label);
        };
        add_poll(IdPoll1000, 1000, L"1000 Hz (1 ms)");
        add_poll(IdPoll500, 500, L"500 Hz (2 ms)");
        add_poll(IdPoll125, 125, L"125 Hz (8 ms)");
        // Submenus attached via MF_POPUP are owned and destroyed by `menu`.
        if (AppendMenuW(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(poll_menu.get()), L"Polling Rate")) {
            (void)poll_menu.release();
        }

        wil::unique_hmenu dpi_menu{CreatePopupMenu()};
        for (size_t i = 0; i < settings_.dpi_stages.size(); ++i) {
            const uint16_t stage = settings_.dpi_stages[i];
            const UINT flags = MF_STRING | (settings_.dpi_x == stage ? MF_CHECKED : 0);
            FixedWString<32> label = to_fixed_wstr<32>(stage);
            label.append(L" DPI");
            AppendMenuW(dpi_menu.get(), flags, IdStageBase + static_cast<UINT>(i), label.c_str());
        }
        if (AppendMenuW(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(dpi_menu.get()), L"DPI Preset")) {
            (void)dpi_menu.release();
        }

        AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu.get(), MF_STRING, IdConfigure, L"Configure...");
        AppendMenuW(menu.get(), MF_STRING, IdReapply, L"Re-apply Settings");
        AppendMenuW(menu.get(), MF_STRING | (settings_.launch_on_startup ? MF_CHECKED : 0),
                    IdStartup, L"Launch on Windows Startup");
        AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu.get(), MF_STRING, IdQuit, L"Quit");
        SetMenuDefaultItem(menu.get(), IdConfigure, FALSE);

        if (pt.x == 0 && pt.y == 0) {
            GetCursorPos(&pt);
        }
        SetForegroundWindow(hwnd_);
        const UINT cmd = static_cast<UINT>(
            TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             pt.x, pt.y, hwnd_, nullptr));
        PostMessageW(hwnd_, WM_NULL, 0, 0);

        switch (cmd) {
            case IdConfigure: open_config(); break;
            case IdReapply: apply_all_to_device(true); break;
            case IdPoll1000: set_quick_poll(1000); break;
            case IdPoll500: set_quick_poll(500); break;
            case IdPoll125: set_quick_poll(125); break;
            case IdStartup: {
                const bool want = !settings_.launch_on_startup;
                if (!set_startup_enabled(want)) {
                    const DWORD err = GetLastError();
                    const std::wstring msg = ulp::str::concat_strings(
                        L"Failed to update Windows startup setting: ", to_wide(win32_error_utf8(err)));
                    MessageBoxW(hwnd_, msg.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
                } else {
                    settings_.launch_on_startup = want;
                    (void)save_settings(settings_, json_scratch_);
                }
                config_.update_status_from_service(settings_);
                break;
            }
            case IdQuit:
                DestroyWindow(hwnd_);
                break;
            default:
                if (cmd >= IdStageBase && cmd < IdStageBase + settings_.dpi_stages.size()) {
                    set_quick_dpi(settings_.dpi_stages[cmd - IdStageBase]);
                }
                break;
        }
    }

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbar_created_ = 0;
    HDEVNOTIFY dev_notify_ = nullptr;
    bool shut_down_ = false;
    bool in_destroy_ = false;
    bool driver_mode_applied_ = false;
    Settings settings_{};
    DeathAdderEliteDevice mouse_;
    ButtonRemapper remapper_;
    ConfigWindow config_;
    wil::unique_hicon tray_icon_;
    FixedWString<128> last_tip_;
    std::wstring last_device_status_;
    std::string json_scratch_;
};

void attach_parent_console_if_present() noexcept {
    const HANDLE out_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out_h != nullptr && out_h != INVALID_HANDLE_VALUE && GetFileType(out_h) != FILE_TYPE_UNKNOWN) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f_out = nullptr;
        FILE* f_err = nullptr;
        freopen_s(&f_out, "CONOUT$", "w", stdout);
        freopen_s(&f_err, "CONOUT$", "w", stderr);
    }
}

void print_help() {
    std::println(
        "da-elite.exe — Lightweight Razer DeathAdder Elite controller\n"
        "Usage:\n"
        "  da-elite.exe                 Start tray service and open settings window\n"
        "  da-elite.exe --tray          Start silently in system tray\n"
        "  da-elite.exe --show          Open settings window on running instance\n"
        "  da-elite.exe --quit          Quit running tray instance\n"
        "  da-elite.exe --status        Query and print current mouse status, then exit\n"
        "  da-elite.exe --apply         Apply saved data\\settings.json once, then exit\n"
        "  da-elite.exe --poll <hz>     Set polling rate (125 | 500 | 1000), save & exit\n"
        "  da-elite.exe --dpi <x> [y]   Set DPI (100..16000), save & exit\n"
        "  da-elite.exe --startup <on|off> Enable or disable launch on Windows startup");
}

int run_cli_one_shot(const std::vector<std::wstring_view>& args) {
    attach_parent_console_if_present();

    std::optional<uint16_t> req_poll;
    std::optional<std::pair<uint16_t, uint16_t>> req_dpi;
    std::optional<bool> req_startup;
    bool do_status = false;
    bool do_apply = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring_view a = args[i];
        if (a == L"--help" || a == L"-h" || a == L"/?") {
            print_help();
            return 0;
        }
        if (a == L"--status") {
            do_status = true;
        } else if (a == L"--apply") {
            do_apply = true;
        } else if (a == L"--startup" && i + 1 < args.size()) {
            const std::wstring_view v = args[++i];
            if (v == L"on" || v == L"1" || v == L"true") {
                req_startup = true;
            } else if (v == L"off" || v == L"0" || v == L"false") {
                req_startup = false;
            } else {
                std::println(stderr, "Invalid --startup value. Use 'on' or 'off'.");
                return 2;
            }
        } else if (a == L"--poll" && i + 1 < args.size()) {
            const long v = parse_long(args[++i]).value_or(0);
            if (v <= 0 || v > 65535 || !protocol::is_valid_poll_rate(static_cast<uint16_t>(v))) {
                std::println(stderr, "Invalid polling rate. Allowed: 125, 500, 1000");
                return 2;
            }
            req_poll = static_cast<uint16_t>(v);
        } else if (a == L"--dpi" && i + 1 < args.size()) {
            const std::optional<long> x = parse_long(args[++i]);
            if (!x.has_value() || *x <= 0) {
                std::println(stderr, "Invalid DPI value. Expected integer in 100..16000.");
                return 2;
            }
            long y = *x;
            if (i + 1 < args.size() && !args[i + 1].starts_with(L'-')) {
                const std::optional<long> py = parse_long(args[++i]);
                if (!py.has_value() || *py <= 0) {
                    std::println(stderr, "Invalid DPI Y value. Expected integer in 100..16000.");
                    return 2;
                }
                y = *py;
            }
            req_dpi = std::pair{
                protocol::clamp_dpi(static_cast<uint16_t>(std::clamp<long>(*x, 100, 16000))),
                protocol::clamp_dpi(static_cast<uint16_t>(std::clamp<long>(y, 100, 16000))),
            };
        } else {
            std::println(stderr, "Unknown or incomplete argument.");
            print_help();
            return 2;
        }
    }

    if (req_startup.has_value()) {
        if (!set_startup_enabled(*req_startup)) {
            std::println(stderr, "Failed to update startup setting: {}", win32_error_utf8(GetLastError()));
            return 1;
        }
        Settings s = load_settings();
        s.launch_on_startup = is_startup_enabled();
        (void)save_settings(s);
        if (HWND svc = find_service_window()) {
            send_app_cmd(svc, AppCmd::ApplySettings);
        }
        std::println("Windows startup: {}", s.launch_on_startup ? "enabled" : "disabled");
        if (!do_status && !do_apply && !req_poll.has_value() && !req_dpi.has_value()) return 0;
    }

    DeathAdderEliteDevice mouse;
    if (!mouse.connect()) {
        std::println(stderr, "Error: {}", mouse.last_error());
        return 1;
    }

    if (do_apply || req_poll.has_value() || req_dpi.has_value()) {
        Settings s = load_settings();
        if (req_poll.has_value()) s.polling_rate = *req_poll;
        if (req_dpi.has_value()) {
            s.dpi_x = req_dpi->first;
            s.dpi_y = req_dpi->second;
            s.link_xy = (s.dpi_x == s.dpi_y);
        }
        sanitize_settings(s);
        if (req_poll.has_value() || req_dpi.has_value()) {
            (void)save_settings(s);
        }
        if (!mouse.set_polling_rate(s.polling_rate)) {
            std::println(stderr, "Failed to set polling rate: {}", mouse.last_error());
            return 1;
        }
        if (!mouse.set_dpi(s.dpi_x, s.dpi_y)) {
            std::println(stderr, "Failed to set DPI: {}", mouse.last_error());
            return 1;
        }
        if (HWND svc = find_service_window()) {
            send_app_cmd(svc, AppCmd::ApplySettings);
        }
        std::println("Applied: {} DPI ({}x{}), {} Hz", s.dpi_x, s.dpi_x, s.dpi_y, s.polling_rate);
        if (!do_status) return 0;
    }

    if (do_status) {
        const auto fw = mouse.get_firmware_version();
        const auto sn = mouse.get_serial();
        const auto poll = mouse.get_polling_rate();
        const auto dpi = mouse.get_dpi();
        const auto mode = mouse.get_device_mode();
        std::println("Device:   Razer DeathAdder Elite (1532:005C)");
        std::println("Firmware: {}", fw ? fw->view() : std::string_view{"unknown"});
        std::println("Serial:   {}", sn ? sn->view() : std::string_view{"unknown"});
        if (poll) {
            std::println("Polling:  {} Hz", *poll);
        } else {
            std::println("Polling:  unknown");
        }
        if (dpi) {
            std::println("DPI:      {} x {}", dpi->first, dpi->second);
        } else {
            std::println("DPI:      unknown");
        }
        std::println("Mode:     {}",
                     mode ? (*mode == protocol::DeviceMode::Driver ? "Driver (0x03)" : "Normal (0x00)") : "unknown");
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const INITCOMMONCONTROLSEX icc{.dwSize = sizeof(icc), .dwICC = ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring_view> args;
    if (argv != nullptr) {
        if (argc > 1) args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
    }
    const auto free_argv = wil::scope_exit([argv] {
        if (argv != nullptr) LocalFree(static_cast<HLOCAL>(argv));
    });

    bool tray_only = false;
    bool show_flag = false;
    bool quit_flag = false;
    bool has_cli_one_shot = false;

    for (const std::wstring_view a : args) {
        if (a == L"--tray" || a == L"--minimized") { tray_only = true; }
        else if (a == L"--show") { show_flag = true; }
        else if (a == L"--quit") { quit_flag = true; }
        else { has_cli_one_shot = true; }
    }

    if (quit_flag) {
        if (HWND svc = find_service_window()) {
            send_app_cmd(svc, AppCmd::Quit);
        }
        return 0;
    }

    if (has_cli_one_shot) {
        return run_cli_one_shot(args);
    }

    const wil::unique_mutex_nothrow single{CreateMutexW(nullptr, TRUE, INSTANCE_MUTEX)};
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = find_service_window(); existing != nullptr && (!tray_only || show_flag)) {
            send_app_cmd(existing, AppCmd::ShowConfig);
        }
        return 0;
    }

    usylibpp::windows::darkmode::init_dark_mode();

    ServiceApp app;
    if (!app.init(hInstance, !tray_only || show_flag)) {
        MessageBoxW(nullptr, L"Could not initialize DeathAdder Elite tray service.", APP_NAME, MB_ICONERROR | MB_OK);
        return 1;
    }

    const int code = app.run();
    app.shutdown();
    return code;
}
