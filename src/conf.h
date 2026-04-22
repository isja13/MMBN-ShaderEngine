#ifndef CONF_H
#define CONF_H

#include "main.h"

class Config {
    class Impl;
    Impl *impl;

public:
    std::atomic<HWND> hwnd = NULL;
    std::atomic_bool logging_enabled = false;
    std::vector<BYTE> log_toggle_hotkey;
    std::vector<BYTE> log_frame_hotkey;
    std::atomic_bool interp = false;
    std::atomic_bool linear = false;
    std::atomic_bool linear_test_updated = false;
    UINT linear_test_width = 0;
    UINT linear_test_height = 0;
    std::atomic_bool enhanced = false;
    std::string slang_shader_1;
    std::string slang_shader_2;
    std::string slang_shader_3;
    std::atomic_bool slang_shader_1_updated = false;
    std::atomic_bool slang_shader_2_updated = false;
    std::atomic_bool slang_shader_3_updated = false;
    std::atomic_bool shader_prev_pressed = false;
    std::atomic_bool shader_next_pressed = false;
    std::atomic<int> slang_active = 0;

    UINT display_width = 0;
    UINT display_height = 0;
    std::atomic_bool render_display_updated = false;

    // --- State Control ---
    std::atomic_bool shader_toggle = true;
    std::atomic<float> key_threshold = 0.03f;
    std::atomic_bool widescreen = false;
    std::atomic<float> ws_scale = 1.1830f;
    std::atomic<float> key_sharpness = 8.0f;

    // --- Keybinds ---
    std::vector<BYTE> hotkey_shader_toggle;
    std::vector<BYTE> hotkey_increase_text_aura;
    std::vector<BYTE> hotkey_decrease_text_aura;
    std::vector<BYTE> hotkey_increase_sharpness;
    std::vector<BYTE> hotkey_decrease_sharpness;
    std::vector<BYTE> hotkey_increase_ws_scale;
    std::vector<BYTE> hotkey_decrease_ws_scale;
    std::vector<BYTE> hotkey_shader_prev;
    std::vector<BYTE> hotkey_shader_next;
    std::vector<BYTE> hotkey_widescreen;

    // --- Controller ---
    std::vector<BYTE> hotkey_shader_toggle_pad;
    std::vector<BYTE> hotkey_increase_text_aura_pad;
    std::vector<BYTE> hotkey_decrease_text_aura_pad;
    std::vector<BYTE> hotkey_shader_prev_pad;
    std::vector<BYTE> hotkey_shader_next_pad;
    std::vector<BYTE> hotkey_widescreen_pad;


    // XInput button mappings (custom codes above VK range)
#define XINPUT_VK_BASE       0xE0
#define XINPUT_VK_LT   (XINPUT_VK_BASE + 0)  // Left Trigger
#define XINPUT_VK_RT   (XINPUT_VK_BASE + 1)  // Right Trigger  
#define XINPUT_VK_RS   (XINPUT_VK_BASE + 2)  // Right Stick 
#define XINPUT_VK_LS   (XINPUT_VK_BASE + 3)  // Left Stick
#define XINPUT_VK_A          (XINPUT_VK_BASE + 4)   // 0xE4
#define XINPUT_VK_B          (XINPUT_VK_BASE + 5)   // 0xE5
#define XINPUT_VK_X          (XINPUT_VK_BASE + 6)   // 0xE6
#define XINPUT_VK_Y          (XINPUT_VK_BASE + 7)   // 0xE7
#define XINPUT_VK_LB         (XINPUT_VK_BASE + 8)   // 0xE8
#define XINPUT_VK_RB         (XINPUT_VK_BASE + 9)   // 0xE9
#define XINPUT_VK_START      (XINPUT_VK_BASE + 10)  // 0xEA
#define XINPUT_VK_BACK       (XINPUT_VK_BASE + 11)  // 0xEB
#define XINPUT_VK_DPAD_UP    (XINPUT_VK_BASE + 12)  // 0xEC
#define XINPUT_VK_DPAD_DOWN  (XINPUT_VK_BASE + 13)  // 0xED
#define XINPUT_VK_DPAD_LEFT  (XINPUT_VK_BASE + 14)  // 0xEE
#define XINPUT_VK_DPAD_RIGHT (XINPUT_VK_BASE + 15)  // 0xEF

    void begin_config();
    void end_config();
    Config();
    ~Config();
};
extern Config *default_config;

#endif
