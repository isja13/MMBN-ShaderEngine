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
    std::string slang_shader_2d;
    std::atomic_bool slang_shader_2d_updated = false;
    std::string slang_shader_gba;
    std::atomic_bool slang_shader_gba_updated = false;
    std::string slang_shader_ds;
    std::atomic_bool slang_shader_ds_updated = false;
    UINT display_width = 0;
    UINT display_height = 0;
    std::atomic_bool render_display_updated = false;

    void begin_config();
    void end_config();
    Config();
    ~Config();
};
extern Config *default_config;

#endif
