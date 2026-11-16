#include <da_elite/razer_protocol.hpp>
#include <da_elite/settings.hpp>

#include <cassert>
#include <cstdio>
#include <print>

int main() try {
    using namespace da_elite;
    using namespace da_elite::protocol;

    // 1. Poll rate encoding / decoding
    assert(encode_poll_rate(1000) == 0x01);
    assert(encode_poll_rate(500) == 0x02);
    assert(encode_poll_rate(125) == 0x08);
    assert(decode_poll_rate(0x01) == 1000);
    assert(decode_poll_rate(0x02) == 500);
    assert(decode_poll_rate(0x08) == 125);
    assert(!decode_poll_rate(0x04).has_value());

    // 2. Polling rate report & CRC
    {
        const auto req = make_set_polling_rate(1000);
        const auto buf = pack_feature_report(req);
        assert(buf.size() == 91);
        assert(buf[0] == 0x00); // Report ID 0
        assert(buf[1] == 0x00); // status
        assert(buf[2] == 0x3F); // DeathAdder Elite transaction_id
        assert(buf[6] == 0x01); // data_size
        assert(buf[7] == 0x00); // command_class
        assert(buf[8] == 0x05); // command_id (set polling rate)
        assert(buf[9] == 0x01); // arg0 = 1000 Hz
        // CRC = XOR of report bytes [2..87] => buf bytes [3..88]
        uint8_t expected_crc = 0;
        for (size_t i = 3; i < 89; ++i) expected_crc ^= buf[i];
        assert(buf[89] == expected_crc);
        assert(expected_crc == (0x01u ^ 0x00u ^ 0x05u ^ 0x01u));

        const auto unpacked = unpack_feature_report(buf);
        assert(response_matches(req, unpacked));
        assert(unpacked.arguments[0] == 0x01);
    }

    // 3. DPI report & clamping
    {
        assert(clamp_dpi(50) == 100);
        assert(clamp_dpi(32000) == 16000);
        const auto req = make_set_dpi_xy(1600, 800);
        const auto buf = pack_feature_report(req);
        assert(buf[2] == 0x3F);
        assert(buf[6] == 0x07); // data_size
        assert(buf[7] == 0x04); // command_class
        assert(buf[8] == 0x05); // command_id
        assert(buf[9] == 0x01); // VARSTORE
        assert(buf[10] == 0x06 && buf[11] == 0x40); // 1600 = 0x0640
        assert(buf[12] == 0x03 && buf[13] == 0x20); // 800  = 0x0320
    }

    // 4. Device mode report
    {
        const auto drv = make_set_device_mode(DeviceMode::Driver);
        assert(drv.command_class == 0x00 && drv.command_id == 0x04 && drv.data_size == 0x02);
        assert(drv.arguments[0] == 0x03 && drv.arguments[1] == 0x00);

        const auto norm = make_set_device_mode(DeviceMode::Normal);
        assert(norm.arguments[0] == 0x00 && norm.arguments[1] == 0x00);
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
        assert(s.polling_rate == 1000);
        assert(s.dpi_x == 16000 && s.dpi_y == 16000);
        assert((s.dpi_stages == std::vector<uint16_t>{400, 800, 3200}));
        assert(!any_driver_mode_needed(s));
        assert(any_mouse_hook_needed(s));

        s.buttons.dpi_up.kind = "dpi_up";
        assert(any_driver_mode_needed(s));

        const auto json = glz::write<JSON_WRITE>(s);
        assert(json.has_value());
        Settings roundtrip{};
        const auto err = glz::read<JSON_READ>(roundtrip, *json);
        assert(!err);
        assert(roundtrip.polling_rate == 1000);
        assert(roundtrip.dpi_x == 16000);
        assert(roundtrip.buttons.x1.vk == VK_MEDIA_PLAY_PAUSE);
        assert(roundtrip.buttons.x1.mods == MOD_CONTROL);
        assert(roundtrip.buttons.dpi_up.kind == "dpi_up");
    }

    // 6. Windows startup registry round-trip (only mutate if not already enabled)
    if (!is_startup_enabled()) {
        assert(set_startup_enabled(true));
        assert(is_startup_enabled());
        assert(set_startup_enabled(false));
        assert(!is_startup_enabled());
    }

    std::println("All protocol & settings tests passed.");
    return 0;
} catch (...) {
    std::fputs("Test failed with unexpected exception.\n", stderr);
    return 1;
}
