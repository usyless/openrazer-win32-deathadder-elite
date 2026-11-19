#include <da_elite/razer_protocol.hpp>
#include <da_elite/settings.hpp>

#include <cstdio>
#include <cstdlib>
#include <print>

#define DA_CHECK(cond)                                                                             \
    do {                                                                                           \
        const bool da_check_ok_ = static_cast<bool>(cond);                                         \
        if (!da_check_ok_) {                                                                       \
            std::println(stderr, "Test check failed at {}:{} -> {}", __FILE__, __LINE__, #cond); \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int main() try {
    using namespace da_elite;
    using namespace da_elite::protocol;

    // 1. Poll rate encoding / decoding
    DA_CHECK(encode_poll_rate(1000) == 0x01);
    DA_CHECK(encode_poll_rate(500) == 0x02);
    DA_CHECK(encode_poll_rate(125) == 0x08);
    DA_CHECK(decode_poll_rate(0x01) == 1000);
    DA_CHECK(decode_poll_rate(0x02) == 500);
    DA_CHECK(decode_poll_rate(0x08) == 125);
    DA_CHECK(!decode_poll_rate(0x04).has_value());

    // 2. Polling rate report & CRC
    {
        const auto req = make_set_polling_rate(1000);
        const auto buf = pack_feature_report(req);
        DA_CHECK(buf.size() == 91);
        DA_CHECK(buf[0] == 0x00); // Report ID 0
        DA_CHECK(buf[1] == 0x00); // status
        DA_CHECK(buf[2] == 0x3F); // DeathAdder Elite transaction_id
        DA_CHECK(buf[6] == 0x01); // data_size
        DA_CHECK(buf[7] == 0x00); // command_class
        DA_CHECK(buf[8] == 0x05); // command_id (set polling rate)
        DA_CHECK(buf[9] == 0x01); // arg0 = 1000 Hz
        // CRC = XOR of report bytes [2..87] => buf bytes [3..88]
        uint8_t expected_crc = 0;
        for (size_t i = 3; i < 89; ++i) expected_crc ^= buf[i];
        DA_CHECK(buf[89] == expected_crc);
        DA_CHECK(expected_crc == (0x01u ^ 0x00u ^ 0x05u ^ 0x01u));

        const auto unpacked = unpack_feature_report(buf);
        DA_CHECK(response_matches(req, unpacked));
        DA_CHECK(unpacked.arguments[0] == 0x01);
    }

    // 3. DPI report & clamping
    {
        DA_CHECK(clamp_dpi(50) == 100);
        DA_CHECK(clamp_dpi(32000) == 16000);
        const auto req = make_set_dpi_xy(1600, 800);
        const auto buf = pack_feature_report(req);
        DA_CHECK(buf[2] == 0x3F);
        DA_CHECK(buf[6] == 0x07); // data_size
        DA_CHECK(buf[7] == 0x04); // command_class
        DA_CHECK(buf[8] == 0x05); // command_id
        DA_CHECK(buf[9] == 0x01); // VARSTORE
        DA_CHECK(buf[10] == 0x06 && buf[11] == 0x40); // 1600 = 0x0640
        DA_CHECK(buf[12] == 0x03 && buf[13] == 0x20); // 800  = 0x0320
    }

    // 4. Device mode report
    {
        const auto drv = make_set_device_mode(DeviceMode::Driver);
        DA_CHECK(drv.command_class == 0x00 && drv.command_id == 0x04 && drv.data_size == 0x02);
        DA_CHECK(drv.arguments[0] == 0x03 && drv.arguments[1] == 0x00);

        const auto norm = make_set_device_mode(DeviceMode::Normal);
        DA_CHECK(norm.arguments[0] == 0x00 && norm.arguments[1] == 0x00);
    }

    // 5. Settings JSON round-trip & sanitization
    {
        Settings s{};
        s.polling_rate = 999; // invalid -> should sanitize to 1000
        s.dpi_x = 50000;      // clamps to 16000
        s.link_xy = true;
        s.dpi_stages = {3200, 800, 800, 400};
        s.buttons.x1.kind = "key";
        s.buttons.x1.vk = VK_MEDIA_PLAY_PAUSE;
        s.buttons.x1.mods = MOD_CONTROL;
        sanitize_settings(s);
        DA_CHECK(s.polling_rate == 1000);
        DA_CHECK(s.dpi_x == 16000 && s.dpi_y == 16000);
        DA_CHECK((s.dpi_stages == std::vector<uint16_t>{400, 800, 3200}));
        DA_CHECK(!any_driver_mode_needed(s));
        DA_CHECK(any_mouse_hook_needed(s));

        s.buttons.dpi_up.kind = "dpi_up";
        DA_CHECK(any_driver_mode_needed(s));

        const auto json = glz::write<JSON_WRITE>(s);
        DA_CHECK(json.has_value());
        Settings roundtrip{};
        const auto err = glz::read<JSON_READ>(roundtrip, *json);
        DA_CHECK(!err);
        DA_CHECK(roundtrip.polling_rate == 1000);
        DA_CHECK(roundtrip.dpi_x == 16000);
        DA_CHECK(roundtrip.buttons.x1.vk == VK_MEDIA_PLAY_PAUSE);
        DA_CHECK(roundtrip.buttons.x1.mods == MOD_CONTROL);
        DA_CHECK(roundtrip.buttons.dpi_up.kind == "dpi_up");
    }

    // 6. Windows startup registry round-trip (only mutate if not already enabled)
    if (!is_startup_enabled()) {
        DA_CHECK(set_startup_enabled(true));
        DA_CHECK(is_startup_enabled());
        DA_CHECK(set_startup_enabled(false));
        DA_CHECK(!is_startup_enabled());
    }

    // 7. Zero-allocation helpers (ASCII case-insensitive match, RuntimeBinding, Glaze scratch reuse)
    {
        DA_CHECK(contains_ascii_icase(L"\\\\?\\HID#VID_1532&PID_005C&MI_00", L"vid_1532"));
        DA_CHECK(contains_ascii_icase(L"\\\\?\\hid#vid_1532&pid_005c&mi_00", L"PID_005C"));
        DA_CHECK(!contains_ascii_icase(L"\\\\?\\HID#VID_1532&PID_005D", L"pid_005c"));
        DA_CHECK(find_ascii_icase(L"abc&MI_02&Col01", L"&mi_") == 3);

        ButtonBinding bb{};
        bb.kind = "key";
        bb.vk = VK_SPACE;
        bb.mods = MOD_CONTROL | MOD_SHIFT;
        const RuntimeBinding rb = to_runtime_binding(bb);
        DA_CHECK(rb.kind == ActionKind::KeyCombo);
        DA_CHECK(rb.vk == VK_SPACE);
        DA_CHECK(rb.mods == (MOD_CONTROL | MOD_SHIFT));

        Settings s{};
        std::string scratch;
        scratch.reserve(2048);
        const char* const initial_ptr = scratch.data();
        const size_t initial_cap = scratch.capacity();
        const auto err1 = glz::write<JSON_WRITE>(s, scratch);
        DA_CHECK(!err1);
        DA_CHECK(!scratch.empty());
        DA_CHECK(scratch.data() == initial_ptr);
        DA_CHECK(scratch.capacity() == initial_cap);
        s.dpi_x = 1600;
        scratch.clear();
        const auto err2 = glz::write<JSON_WRITE>(s, scratch);
        DA_CHECK(!err2);
        DA_CHECK(scratch.data() == initial_ptr);
        DA_CHECK(scratch.capacity() == initial_cap);

        // FixedString / FixedWString / to_fixed_str / to_fixed_wstr / append_to / concat_strings
        FixedString<8> fs{"hello"};
        const FixedString<16> fs2{"hello"};
        const FixedString<8> fs3{"world"};
        DA_CHECK(fs == "hello");
        DA_CHECK(fs == fs2);
        DA_CHECK(fs != fs3);
        DA_CHECK(fs < fs3);
        fs.append(" world");
        DA_CHECK(fs == "hello w"); // truncated at capacity N - 1 = 7
        DA_CHECK(fs.size() == 7);

        const auto n_str = to_fixed_str(1600);
        DA_CHECK(n_str == "1600");
        const auto f_str = to_fixed_str(12.5, 1);
        DA_CHECK(f_str == "12.5");
        const auto w_str = to_fixed_wstr(1000u);
        DA_CHECK(w_str == L"1000");
        const auto fw_str = to_fixed_wstr(3.25, 2);
        DA_CHECK(fw_str == L"3.25");

        const std::wstring joined = ulp::str::concat_strings(L"DPI=", w_str, L", Hz=", to_fixed_wstr(500));
        DA_CHECK(joined == L"DPI=1000, Hz=500");

        std::string app = "abc";
        append_to(app, "-", std::string_view{app}.substr(0, 2), "-", to_fixed_str(42));
        DA_CHECK(app == "abc-ab-42");

        DA_CHECK(format_vk_label(MOD_CONTROL | MOD_SHIFT, VK_F5) == L"Ctrl+Shift+F5");
        DA_CHECK(format_vk_label(0, 0) == L"(None)");
        DA_CHECK(parse_long(L"1600") == 1600);
        DA_CHECK(parse_long(L"-42") == -42);
        DA_CHECK(!parse_long(L"").has_value());
        DA_CHECK(!parse_long(L"abc").has_value());
        DA_CHECK(!parse_long(L"1000abc").has_value());
        DA_CHECK(!parse_long(L"12345678901234567890123456789012").has_value());
        DA_CHECK(to_wide(to_utf8(L"DeathAdder Elite — 800 DPI")) == L"DeathAdder Elite — 800 DPI");
        std::wstring appended = L"Err: ";
        append_utf8_as_wide(appended, to_utf8(L"DeathAdder Elite — ok"));
        DA_CHECK(appended == L"Err: DeathAdder Elite — ok");
        DA_CHECK(!exe_path().empty());
    }

    std::println("All protocol & settings tests passed.");
    return 0;
} catch (...) {
    std::fputs("Test failed with unexpected exception.\n", stderr);
    return 1;
}
