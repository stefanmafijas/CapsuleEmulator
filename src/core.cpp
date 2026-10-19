#include "core.h"
#include "gl.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

static Core* g = nullptr; // libretro callbacks carry no user pointer

bool readFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool writeFileAtomic(const fs::path& path, const void* data, size_t size) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.write((const char*)data, size)) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

static void RETRO_CALLCONV logCb(retro_log_level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static uintptr_t RETRO_CALLCONV hwFramebufferCb() { return g->fbo; }

static retro_proc_address_t RETRO_CALLCONV hwProcCb(const char* sym) {
    return (retro_proc_address_t)SDL_GL_GetProcAddress(sym);
}

static bool RETRO_CALLCONV envCb(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto f = *(const retro_pixel_format*)data;
        if (f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565)
            return false;
        g->fmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char**)data = g->sysDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char**)data = g->saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((retro_log_callback*)data)->log = logCb;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        auto* av = (const retro_system_av_info*)data;
        g->fps = av->timing.fps;
        g->sampleRate = av->timing.sample_rate;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: // frame size is taken from each video callback
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* cb = (retro_hw_render_callback*)data;
        bool gl = cb->context_type == RETRO_HW_CONTEXT_OPENGL || cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
        if (!gl || (int)(cb->version_major * 10 + cb->version_minor) > g->glVersion) return false;
        cb->get_current_framebuffer = hwFramebufferCb;
        cb->get_proc_address = hwProcCb;
        g->hw = *cb;
        g->hwRender = true;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: { // copied: the core's array need not outlive this call
        auto* map = (const retro_memory_map*)data;
        g->memDescs.assign(map->descriptors, map->descriptors + map->num_descriptors);
        g->memMap = {g->memDescs.data(), (unsigned)g->memDescs.size()};
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        fprintf(stderr, "%s\n", ((const retro_message*)data)->msg);
        return true;
    default:
        return false; // core falls back to defaults
    }
}

static void RETRO_CALLCONV videoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return; // dupe: keep last frame
    g->fw = w;
    g->fh = h;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) { // already drawn into our FBO
        g->frameDirty = true;
        return;
    }
    g->frame.resize((size_t)w * h);
    for (unsigned y = 0; y < h; ++y) {
        const uint8_t* row = (const uint8_t*)data + y * pitch;
        uint32_t* dst = &g->frame[(size_t)y * w];
        if (g->fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            memcpy(dst, row, w * 4);
        } else if (g->fmt == RETRO_PIXEL_FORMAT_RGB565) {
            const uint16_t* src = (const uint16_t*)row;
            for (unsigned x = 0; x < w; ++x) {
                uint16_t p = src[x];
                uint32_t r = (p >> 11) & 31, gg = (p >> 5) & 63, b = p & 31;
                dst[x] = (r << 19 | r << 14) & 0xFF0000 | (gg << 10 | gg << 4) & 0xFF00 | (b << 3 | b >> 2);
            }
        } else {
            const uint16_t* src = (const uint16_t*)row;
            for (unsigned x = 0; x < w; ++x) {
                uint16_t p = src[x];
                uint32_t r = (p >> 10) & 31, gg = (p >> 5) & 31, b = p & 31;
                dst[x] = (r << 19 | r << 14) & 0xFF0000 | (gg << 11 | gg << 6) & 0xFF00 | (b << 3 | b >> 2);
            }
        }
    }
    g->frameDirty = true;
}

static void RETRO_CALLCONV audioCb(int16_t l, int16_t r) {
    g->audio.push_back(l);
    g->audio.push_back(r);
}

static size_t RETRO_CALLCONV audioBatchCb(const int16_t* data, size_t frames) {
    g->audio.insert(g->audio.end(), data, data + frames * 2);
    return frames;
}

static void RETRO_CALLCONV pollCb() {}

static int16_t RETRO_CALLCONV inputCb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    if (device == RETRO_DEVICE_JOYPAD)
        return id == RETRO_DEVICE_ID_JOYPAD_MASK ? g->buttons : (g->buttons >> id) & 1;
    if (device == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y)
        return g->analog[index][id];
    if (device == RETRO_DEVICE_MOUSE && id == RETRO_DEVICE_ID_MOUSE_LEFT) return g->ptrDown; // "mouse touchscreen" modes
    if (device == RETRO_DEVICE_POINTER && index == 0) {
        if (id == RETRO_DEVICE_ID_POINTER_X) return g->ptrX;
        if (id == RETRO_DEVICE_ID_POINTER_Y) return g->ptrY;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return g->ptrDown;
    }
    return 0;
}

