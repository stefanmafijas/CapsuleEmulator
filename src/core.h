#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "libretro.h"

// Minimal libretro frontend: loads one core DLL + one game at a time.
struct Core {
    bool load(const std::filesystem::path& dll, const std::filesystem::path& rom,
              const std::filesystem::path& sysDir, const std::filesystem::path& saveDir, std::string& err);
    void unload();
    bool loaded() const { return gameLoaded; }
    void run();

    bool saveState(const std::filesystem::path& path);
    bool loadState(const std::filesystem::path& path);
    void loadSram(const std::filesystem::path& path);
    bool saveSram(const std::filesystem::path& path); // writes only if SRAM changed

    uint8_t* memoryData(unsigned id, size_t* size);
    const retro_memory_map* memoryMap() const { return memDescs.empty() ? nullptr : &memMap; }

    // Output from the core. Software cores fill `frame` (XRGB8888); OpenGL cores render into `fboTex`.
    std::vector<uint32_t> frame;
    unsigned fw = 0, fh = 0;
    bool frameDirty = false;
    std::vector<int16_t> audio; // interleaved stereo
    double fps = 60, sampleRate = 32768;

    bool hwRender = false;      // core draws with OpenGL
    unsigned fboTex = 0, fboW = 0, fboH = 0;
    bool fboBottomLeft = true;  // GL image origin: flip when displaying
    int glVersion = 33;         // context the frontend created, e.g. 43 for 4.3

    // Input to the core.
    uint16_t buttons = 0; // bit n = RETRO_DEVICE_ID_JOYPAD_n
    int16_t analog[2][2] = {}; // [left/right stick][x/y]
    int16_t ptrX = 0, ptrY = 0;
    bool ptrDown = false;

    // Internals used by the C callbacks.
    retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
    std::string sysDir, saveDir;
    retro_hw_render_callback hw{};
    unsigned fbo = 0;
    std::vector<retro_memory_descriptor> memDescs;
    retro_memory_map memMap{};

private:
    void* handle = nullptr;
    bool inited = false, gameLoaded = false;
    std::vector<uint8_t> romData, lastSram;
    std::string romPath;
    unsigned fboDepth = 0;

    void createFbo(unsigned w, unsigned h);
    void destroyFbo();

    void (*p_init)() = nullptr;
    void (*p_deinit)() = nullptr;
    void (*p_set_environment)(retro_environment_t) = nullptr;
    void (*p_set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*p_set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*p_set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*p_set_input_poll)(retro_input_poll_t) = nullptr;
    void (*p_set_input_state)(retro_input_state_t) = nullptr;
    void (*p_get_system_info)(retro_system_info*) = nullptr;
    void (*p_get_system_av_info)(retro_system_av_info*) = nullptr;
    bool (*p_load_game)(const retro_game_info*) = nullptr;
    void (*p_unload_game)() = nullptr;
    void (*p_run)() = nullptr;
    size_t (*p_serialize_size)() = nullptr;
    bool (*p_serialize)(void*, size_t) = nullptr;
    bool (*p_unserialize)(const void*, size_t) = nullptr;
    void* (*p_get_memory_data)(unsigned) = nullptr;
    size_t (*p_get_memory_size)(unsigned) = nullptr;
};

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& out);
// Write to a temp file then rename, so a crash mid-write never corrupts the old file.
bool writeFileAtomic(const std::filesystem::path& path, const void* data, size_t size);
