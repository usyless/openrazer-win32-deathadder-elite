#pragma once

#include "common.hpp"
#include "hid_device.hpp"
#include "settings.hpp"

#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace da_elite {

namespace detail {

[[nodiscard]] inline bool is_extended_vk(uint32_t vk) noexcept {
    switch (vk) {
        case VK_RMENU:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_NUMLOCK:
        case VK_CANCEL:
        case VK_SNAPSHOT:
        case VK_DIVIDE:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
        case VK_MEDIA_NEXT_TRACK:
        case VK_MEDIA_PREV_TRACK:
        case VK_MEDIA_STOP:
        case VK_MEDIA_PLAY_PAUSE:
        case VK_VOLUME_MUTE:
        case VK_VOLUME_DOWN:
        case VK_VOLUME_UP:
        case VK_BROWSER_BACK:
        case VK_BROWSER_FORWARD:
            return true;
        default:
            return false;
    }
}

inline void append_key_input(std::vector<INPUT>& inputs, uint16_t vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    DWORD flags = down ? 0 : KEYEVENTF_KEYUP;
    if (is_extended_vk(vk)) flags |= KEYEVENTF_EXTENDEDKEY;
    in.ki.dwFlags = flags;
    inputs.push_back(in);
}

inline void append_mouse_input(std::vector<INPUT>& inputs, DWORD flags, DWORD mouse_data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.mouseData = mouse_data;
    inputs.push_back(in);
}

inline void send_binding_event(const ButtonBinding& binding, bool is_down) {
    const ActionKind kind = parse_action_kind(binding.kind);
    std::vector<INPUT> inputs;
    inputs.reserve(6);

    switch (kind) {
        case ActionKind::Default:
        case ActionKind::Disabled:
        case ActionKind::DpiCycleUp:
        case ActionKind::DpiCycleDown:
            return;
        case ActionKind::MouseLeft:
            append_mouse_input(inputs, is_down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
            break;
        case ActionKind::MouseRight:
            append_mouse_input(inputs, is_down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
            break;
        case ActionKind::MouseMiddle:
            append_mouse_input(inputs, is_down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
            break;
        case ActionKind::MouseX1:
            append_mouse_input(inputs, is_down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, XBUTTON1);
            break;
        case ActionKind::MouseX2:
            append_mouse_input(inputs, is_down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, XBUTTON2);
            break;
        case ActionKind::KeyCombo: {
            if (binding.vk == 0) return;
            const auto vk = static_cast<uint16_t>(binding.vk);
            if (is_down) {
                if ((binding.mods & MOD_CONTROL) != 0) append_key_input(inputs, VK_CONTROL, true);
                if ((binding.mods & MOD_ALT) != 0) append_key_input(inputs, VK_MENU, true);
                if ((binding.mods & MOD_SHIFT) != 0) append_key_input(inputs, VK_SHIFT, true);
                if ((binding.mods & MOD_WIN) != 0) append_key_input(inputs, VK_LWIN, true);
                append_key_input(inputs, vk, true);
            } else {
                append_key_input(inputs, vk, false);
                if ((binding.mods & MOD_WIN) != 0) append_key_input(inputs, VK_LWIN, false);
                if ((binding.mods & MOD_SHIFT) != 0) append_key_input(inputs, VK_SHIFT, false);
                if ((binding.mods & MOD_ALT) != 0) append_key_input(inputs, VK_MENU, false);
                if ((binding.mods & MOD_CONTROL) != 0) append_key_input(inputs, VK_CONTROL, false);
            }
            break;
        }
    }

    if (!inputs.empty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

} // namespace detail

/// Manages the optional WH_MOUSE_LL hook and Driver-Mode DPI button HID listener.
class ButtonRemapper {
public:
    static constexpr UINT WM_REMAP_EVENT = WM_APP + 20;

    ButtonRemapper() = default;
    ~ButtonRemapper() { stop(); }
    ButtonRemapper(const ButtonRemapper&) = delete;
    ButtonRemapper& operator=(const ButtonRemapper&) = delete;
    ButtonRemapper(ButtonRemapper&&) = delete;
    ButtonRemapper& operator=(ButtonRemapper&&) = delete;

    void configure(HWND target_hwnd, const Settings& settings) {
        target_hwnd_.store(target_hwnd, std::memory_order_relaxed);
        {
            const std::scoped_lock lock{mu_};
            mappings_ = settings.buttons;
        }

        remapped_mouse_[0].store(is_binding_remapped(settings.buttons.middle), std::memory_order_relaxed);
        remapped_mouse_[1].store(is_binding_remapped(settings.buttons.x1), std::memory_order_relaxed);
        remapped_mouse_[2].store(is_binding_remapped(settings.buttons.x2), std::memory_order_relaxed);

        if (any_mouse_hook_needed(settings)) {
            install_hook();
        } else {
            remove_hook();
        }

        if (any_driver_mode_needed(settings)) {
            start_dpi_listener();
        } else {
            stop_dpi_listener();
        }
    }

    void stop() noexcept {
        remove_hook();
        stop_dpi_listener();
    }

    [[nodiscard]] bool hook_active() const noexcept {
        return hook_ != nullptr;
    }

    [[nodiscard]] bool dpi_listener_active() const noexcept {
        return dpi_listener_open_.load(std::memory_order_relaxed);
    }

    /// Executes a remapped button event on the service message thread.
    /// Returns +1 for DpiCycleUp, -1 for DpiCycleDown, or 0 otherwise.
    [[nodiscard]] int dispatch_event(ButtonId btn, bool is_down) {
        ButtonBinding binding{};
        {
            const std::scoped_lock lock{mu_};
            binding = get_binding(mappings_, btn);
        }
        const ActionKind kind = parse_action_kind(binding.kind);
        if (kind == ActionKind::DpiCycleUp) {
            return is_down ? 1 : 0;
        }
        if (kind == ActionKind::DpiCycleDown) {
            return is_down ? -1 : 0;
        }
        detail::send_binding_event(binding, is_down);
        return 0;
    }

    /// Handles WM_INPUT fallback when a vendor HID collection is routed via Raw Input.
    void handle_raw_input(HRAWINPUT lparam) {
        if (direct_reader_active_.load(std::memory_order_relaxed)) return;
        UINT size = 0;
        if (GetRawInputData(lparam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
            return;
        }
        std::vector<uint8_t> buf(size, 0);
        if (GetRawInputData(lparam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
            return;
        }
        const auto* raw = reinterpret_cast<const RAWINPUT*>(buf.data());
        if (raw->header.dwType != RIM_TYPEHID) return;

        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT info_size = sizeof(info);
        if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICEINFO, &info, &info_size) == static_cast<UINT>(-1)) {
            return;
        }
        if (info.hid.dwVendorId != RAZER_VID || info.hid.dwProductId != DEATHADDER_ELITE_PID) {
            return;
        }

        const uint8_t* ptr = raw->data.hid.bRawData;
        const size_t report_sz = raw->data.hid.dwSizeHid;
        for (size_t i = 0; i < raw->data.hid.dwCount; ++i) {
            const uint8_t* rep = ptr + (i * report_sz);
            if (report_sz >= 16 && rep[0] == 0x04) {
                process_rep4(std::span<const uint8_t, 16>{rep, 16});
            }
        }
    }

private:
    static inline ButtonRemapper* active_instance_ = nullptr;

    static LRESULT CALLBACK low_level_mouse_proc(int n_code, WPARAM w_param, LPARAM l_param) {
        ButtonRemapper* self = active_instance_;
        if (n_code == HC_ACTION && self != nullptr) {
            const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
            if ((ms->flags & LLMHF_INJECTED) == 0) {
                std::optional<ButtonId> btn;
                bool is_down = false;
                switch (w_param) {
                    case WM_MBUTTONDOWN:
                        btn = ButtonId::Middle;
                        is_down = true;
                        break;
                    case WM_MBUTTONUP:
                        btn = ButtonId::Middle;
                        is_down = false;
                        break;
                    case WM_XBUTTONDOWN:
                        if (HIWORD(ms->mouseData) == XBUTTON1) { btn = ButtonId::X1; }
                        else if (HIWORD(ms->mouseData) == XBUTTON2) { btn = ButtonId::X2; }
                        is_down = true;
                        break;
                    case WM_XBUTTONUP:
                        if (HIWORD(ms->mouseData) == XBUTTON1) { btn = ButtonId::X1; }
                        else if (HIWORD(ms->mouseData) == XBUTTON2) { btn = ButtonId::X2; }
                        is_down = false;
                        break;
                    default:
                        break;
                }
                if (btn.has_value()) {
                    const auto idx = static_cast<size_t>(*btn);
                    if (idx < 3 && self->remapped_mouse_[idx].load(std::memory_order_relaxed)) {
                        if (HWND target = self->target_hwnd_.load(std::memory_order_relaxed)) {
                            PostMessageW(target, WM_REMAP_EVENT,
                                         static_cast<WPARAM>(*btn), is_down ? 1 : 0);
                        }
                        return 1;
                    }
                }
            }
        }
        return CallNextHookEx(nullptr, n_code, w_param, l_param);
    }

    void install_hook() noexcept {
        if (hook_ != nullptr) return;
        active_instance_ = this;
        hook_ = SetWindowsHookExW(WH_MOUSE_LL, &ButtonRemapper::low_level_mouse_proc,
                                  GetModuleHandleW(nullptr), 0);
    }

    void remove_hook() noexcept {
        if (hook_ != nullptr) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        if (active_instance_ == this) {
            active_instance_ = nullptr;
        }
    }

    void process_rep4(std::span<const uint8_t, 16> data) noexcept {
        // Mirrors razermouse_driver.c: Report ID 0x04 holds active button bytes in [1..15].
        // 0x20 = DPI Up, 0x21 = DPI Down.
        const auto contains_code = [](std::span<const uint8_t, 16> arr, uint8_t code) noexcept {
            for (size_t i = 1; i < arr.size(); ++i) {
                if (arr[i] == code) return true;
            }
            return false;
        };

        for (const auto [code, btn] : {
                 std::pair{uint8_t{0x20}, ButtonId::DpiUp},
                 std::pair{uint8_t{0x21}, ButtonId::DpiDown},
             }) {
            const bool was_down = contains_code(prev_rep4_, code);
            const bool now_down = contains_code(data, code);
            if (was_down != now_down) {
                if (HWND target = target_hwnd_.load(std::memory_order_relaxed)) {
                    PostMessageW(target, WM_REMAP_EVENT,
                                 static_cast<WPARAM>(btn), now_down ? 1 : 0);
                }
            }
        }
        std::ranges::copy(data, prev_rep4_.begin());
    }

    void start_dpi_listener() {
        if (dpi_thread_.joinable()) return;
        prev_rep4_.fill(0);
        register_raw_hid_fallback();
        dpi_stop_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        dpi_thread_ = std::jthread([this](const std::stop_token& st) { dpi_reader_loop(st); });
    }

    void stop_dpi_listener() noexcept {
        unregister_raw_hid_fallback();
        if (dpi_stop_event_) SetEvent(dpi_stop_event_.get());
        if (dpi_thread_.joinable()) {
            dpi_thread_.request_stop();
            dpi_thread_.join();
        }
        dpi_stop_event_.reset();
        direct_reader_active_.store(false, std::memory_order_relaxed);
        dpi_listener_open_.store(false, std::memory_order_relaxed);
    }

    void register_raw_hid_fallback() {
        HWND target = target_hwnd_.load(std::memory_order_relaxed);
        if (target == nullptr || !raw_pages_.empty()) return;
        const auto cols = DeathAdderEliteDevice::list_collections();
        std::vector<RAWINPUTDEVICE> rids;
        for (const auto& c : cols) {
            if (c.input_len < 16) continue;
            // Skip standard mouse/keyboard TLCs; register vendor or generic non-kbd/mouse TLCs.
            if (c.usage_page == 0x01 && (c.usage == 0x02 || c.usage == 0x06)) continue;
            const uint32_t key = (static_cast<uint32_t>(c.usage_page) << 16u) | c.usage;
            if (std::ranges::find(raw_pages_, key) != raw_pages_.end()) continue;
            raw_pages_.push_back(key);
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = c.usage_page;
            rid.usUsage = c.usage;
            rid.dwFlags = RIDEV_INPUTSINK;
            rid.hwndTarget = target;
            rids.push_back(rid);
        }
        if (!rids.empty()) {
            RegisterRawInputDevices(rids.data(), static_cast<UINT>(rids.size()), sizeof(RAWINPUTDEVICE));
        }
    }

    void unregister_raw_hid_fallback() noexcept {
        if (raw_pages_.empty()) return;
        for (const uint32_t key : raw_pages_) {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = static_cast<USHORT>(key >> 16u);
            rid.usUsage = static_cast<USHORT>(key & 0xFFFFu);
            rid.dwFlags = RIDEV_REMOVE;
            rid.hwndTarget = nullptr;
            RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
        }
        raw_pages_.clear();
    }

    void dpi_reader_loop(const std::stop_token& st) {
        struct Reader {
            wil::unique_hfile file;
            wil::unique_event_nothrow ev;
            OVERLAPPED ov{};
            std::vector<uint8_t> buf;
            bool pending = false;
        };

        std::vector<Reader> readers;
        for (const auto& c : DeathAdderEliteDevice::list_collections()) {
            if (c.input_len < 16) continue;
            wil::unique_hfile h{CreateFileW(
                c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
            if (!h) continue;
            HidD_SetNumInputBuffers(h.get(), 64);
            Reader r{};
            r.file = std::move(h);
            r.ev.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            r.ov.hEvent = r.ev.get();
            r.buf.resize(c.input_len, 0);
            readers.push_back(std::move(r));
        }

        direct_reader_active_.store(!readers.empty(), std::memory_order_relaxed);
        dpi_listener_open_.store(!readers.empty() || !raw_pages_.empty(), std::memory_order_relaxed);
        if (readers.empty()) return;

        while (!st.stop_requested()) {
            std::vector<HANDLE> handles;
            handles.reserve(readers.size() + 1);
            if (dpi_stop_event_) handles.push_back(dpi_stop_event_.get());

            for (auto& r : readers) {
                if (!r.pending) {
                    ResetEvent(r.ev.get());
                    r.ov = OVERLAPPED{};
                    r.ov.hEvent = r.ev.get();
                    DWORD got = 0;
                    const BOOL ok = ReadFile(r.file.get(), r.buf.data(), static_cast<DWORD>(r.buf.size()), &got, &r.ov);
                    if (ok) {
                        if (got >= 16 && r.buf[0] == 0x04) {
                            process_rep4(std::span<const uint8_t, 16>{r.buf.data(), 16});
                        }
                        continue;
                    }
                    if (GetLastError() == ERROR_IO_PENDING) {
                        r.pending = true;
                    } else {
                        continue;
                    }
                }
                handles.push_back(r.ev.get());
            }

            if (handles.empty()) break;
            const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
            if (dpi_stop_event_ && wait == WAIT_OBJECT_0) break;

            for (auto& r : readers) {
                if (!r.pending) continue;
                DWORD got = 0;
                if (GetOverlappedResult(r.file.get(), &r.ov, &got, FALSE)) {
                    r.pending = false;
                    if (got >= 16 && r.buf[0] == 0x04) {
                        process_rep4(std::span<const uint8_t, 16>{r.buf.data(), 16});
                    }
                } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
                    r.pending = false;
                }
            }
        }

        for (auto& r : readers) {
            if (r.pending) CancelIoEx(r.file.get(), &r.ov);
        }
    }

    std::mutex mu_;
    std::atomic<HWND> target_hwnd_{nullptr};
    ButtonMappings mappings_{};
    std::array<std::atomic<bool>, 3> remapped_mouse_{};
    std::array<uint8_t, 16> prev_rep4_{};
    HHOOK hook_ = nullptr;
    std::vector<uint32_t> raw_pages_;
    std::jthread dpi_thread_;
    wil::unique_event_nothrow dpi_stop_event_;
    std::atomic<bool> direct_reader_active_{false};
    std::atomic<bool> dpi_listener_open_{false};
};

} // namespace da_elite