bool Core::load(const fs::path& dll, const fs::path& rom, const fs::path& sys, const fs::path& save, std::string& err) {
    unload();
    g = this;
    sysDir = sys.u8string();
    saveDir = save.u8string();
    fmt = RETRO_PIXEL_FORMAT_0RGB1555;
    frame.clear();
    fw = fh = 0;
    hwRender = false;
    hw = {};
    memDescs.clear();

    handle = SDL_LoadObject(dll.u8string().c_str());
    if (!handle) {
        err = "Cannot load core " + dll.filename().u8string() + ": " + SDL_GetError();
        return false;
    }
#define SYM(n)                                                         \
    if (!(p_##n = (decltype(p_##n))SDL_LoadFunction(handle, "retro_" #n))) { \
        err = "Core is missing retro_" #n;                              \
        unload();                                                       \
        return false;                                                   \
    }
    SYM(init) SYM(deinit) SYM(set_environment) SYM(set_video_refresh) SYM(set_audio_sample)
    SYM(set_audio_sample_batch) SYM(set_input_poll) SYM(set_input_state) SYM(get_system_info)
    SYM(get_system_av_info) SYM(load_game) SYM(unload_game) SYM(run) SYM(serialize_size)
    SYM(serialize) SYM(unserialize) SYM(get_memory_data) SYM(get_memory_size)
#undef SYM

    p_set_environment(envCb);
    p_init();
    inited = true;
    p_set_video_refresh(videoCb);
    p_set_audio_sample(audioCb);
    p_set_audio_sample_batch(audioBatchCb);
    p_set_input_poll(pollCb);
    p_set_input_state(inputCb);

    retro_system_info sysInfo{};
    p_get_system_info(&sysInfo);
    // need_fullpath cores (3DS: multi-GB images) read the file themselves.
    if (!sysInfo.need_fullpath && !readFile(rom, romData)) {
        err = "Cannot read ROM " + rom.u8string();
        unload();
        return false;
    }
    romPath = rom.u8string();
    retro_game_info info{romPath.c_str(), romData.empty() ? nullptr : romData.data(), romData.size(), nullptr};
    if (!p_load_game(&info)) {
        err = "Core rejected the ROM";
        unload();
        return false;
    }
    gameLoaded = true;

    retro_system_av_info av{};
    p_get_system_av_info(&av);
    fps = av.timing.fps > 0 ? av.timing.fps : 60;
    sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 32768;
    if (hwRender) {
        // shortcut: FBO is sized once from max geometry; recreate it if a core ever grows past that.
        createFbo(std::max(av.geometry.max_width, av.geometry.base_width), std::max(av.geometry.max_height, av.geometry.base_height));
        if (hw.context_reset) hw.context_reset();
    }
    return true;
}

void Core::run() {
    p_run();
    if (!hwRender) return;
    // Put back the GL state the UI renderer relies on.
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void Core::createFbo(unsigned w, unsigned h) {
    glGenTextures(1, &fboTex);
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
    if (hw.depth) {
        gl::GenRenderbuffers(1, &fboDepth);
        gl::BindRenderbuffer(GL_RENDERBUFFER, fboDepth);
        gl::RenderbufferStorage(GL_RENDERBUFFER, hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, w, h);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, hw.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER, fboDepth);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    fboW = w;
    fboH = h;
    fboBottomLeft = hw.bottom_left_origin;
}

void Core::destroyFbo() {
    if (fbo) gl::DeleteFramebuffers(1, &fbo);
    if (fboDepth) gl::DeleteRenderbuffers(1, &fboDepth);
    if (fboTex) glDeleteTextures(1, &fboTex);
    fbo = fboDepth = fboTex = fboW = fboH = 0;
}

uint8_t* Core::memoryData(unsigned id, size_t* size) {
    *size = gameLoaded ? p_get_memory_size(id) : 0;
    return gameLoaded ? (uint8_t*)p_get_memory_data(id) : nullptr;
}

void Core::unload() {
    if (gameLoaded) p_unload_game();
    if (hwRender && hw.context_destroy) hw.context_destroy();
    destroyFbo();
    hwRender = false;
    memDescs.clear();
    if (inited) p_deinit();
    if (handle) SDL_UnloadObject(handle);
    handle = nullptr;
    inited = gameLoaded = false;
    romData.clear();
    lastSram.clear();
    audio.clear();
    buttons = 0;
    ptrDown = false;
}

bool Core::saveState(const fs::path& path) {
    size_t size = p_serialize_size();
    if (!size) return false;
    std::vector<uint8_t> buf(size);
    return p_serialize(buf.data(), size) && writeFileAtomic(path, buf.data(), size);
}

bool Core::loadState(const fs::path& path) {
    std::vector<uint8_t> buf;
    return readFile(path, buf) && !buf.empty() && p_unserialize(buf.data(), buf.size());
}

void Core::loadSram(const fs::path& path) {
    auto* mem = (uint8_t*)p_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = p_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!mem || !size) return; // core manages its own saves
    std::vector<uint8_t> buf;
    if (readFile(path, buf)) memcpy(mem, buf.data(), std::min(size, buf.size()));
    lastSram.assign(mem, mem + size);
}

bool Core::saveSram(const fs::path& path) {
    auto* mem = (uint8_t*)p_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = p_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!mem || !size) return true;
    if (lastSram.size() == size && memcmp(lastSram.data(), mem, size) == 0) return true;
    if (!writeFileAtomic(path, mem, size)) return false;
    lastSram.assign(mem, mem + size);
    return true;
}
