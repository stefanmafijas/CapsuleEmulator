#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include "core.h"
#include "games.h"
#include "gl.h"
#include "net.h"
#include "ra.h"
#include "rc_consoles.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL_syswm.h>

namespace fs = std::filesystem;

enum Action {
    A_UP, A_DOWN, A_LEFT, A_RIGHT, A_A, A_B, A_X, A_Y, A_L, A_R, A_ZL, A_ZR, A_START, A_SELECT,
    A_FF_HOLD, A_FF_TOGGLE, A_PAUSE, A_SAVE_STATE, A_LOAD_STATE, A_SLOT_PREV, A_SLOT_NEXT,
    A_FULLSCREEN, A_MENU, A_COUNT
};

// Gamepad binding: -1 none, 0..SDL_CONTROLLER_BUTTON_MAX-1 a button,
// kAxisBase + axis * 2 + (positive ? 1 : 0) a stick or trigger direction.
static const int kAxisBase = 100;
static const int kAxisThreshold = 16000; // about half travel
static int axisBind(SDL_GameControllerAxis a, bool positive) { return kAxisBase + a * 2 + positive; }

struct ActionInfo {
    const char* key;   // name in config.ini
    const char* label;
    SDL_Scancode def;
    int padDef;
    int retroId;       // -1 for hotkeys
};

// Pad face buttons follow Nintendo positions: A is the east button, B the south one.
static const ActionInfo kActions[A_COUNT] = {
    {"up", "D-Pad Up", SDL_SCANCODE_UP, SDL_CONTROLLER_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP},
    {"down", "D-Pad Down", SDL_SCANCODE_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"left", "D-Pad Left", SDL_SCANCODE_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
    {"right", "D-Pad Right", SDL_SCANCODE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
    {"a", "A", SDL_SCANCODE_X, SDL_CONTROLLER_BUTTON_B, RETRO_DEVICE_ID_JOYPAD_A},
    {"b", "B", SDL_SCANCODE_Z, SDL_CONTROLLER_BUTTON_A, RETRO_DEVICE_ID_JOYPAD_B},
    {"x", "X (DS/3DS)", SDL_SCANCODE_S, SDL_CONTROLLER_BUTTON_Y, RETRO_DEVICE_ID_JOYPAD_X},
    {"y", "Y (DS/3DS)", SDL_SCANCODE_A, SDL_CONTROLLER_BUTTON_X, RETRO_DEVICE_ID_JOYPAD_Y},
    {"l", "L", SDL_SCANCODE_Q, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, RETRO_DEVICE_ID_JOYPAD_L},
    {"r", "R", SDL_SCANCODE_W, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, RETRO_DEVICE_ID_JOYPAD_R},
    {"zl", "ZL (New 3DS)", SDL_SCANCODE_E, -1, RETRO_DEVICE_ID_JOYPAD_L2},
    {"zr", "ZR (New 3DS)", SDL_SCANCODE_R, -1, RETRO_DEVICE_ID_JOYPAD_R2},
    {"start", "Start", SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START},
    {"select", "Select", SDL_SCANCODE_RSHIFT, SDL_CONTROLLER_BUTTON_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT},
    {"ff_hold", "Fast forward (hold)", SDL_SCANCODE_TAB, axisBind(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true), -1},
    {"ff_toggle", "Fast forward (toggle)", SDL_SCANCODE_SPACE, -1, -1},
    {"pause", "Pause / resume", SDL_SCANCODE_P, -1, -1},
    {"save_state", "Save state", SDL_SCANCODE_F5, SDL_CONTROLLER_BUTTON_RIGHTSTICK, -1},
    {"load_state", "Load state", SDL_SCANCODE_F8, SDL_CONTROLLER_BUTTON_LEFTSTICK, -1},
    {"slot_prev", "Previous state slot", SDL_SCANCODE_F6, -1, -1},
    {"slot_next", "Next state slot", SDL_SCANCODE_F7, -1, -1},
    {"fullscreen", "Toggle fullscreen", SDL_SCANCODE_F11, -1, -1},
    {"menu", "Switch library / game", SDL_SCANCODE_ESCAPE, SDL_CONTROLLER_BUTTON_GUIDE, -1},
};

static std::string padName(int b) {
    static const char* buttons[] = {"A (south)", "B (east)", "X (west)", "Y (north)", "Back", "Guide", "Start",
                                    "Left stick click", "Right stick click", "LB", "RB", "D-Pad Up", "D-Pad Down",
                                    "D-Pad Left", "D-Pad Right", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4",
                                    "Touchpad"};
    static const char* axes[] = {"Left stick Left", "Left stick Right", "Left stick Up", "Left stick Down",
                                 "Right stick Left", "Right stick Right", "Right stick Up", "Right stick Down",
                                 "LT", "LT", "RT", "RT"};
    if (b >= 0 && b < (int)std::size(buttons)) return buttons[b];
    if (b >= kAxisBase && b < kAxisBase + (int)std::size(axes)) return axes[b - kAxisBase];
    return "";
}

struct GenInfo {
    const char* numeral;
    const char* region;
    uint32_t color;
    int mascot; // National Dex number drawn as the card's silhouette, 0 for none
};
static const int kCustomGen = kGenCount + 1; // the "Custom" card after the generations
static const GenInfo kGens[kCustomGen + 1] = {
    {}, {"I", "Kanto", 0xE3350D, 25}, {"II", "Johto", 0xC9A227, 249}, {"III", "Hoenn", 0x2E9E6A, 384},
    {"IV", "Sinnoh", 0x5B8DEF, 483}, {"V", "Unova", 0x5A5A66, 643}, {"VI", "Kalos", 0x1F5FBF, 716},
    {"VII", "Alola", 0xF08A24, 791}, {"VIII", "Galar", 0x7B2CBF, 888}, {"IX", "Paldea", 0xD9480F, 1007},
    {"", "Custom", 0x3A4157, 0},
};

// User-added ROMs on the Custom card. A deque keeps each Game's address stable as entries are added,
// and removed entries are only flagged, so pointers held elsewhere (running, selected...) stay valid.
struct CustomGame {
    std::string id, title, platform;
    bool removed = false;
    Game game{};
};
static std::deque<CustomGame> customGames;
static std::vector<const Game*> games; // kGames followed by live custom games; see refreshGames()

static void refreshGames() {
    games.clear();
    for (const Game& g : kGames) games.push_back(&g);
    for (CustomGame& c : customGames)
        if (!c.removed) games.push_back(&c.game);
}

static Sys sysFromExt(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".gb") return Sys::GB;
    if (ext == ".gbc") return Sys::GBC;
    if (ext == ".gba") return Sys::GBA;
    if (ext == ".nds") return Sys::NDS;
    if (ext == ".3ds" || ext == ".cci" || ext == ".cxi") return Sys::N3DS;
    if (ext == ".xci" || ext == ".nsp") return Sys::NSW;
    return Sys::Unsupported;
}

static const char* sysName(Sys s) {
    switch (s) {
    case Sys::GB:   return "Game Boy";
    case Sys::GBC:  return "Game Boy Color";
    case Sys::GBA:  return "Game Boy Advance";
    case Sys::NDS:  return "Nintendo DS";
    case Sys::N3DS: return "Nintendo 3DS";
    case Sys::NSW:  return "Nintendo Switch";
    default:        return "Unknown";
    }
}

static const int kSlots = 10;
static uint64_t maxRomSize(Sys s) { return s == Sys::NSW ? 32ull << 30 : 4ull << 30; } // Switch carts 32 GiB, 3DS 4 GiB
static const float kPanelW = 380;

static uint32_t raConsole(Sys s) {
    switch (s) {
    case Sys::GB:  return RC_CONSOLE_GAMEBOY;
    case Sys::GBC: return RC_CONSOLE_GAMEBOY_COLOR;
    case Sys::GBA: return RC_CONSOLE_GAMEBOY_ADVANCE;
    case Sys::NDS: return RC_CONSOLE_NINTENDO_DS;
    default:       return 0; // RetroAchievements has no 3DS support
    }
}

struct Config {
    SDL_Scancode keys[A_COUNT];
    int pad[A_COUNT];
    int ffSpeed = 4;
    bool achUnlockedFirst = true;
    std::string raUser, raToken;
    std::string switchEmu, switchArgs = "{rom}"; // external Switch emulator; {rom} becomes the quoted game path
};

static struct {
    fs::path base, roms, saves, states, system, cores, art, badges, config;
} dirs;

enum View { V_LIBRARY, V_GAME, V_ACHIEVEMENTS, V_SETTINGS, V_DONATE };

// Developer's PayPal donation link (https://paypal.me/... or https://www.paypal.com/...). Empty: Donate tab says so.
static const char* kDonateUrl = "https://paypal.me/stefanmafijas";

static Config cfg;
static Core core;
static const Game* running = nullptr;
static bool paused = false, ffToggle = false, fullscreen = false;
static int slot = 0, capture = -1; // capture: action being rebound
static bool capturePad = false;      // rebinding its gamepad input instead of its key
static std::vector<SDL_GameController*> pads;
static View view = V_LIBRARY;
static int openGen = 0;                  // 0 = generation list
static const Game* selected = nullptr;   // game shown in the side panel
static const Game* panelGame = nullptr;  // stays set while the panel animates closed
static const Game* achGame = nullptr;    // game shown in the achievements view
static float panelW = 0;
static char urlBuf[1024] = "", raUserBuf[128] = "", raPassBuf[128] = "";
static ImFont *fontBold = nullptr, *fontTitle = nullptr, *fontHuge = nullptr;
static SDL_Window* window = nullptr;
static GLuint frameTex = 0;
static unsigned frameTexW = 0, frameTexH = 0;
static SDL_AudioDeviceID audioDev = 0;
static std::string toastMsg;
static Uint32 toastUntil = 0, lastSramSave = 0;
static std::atomic<bool> quitting{false};

// One background ROM transfer (download or local copy) at a time, streamed to roms/<id>.part; the main loop finishes it when state == 2.
static struct {
    std::thread th;
    std::atomic<int> state{0}; // 0 idle, 1 running, 2 finished
    std::atomic<uint64_t> got{0}, total{0};
    std::atomic<bool> cancel{false};
    std::vector<uint8_t> head; // first kRomHeadSize bytes, for validation
    std::string error;
    const Game* game = nullptr;
    bool isCopy = false; // copying a local file rather than downloading
} download;

// Images (generation mascots, achievement badges): a worker downloads missing files, the main thread makes textures.
struct Image {
    GLuint tex = 0;
    int w = 0, h = 0;
    bool tried = false;
};
static std::map<std::string, Image> images; // by local file path
static struct {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<std::string, fs::path>> queue; // url, destination
    std::set<std::string> requested;
    std::atomic<int> fetched{0};
    std::vector<std::thread> th; // several workers: each fetch is mostly TLS/network wait
} imgQ;

// Steam-style unlock notifications, stacked in the bottom-right corner.
struct Popup {
    std::string title, badgeUrl;
    uint32_t points;
    Uint32 start;
};
static std::vector<Popup> popups;
static const Uint32 kPopupMs = 5000;

static void toast(const std::string& msg) {
    toastMsg = msg;
    toastUntil = SDL_GetTicks() + 3500;
}

static ImU32 rgb(uint32_t c, float mul = 1, int a = 255) {
    auto ch = [&](int s) { return (int)std::min(255.0f, ((c >> s) & 255) * mul); };
    return IM_COL32(ch(16), ch(8), ch(0), a);
}

static void resetKeys() {
    for (int i = 0; i < A_COUNT; ++i) cfg.keys[i] = kActions[i].def, cfg.pad[i] = kActions[i].padDef;
}

static void loadConfig() {
    resetKeys();
    std::ifstream f(dirs.config);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), val = line.substr(eq + 1);
        int v = std::atoi(val.c_str());
        if (k == "ff_speed") cfg.ffSpeed = std::clamp(v, 2, 10);
        if (k == "ach_unlocked_first") cfg.achUnlockedFirst = v != 0;
        if (k == "ra_user") cfg.raUser = val;
        if (k == "switch_emu") cfg.switchEmu = val;
        if (k == "switch_args") cfg.switchArgs = val;
        if (k == "ra_token") cfg.raToken = val;
        for (int i = 0; i < A_COUNT; ++i) {
            if (k == kActions[i].key && v >= 0 && v < SDL_NUM_SCANCODES) cfg.keys[i] = (SDL_Scancode)v;
            if (k == std::string("pad_") + kActions[i].key) cfg.pad[i] = padName(v).empty() ? -1 : v;
        }
    }
}

static void saveConfig() {
    std::string s = "ff_speed=" + std::to_string(cfg.ffSpeed) + "\n";
    s += "ach_unlocked_first=" + std::to_string(cfg.achUnlockedFirst) + "\n";
    s += "ra_user=" + cfg.raUser + "\nra_token=" + cfg.raToken + "\n";
    s += "switch_emu=" + cfg.switchEmu + "\nswitch_args=" + cfg.switchArgs + "\n";
    for (int i = 0; i < A_COUNT; ++i) s += std::string(kActions[i].key) + "=" + std::to_string(cfg.keys[i]) + "\n";
    for (int i = 0; i < A_COUNT; ++i) s += std::string("pad_") + kActions[i].key + "=" + std::to_string(cfg.pad[i]) + "\n";
    if (!writeFileAtomic(dirs.config, s.data(), s.size())) toast("Could not write config.ini");
}

static bool padHeld(int b) {
    if (b < 0) return false;
    for (SDL_GameController* c : pads) {
        if (b < kAxisBase) {
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) return true;
        } else {
            int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)((b - kAxisBase) / 2));
            if ((b - kAxisBase) % 2 ? v > kAxisThreshold : v < -kAxisThreshold) return true;
        }
    }
    return false;
}

// 3DS/Switch games are linked where they already are instead of copied: multi-GB copies hammer the disk.
// Each link is roms/<id>.link holding the file's path; this map mirrors them.
static std::map<std::string, fs::path> links;
static fs::path linkFile(const Game& g) { return dirs.roms / (std::string(g.id) + ".link"); }

static void loadLinks() {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dirs.roms, ec)) {
        std::vector<uint8_t> text;
        if (e.path().extension() == ".link" && readFile(e.path(), text))
            links[e.path().stem().u8string()] = fs::u8path(std::string(text.begin(), text.end()));
    }
}

static void setLink(const Game& g, const fs::path& target) {
    std::string s = target.u8string();
    if (writeFileAtomic(linkFile(g), s.data(), s.size())) links[g.id] = target;
}

static void clearLink(const Game& g) {
    std::error_code ec;
    fs::remove(linkFile(g), ec);
    links.erase(g.id);
}

static fs::path romPath(const Game& g) {
    if (auto it = links.find(g.id); it != links.end()) return it->second;
    fs::path xci = dirs.roms / (std::string(g.id) + ".xci"); // Switch games keep whichever container they came in
    return g.sys == Sys::NSW && fs::exists(xci) ? xci : dirs.roms / (std::string(g.id) + romExt(g.sys));
}

// Where newly added ROM bytes go: for Switch the extension follows the file's container format.
static fs::path romTarget(const Game& g, const std::vector<uint8_t>& head) {
    if (g.sys != Sys::NSW) return romPath(g);
    return dirs.roms / (std::string(g.id) + (romHeaderName(g.sys, head) == "XCI" ? ".xci" : ".nsp"));
}

// After adding a Switch game, drop the copy in the other container format so romPath sees the new one.
static void removeOtherContainer(const Game& g, const fs::path& kept) {
    std::error_code ec;
    for (const char* ext : {".xci", ".nsp"}) {
        fs::path p = dirs.roms / (std::string(g.id) + ext);
        if (g.sys == Sys::NSW && p != kept) fs::remove(p, ec);
    }
}
static fs::path sramPath(const Game& g) { return dirs.saves / (std::string(g.id) + ".srm"); }
static fs::path statePath(const Game& g, int s) { return dirs.states / (std::string(g.id) + ".state" + std::to_string(s)); }
static bool hasRom(const Game& g) { return g.sys != Sys::Unsupported && fs::exists(romPath(g)); }
static const char* shortTitle(const Game& g) { return strncmp(g.title, "Pokemon ", 8) ? g.title : g.title + 8; }

// Our own copies of a game's ROM in roms/ (never the user's linked original).
static void removeCopies(const Game& g, const fs::path& keep = {}) {
    std::error_code ec;
    for (const char* ext : {romExt(g.sys), ".xci", ".nsp"}) {
        fs::path p = dirs.roms / (std::string(g.id) + ext);
        if (*ext && (keep.empty() || !fs::equivalent(p, keep, ec))) fs::remove(p, ec);
    }
}

// "Remove ROM": drops a link (the original file stays put) or deletes our copy. Saves are kept.
static void removeRom(const Game& g) {
    if (links.count(g.id)) clearLink(g);
    else removeCopies(g);
}

static fs::path customListFile() { return dirs.roms / "custom.txt"; }

static void saveCustom() {
    std::string s;
    for (const CustomGame& c : customGames)
        if (!c.removed) s += c.id + "\t" + std::to_string((int)c.game.sys) + "\t" + c.title + "\n";
    writeFileAtomic(customListFile(), s.data(), s.size());
}

static CustomGame& newCustom(const std::string& id, Sys sys, const std::string& title) {
    static const uint32_t palette[] = {0x3B5BDB, 0xC2185B, 0x2E9E6A, 0xD9480F, 0x7B2CBF, 0x1F8A9E, 0xA0703F};
    CustomGame& c = customGames.emplace_back();
    c.id = id;
    c.title = title;
    c.platform = sysName(sys);
    c.game = {c.id.c_str(), c.title.c_str(), sys, c.platform.c_str(), kCustomGen, false, "", "",
              palette[std::hash<std::string>{}(id) % std::size(palette)]};
    return c;
}

static void loadCustom() {
    std::ifstream f(customListFile());
    std::string line;
    while (std::getline(f, line)) {
        size_t a = line.find('\t'), b = line.find('\t', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        newCustom(line.substr(0, a), (Sys)std::atoi(line.c_str() + a + 1), line.substr(b + 1));
    }
}

// ---------- images ----------

static void imageWorker() {
    for (;;) {
        std::pair<std::string, fs::path> job;
        {
            std::unique_lock<std::mutex> lock(imgQ.m);
            imgQ.cv.wait(lock, [] { return quitting || !imgQ.queue.empty(); });
            if (quitting) return;
            job = imgQ.queue.front();
            imgQ.queue.pop_front();
        }
        for (int attempt = 0; attempt < 2 && !quitting; ++attempt) {
            std::vector<uint8_t> data;
            HttpResult r = httpRequest(job.first, nullptr, "", [&](const char* d, size_t n) {
                data.insert(data.end(), d, d + n);
                return data.size() < (16u << 20);
            }, &quitting);
            int w, h, n;
            if (r.error.empty() && r.status == 200 && stbi_info_from_memory(data.data(), (int)data.size(), &w, &h, &n)
                && writeFileAtomic(job.second, data.data(), data.size())) {
                ++imgQ.fetched;
                break;
            }
        }
    }
}

// Texture for an image file; if the file is missing and url is set, it gets downloaded.
// silhouette: every pixel turned white, keeping only the shape (alpha).
static const Image* image(const fs::path& file, const std::string& url, bool silhouette = false) {
    Image& im = images[file.u8string() + (silhouette ? "#silhouette" : "")];
    if (!im.tried) {
        im.tried = true;
        std::vector<uint8_t> bytes;
        int w, h, n;
        stbi_uc* px = readFile(file, bytes) ? stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4) : nullptr;
        if (px) {
            if (silhouette)
                for (int i = 0; i < w * h; ++i) px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
            glGenTextures(1, &im.tex);
            glBindTexture(GL_TEXTURE_2D, im.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            gl::GenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            im.w = w;
            im.h = h;
            stbi_image_free(px);
        } else if (!url.empty()) {
            std::lock_guard<std::mutex> lock(imgQ.m);
            if (imgQ.requested.insert(url).second) {
                imgQ.queue.emplace_back(url, file);
                imgQ.cv.notify_one();
            }
        }
    }
    return im.tex ? &im : nullptr;
}

static fs::path badgePath(const std::string& url) { return dirs.badges / url.substr(url.find_last_of('/') + 1); }

// shortcut: Win32-only file dialog, swap for a portable one if this ever targets Linux/macOS.
static fs::path openFileDialog(const wchar_t* filter) {
    wchar_t buf[4096] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 4096;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(buf) : fs::path();
}

// ---------- games ----------

static void stopGame() {
    if (!running) return;
    if (!core.saveSram(sramPath(*running))) toast("Failed to write save file!");
    ra::detachCore();
    core.unload();
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    audioDev = 0;
    running = nullptr;
    if (view == V_GAME) view = V_LIBRARY;
}

static void startGame(const Game& g) {
    stopGame();
    std::string err;
    if (!core.load(dirs.cores / coreDll(g.sys), romPath(g), dirs.system, dirs.saves, err)) {
        toast(err);
        return;
    }
    core.loadSram(sramPath(g));
    running = &g;
    paused = ffToggle = false;
    view = V_GAME;
    lastSramSave = SDL_GetTicks();
    if (ra::user() && raConsole(g.sys)) {
        // mGBA publishes its memory map on the first frame; without it most achievement addresses look invalid.
        core.run();
        core.audio.clear();
        ra::attachCore(raConsole(g.sys));
        ra::loadGame(g.id, raConsole(g.sys), romPath(g).u8string(), true); // reload: validate against real memory
    }

    SDL_AudioSpec want{}, have{};
    want.freq = (int)std::lround(core.sampleRate);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
    else toast(std::string("No audio: ") + SDL_GetError());
}

// Rejects files that can't be a ROM; returns an error or "".
static std::string checkRom(const Game& g, const std::vector<uint8_t>& head, uint64_t size) {
    if (head.size() >= 2 && head[0] == 'P' && head[1] == 'K')
        return "That is a .zip archive. Extract it and add the ROM inside."; // shortcut: no unzip step
    size_t first = 0;
    while (first < head.size() && first < 64 && std::isspace(head[first])) ++first;
    if (first < head.size() && head[first] == '<') return "That is a web page, not a ROM file. Use a direct download link.";
    if (size < 0x8000) return "Not a valid ROM file"; // smallest GB cartridge is 32 KiB
    if (size > maxRomSize(g.sys)) return "File is larger than any supported game";
    if (g.sys == Sys::NSW && romHeaderName(g.sys, head).empty()) return "Not a Switch game file (.xci or .nsp)";
    return "";
}

// ---------- Switch: external emulator, its window embedded in ours ----------

// No embeddable Switch emulator exists, so the user's emulator runs as its own process and its main
// window is re-parented into Capsule's game area.
static struct {
    HANDLE proc = nullptr;
    DWORD pid = 0;
    HWND hwnd = nullptr;     // embedded emulator window
    const Game* game = nullptr;
    RECT placed{};           // last position given to hwnd
    bool shown = false;
    Uint32 nextScan = 0;
} ext;
static HWND mainHwnd = nullptr;

static bool extRunning() { return ext.proc != nullptr; }

// ---------- fullscreen ----------

static WINDOWPLACEMENT windowedPlacement{sizeof(WINDOWPLACEMENT)};
static bool showChrome = true; // top bar + game toolbar; hidden while a game is fullscreen

// Borderless fullscreen done by hand instead of SDL_SetWindowFullscreen, see the height below.
static void setFullscreen(bool on) {
    if (on == fullscreen || !mainHwnd) return;
    fullscreen = on;
    LONG_PTR style = GetWindowLongPtrW(mainHwnd, GWL_STYLE);
    if (on) {
        GetWindowPlacement(mainHwnd, &windowedPlacement);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(mainHwnd, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& m = mi.rcMonitor;
        SetWindowLongPtrW(mainHwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP);
        // One pixel taller than the monitor: GPU drivers switch a GL window that covers the screen exactly to
        // exclusive presentation, and Windows then stops drawing the embedded Switch emulator over it.
        SetWindowPos(mainHwnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top + 1, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        if (view == V_GAME) toast("Fullscreen: move the mouse to the top edge for controls");
    } else {
        SetWindowLongPtrW(mainHwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(mainHwnd, &windowedPlacement);
        SetWindowPos(mainHwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

// Per frame: in a fullscreen game the bars stay hidden until the mouse touches the top edge.
static void updateChrome(bool inGame) {
    static bool reveal = false;
    POINT c;
    GetCursorPos(&c); // polled: over an embedded emulator window the mouse never reaches ImGui
    ScreenToClient(mainHwnd, &c);
    if (c.y <= 2) reveal = true;
    else if (c.y > 140) reveal = false;
    showChrome = !(fullscreen && inGame) || reveal;
}

// While the embedded emulator has the keyboard, Capsule never sees key presses, so the fullscreen
// key (if it's F1-F12) becomes a hotkey, registered only while Capsule is the active window.
static void updateFullscreenHotkey() {
    static int registered = -1;
    int sc = cfg.keys[A_FULLSCREEN];
    HWND fg = GetForegroundWindow(); // reports the emulator's window itself while it has the keyboard
    bool active = fg == mainHwnd || (fg && (fg == ext.hwnd || GetAncestor(fg, GA_ROOT) == mainHwnd));
    bool want = ext.hwnd && active && sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12;
    if (registered >= 0 && (!want || registered != sc)) {
        UnregisterHotKey(mainHwnd, 1);
        registered = -1;
    }
    if (want && registered < 0 && RegisterHotKey(mainHwnd, 1, MOD_NOREPEAT, VK_F1 + (sc - SDL_SCANCODE_F1))) registered = sc;
}

static bool fullscreenButton() {
    bool clicked = ImGui::Button(fullscreen ? "Exit fullscreen" : "Fullscreen");
    if (clicked) setFullscreen(!fullscreen);
    return clicked;
}

// Largest visible top-level window of the emulator process; small ones (dialogs) stay floating.
static HWND findEmulatorWindow() {
    struct Search {
        DWORD pid;
        HWND best = nullptr;
        long area = 400 * 300;
    } s{ext.pid};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* s = (Search*)lp;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        RECT r;
        if (pid != s->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) || !GetWindowRect(h, &r)) return TRUE;
        long area = (long)(r.right - r.left) * (r.bottom - r.top);
        if (area >= s->area) s->best = h, s->area = area;
        return TRUE;
    }, (LPARAM)&s);
    return s.best;
}

static void embedWindow(HWND h) {
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    style &= ~(LONG_PTR)(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(h, GWL_STYLE, style | WS_CHILD);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~(LONG_PTR)(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    SetParent(h, mainHwnd);
    ext.hwnd = h;
    ext.placed = {};
    ext.shown = false;
}

// Give the window back to the desktop, e.g. when Capsule quits while the emulator keeps running.
static void releaseWindow() {
    if (!ext.hwnd || !IsWindow(ext.hwnd)) return;
    SetParent(ext.hwnd, nullptr);
    LONG_PTR style = GetWindowLongPtrW(ext.hwnd, GWL_STYLE);
    SetWindowLongPtrW(ext.hwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_CHILD) | WS_OVERLAPPEDWINDOW);
    SetWindowPos(ext.hwnd, nullptr, 100, 100, 1280, 760, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void launchExternal(const Game& g) {
    if (cfg.switchEmu.empty() || !fs::exists(fs::u8path(cfg.switchEmu))) {
        toast("Set your Switch emulator in Settings first");
        view = V_SETTINGS;
        return;
    }
    if (extRunning()) {
        toast("Close the running Switch game first");
        view = V_GAME;
        return;
    }
    stopGame();
    std::string args = cfg.switchArgs, rom = "\"" + romPath(g).u8string() + "\"";
    size_t at = args.find("{rom}");
    if (at == std::string::npos) args += " " + rom;
    else args.replace(at, 5, rom);
    std::wstring cmd = L"\"" + fs::u8path(cfg.switchEmu).wstring() + L"\" " + fs::u8path(args).wstring();
    std::wstring dir = fs::u8path(cfg.switchEmu).parent_path().wstring();
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        toast("Could not start the Switch emulator (error " + std::to_string(GetLastError()) + ")");
        return;
    }
    CloseHandle(pi.hThread);
    // Let the emulator have the PC: Capsule steps down in priority (and frame rate, see the main loop).
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    ext = {};
    ext.proc = pi.hProcess;
    ext.pid = pi.dwProcessId;
    ext.game = &g;
    view = V_GAME;
}

// Per frame: notice the emulator exiting, embed its window once it appears, hide it outside the game view.
static void updateExternal() {
    if (!extRunning()) return;
    if (WaitForSingleObject(ext.proc, 0) == WAIT_OBJECT_0) {
        CloseHandle(ext.proc);
        ext = {};
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        if (view == V_GAME) view = V_LIBRARY;
        return;
    }
    if (ext.hwnd && !IsWindow(ext.hwnd)) ext.hwnd = nullptr;
    if (SDL_GetTicks() >= ext.nextScan) { // emulators may open the game in a new window after a splash/library one
        ext.nextScan = SDL_GetTicks() + 500;
        if (HWND h = findEmulatorWindow(); h && h != ext.hwnd) embedWindow(h);
    }
    if (ext.hwnd && view != V_GAME && ext.shown) {
        ShowWindow(ext.hwnd, SW_HIDE);
        ext.shown = false;
    }
}

static void closeExternal() {
    if (ext.hwnd) PostMessageW(ext.hwnd, WM_CLOSE, 0, 0); // let the emulator save and ask its own questions
    else TerminateProcess(ext.proc, 0);                   // never showed a window: nothing to save
}

static void drawExternal() {
    if (showChrome) {
        if (ImGui::Button("Close game")) closeExternal();
        ImGui::SameLine();
        fullscreenButton();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s in %s. Click the game to give it the keyboard; its own controls, saves and speed apply.",
                            ext.game->title, fs::u8path(cfg.switchEmu).filename().u8string().c_str());
    }
    ImVec2 p = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    if (!ext.hwnd) {
        ImGui::TextUnformatted("Starting the emulator...");
        return;
    }
    RECT r{(LONG)p.x, (LONG)p.y, (LONG)(p.x + avail.x), (LONG)(p.y + avail.y)};
    if (memcmp(&r, &ext.placed, sizeof r) != 0 || !ext.shown) {
        SetWindowPos(ext.hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        ext.placed = r;
        ext.shown = true;
    }
}

static void reportAdded(const Game& g, const std::vector<uint8_t>&) { toast(std::string("Added ") + g.title); }

static fs::path partPath(const Game& g) {
    fs::path p = romPath(g);
    p += ".part";
    return p;
}

// Copies a ROM on the transfer thread (multi-GB 3DS/Switch files would freeze the UI).
static void copyWorker(fs::path src, fs::path part) {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN); // low CPU and disk priority: stay out of the way
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    std::vector<char> buf(4 << 20);
    while (!download.cancel && in) {
        in.read(buf.data(), buf.size());
        size_t n = (size_t)in.gcount();
        if (!n) break;
        if (download.head.size() < kRomHeadSize)
            download.head.insert(download.head.end(), buf.data(), buf.data() + std::min(n, kRomHeadSize - download.head.size()));
        if (!out.write(buf.data(), n)) {
            download.error = "Could not write to " + part.u8string() + " (disk full?)";
            break;
        }
        download.got += n;
    }
    out.close();
    if (download.cancel) download.error = "Copy cancelled";
    else if (in.bad()) download.error = "Could not read " + src.u8string();
    else if (!out && download.error.empty()) download.error = "Could not write to " + part.u8string();
    download.state = 2;
}

static bool beginTransfer(const Game& g, bool isCopy) {
    if (download.state != 0) {
        toast("Another download or copy is still running");
        return false;
    }
    download.game = &g;
    download.isCopy = isCopy;
    download.head.clear();
    download.error.clear();
    download.got = download.total = 0;
    download.cancel = false;
    download.state = 1;
    return true;
}

// Validates src and adds it to g: 3DS/Switch files are linked in place, smaller ROMs copied in the background.
// Returns false if the file was rejected.
static bool addRomFrom(const Game& g, const fs::path& src) {
    std::error_code ec;
    uint64_t size = fs::file_size(src, ec);
    std::vector<uint8_t> head(kRomHeadSize);
    std::ifstream f(src, std::ios::binary);
    head.resize(f.read((char*)head.data(), head.size()) ? head.size() : (size_t)f.gcount());
    if (ec || !f.eof() && !f) {
        toast("Could not read " + src.u8string());
        return false;
    }
    if (std::string bad = checkRom(g, head, size); !bad.empty()) {
        toast(bad);
        return false;
    }
    if (running == &g) stopGame();
    if (g.sys == Sys::N3DS || g.sys == Sys::NSW) {
        removeCopies(g, src); // an older copy in roms/ would only waste space now
        setLink(g, src);
        reportAdded(g, head);
        return true;
    }
    if (fs::space(dirs.roms, ec).available < size && !ec) {
        toast("Not enough free disk space to copy this game");
        return false;
    }
    if (!beginTransfer(g, true)) return false;
    download.total = size;
    download.th = std::thread(copyWorker, src, partPath(g));
    return true;
}

static void addRomFromFile(const Game& g) {
    const wchar_t* filter = g.sys == Sys::GBA ? L"GBA ROM (*.gba)\0*.gba\0All files\0*.*\0"
                          : g.sys == Sys::NDS ? L"NDS ROM (*.nds)\0*.nds\0All files\0*.*\0"
                          : g.sys == Sys::N3DS ? L"Decrypted 3DS ROM (*.3ds;*.cci;*.cxi)\0*.3ds;*.cci;*.cxi\0All files\0*.*\0"
                          : g.sys == Sys::NSW ? L"Switch game (*.xci;*.nsp)\0*.xci;*.nsp\0All files\0*.*\0"
                                               : L"Game Boy ROM (*.gb;*.gbc)\0*.gb;*.gbc\0All files\0*.*\0";
    fs::path src = openFileDialog(filter);
    if (!src.empty()) addRomFrom(g, src);
}

// Custom card: any supported ROM, named after its file.
static void addCustomRom() {
    fs::path src = openFileDialog(L"ROMs (*.gb;*.gbc;*.gba;*.nds;*.3ds;*.cci;*.cxi;*.xci;*.nsp)\0"
                                  L"*.gb;*.gbc;*.gba;*.nds;*.3ds;*.cci;*.cxi;*.xci;*.nsp\0");
    if (src.empty()) return;
    Sys sys = sysFromExt(src.extension().u8string());
    if (sys == Sys::Unsupported) {
        toast("Unsupported file type");
        return;
    }
    int n = 1;
    for (const CustomGame& c : customGames) n = std::max(n, std::atoi(c.id.c_str() + 6) + 1); // ids are "customN"
    CustomGame& c = newCustom("custom" + std::to_string(n), sys, src.stem().u8string());
    if (!addRomFrom(c.game, src)) {
        c.removed = true; // never shown or saved
        return;
    }
    refreshGames();
    saveCustom();
    selected = &c.game;
}

static void downloadWorker(std::string url, fs::path part, uint64_t maxSize) {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    HttpResult r = httpRequest(url, nullptr, "", [&](const char* d, size_t n) {
        if (download.head.size() < kRomHeadSize)
            download.head.insert(download.head.end(), d, d + std::min(n, kRomHeadSize - download.head.size()));
        download.got += n;
        return download.got <= maxSize && (bool)out.write(d, n);
    }, &download.cancel, &download.total);
    out.close();
    if (!r.error.empty()) download.error = download.got > maxSize ? "File is larger than any supported game" : r.error;
    else if (r.status != 200) download.error = "Server answered HTTP " + std::to_string(r.status);
    else if (!out) download.error = "Could not write to " + part.u8string();
    download.state = 2;
}

static void startDownload(const Game& g, const std::string& url) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        toast("Enter a link starting with http:// or https://");
        return;
    }
    if (!beginTransfer(g, false)) return;
    download.th = std::thread(downloadWorker, url, partPath(g), maxRomSize(g.sys));
}

static void pollDownload() {
    if (download.state != 2) return;
    download.th.join();
    const Game& g = *download.game;
    std::error_code ec;
    std::string bad = download.error.empty() ? checkRom(g, download.head, download.got) : download.error;
    fs::path dest = romTarget(g, download.head);
    if (bad.empty()) {
        if (running == &g) stopGame();
        fs::rename(partPath(g), dest, ec);
        if (ec) bad = "Could not save ROM into " + dirs.roms.u8string();
    }
    if (bad.empty()) {
        removeOtherContainer(g, dest);
        clearLink(g); // the new copy replaces any linked file
        reportAdded(g, download.head);
    }
    else {
        fs::remove(partPath(g), ec);
        toast(bad);
    }
    download.head = {};
    download.state = 0;
}

// ---------- In-app update: capsule.exe is swapped for the latest GitHub release's ----------

static const char* kVersion = "1.1.1"; // keep in sync with VERSION in installer/capsule.nsi
// The latest release must carry version.txt ("1.2.0") and capsule.exe as assets.
// shortcut: only capsule.exe is replaced, so a release that changes cores/*.dll still needs the installer.
static const std::string kReleaseUrl = "https://github.com/stefanmafijas/CapsuleEmulator/releases/latest/download/";
static struct {
    std::thread th;
    std::atomic<int> state{0}; // 0 none, 1 available, 2 downloading, 3 installed (restart), 4 failed
    std::string latest, error; // written by th before it changes state
    std::atomic<uint64_t> got{0}, total{0};
} update;
static bool restartAfterQuit = false;

static bool newerVersion(const std::string& v) {
    int a[3]{}, b[3]{};
    if (sscanf(v.c_str(), "%d.%d.%d", &a[0], &a[1], &a[2]) != 3) return false;
    sscanf(kVersion, "%d.%d.%d", &b[0], &b[1], &b[2]);
    return std::lexicographical_compare(b, b + 3, a, a + 3);
}

static fs::path exePath() {
    wchar_t buf[MAX_PATH];
    return fs::path(std::wstring(buf, GetModuleFileNameW(nullptr, buf, MAX_PATH)));
}

static void updateCheckWorker() {
    std::string body;
    HttpResult r = httpRequest(kReleaseUrl + "version.txt", nullptr, "", [&](const char* d, size_t n) {
        body.append(d, n);
        return body.size() < 64;
    }, &quitting);
    body.erase(body.find_last_not_of(" \r\n\t") + 1);
    if (r.error.empty() && r.status == 200 && newerVersion(body)) {
        update.latest = body;
        update.state = 1;
    }
}

static void updateWorker() {
    fs::path exe = exePath(), part = exe, old = exe;
    part += ".new";
    old += ".old";
    std::string error;
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        std::string head;
        HttpResult r = httpRequest(kReleaseUrl + "capsule.exe", nullptr, "", [&](const char* d, size_t n) {
            if (head.size() < 2) head.append(d, std::min<size_t>(n, 2));
            update.got += n;
            return (bool)out.write(d, n);
        }, &quitting, &update.total);
        out.close();
        if (!r.error.empty()) error = r.error;
        else if (r.status != 200) error = "Server answered HTTP " + std::to_string(r.status);
        else if (!out) error = "Could not write " + part.u8string();
        else if (head.rfind("MZ", 0) != 0 || update.got < 100000) error = "Downloaded file is not a program";
    }
    std::error_code ec;
    if (error.empty()) {
        // A running exe can't be overwritten but can be renamed; the .old copy is deleted on next start.
        fs::remove(old, ec);
        fs::rename(exe, old, ec);
        if (!ec) {
            fs::rename(part, exe, ec);
            if (ec) fs::rename(old, exe, ec), error = "Could not replace " + exe.u8string();
        } else error = "Could not replace " + exe.u8string();
    }
    if (!error.empty()) fs::remove(part, ec);
    update.error = error;
    update.state = error.empty() ? 3 : 4;
}

static void saveState() {
    toast(core.saveState(statePath(*running, slot)) ? "Saved state to slot " + std::to_string(slot)
                                                    : std::string("Save state failed"));
}

static void loadState() {
    if (!fs::exists(statePath(*running, slot))) {
        toast("Slot " + std::to_string(slot) + " is empty");
    } else if (core.loadState(statePath(*running, slot))) {
        ra::reset(); // achievement conditions restart from the loaded moment
        toast("Loaded state from slot " + std::to_string(slot));
    } else {
        toast("Load state failed");
    }
}

// sc: pressed key, or SDL_SCANCODE_UNKNOWN; pad: pressed gamepad binding, or -1.
static void hotkey(SDL_Scancode sc, int pad = -1) {
    auto is = [&](Action a) {
        return (sc != SDL_SCANCODE_UNKNOWN && cfg.keys[a] == sc) || (pad >= 0 && cfg.pad[a] == pad);
    };
    if (is(A_FULLSCREEN)) {
        setFullscreen(!fullscreen);
    }
    if (!running && !extRunning()) return;
    if (is(A_MENU)) {
        view = view == V_GAME ? V_LIBRARY : V_GAME;
        return;
    }
    if (view != V_GAME || !running) return; // the rest drive Capsule's own emulation
    if (is(A_FF_TOGGLE)) ffToggle = !ffToggle;
    if (is(A_PAUSE)) paused = !paused;
    if (is(A_SAVE_STATE)) saveState();
    if (is(A_LOAD_STATE)) loadState();
    if (is(A_SLOT_PREV) || is(A_SLOT_NEXT)) {
        slot = (slot + (is(A_SLOT_NEXT) ? 1 : kSlots - 1)) % kSlots;
        toast("State slot " + std::to_string(slot));
    }
}

// ---------- UI ----------

static const ImVec4 kAccent(0.94f, 0.27f, 0.27f, 1);

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = {24, 18};
    s.FramePadding = {14, 8};
    s.ItemSpacing = {12, 12};
    s.CellPadding = {8, 6};
    s.ScrollbarSize = 10;
    s.WindowRounding = 0;
    s.ChildRounding = 14;
    s.FrameRounding = 9;
    s.GrabRounding = 9;
    s.PopupRounding = 9;
    s.ScrollbarRounding = 9;
    s.WindowBorderSize = s.ChildBorderSize = s.FrameBorderSize = 0;
    s.SeparatorTextBorderSize = 1;
    ImVec4* c = s.Colors;
    auto hex = [](uint32_t v, float a = 1) { return ImVec4(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a); };
    c[ImGuiCol_WindowBg] = hex(0x0E1016);
    c[ImGuiCol_ChildBg] = hex(0x161922);
    c[ImGuiCol_PopupBg] = hex(0x1B1F2A);
    c[ImGuiCol_Text] = hex(0xE8EAF0);
    c[ImGuiCol_TextDisabled] = hex(0x8A90A2);
    c[ImGuiCol_FrameBg] = hex(0x222633);
    c[ImGuiCol_FrameBgHovered] = hex(0x2B3040);
    c[ImGuiCol_FrameBgActive] = hex(0x333949);
    c[ImGuiCol_Button] = hex(0x252A38);
    c[ImGuiCol_ButtonHovered] = hex(0x30364A);
    c[ImGuiCol_ButtonActive] = hex(0x3A4157);
    c[ImGuiCol_Header] = hex(0x252A38);
    c[ImGuiCol_HeaderHovered] = hex(0x30364A);
    c[ImGuiCol_HeaderActive] = hex(0x3A4157);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_Separator] = hex(0x2A2F3D);
    c[ImGuiCol_TableRowBg] = hex(0, 0);
    c[ImGuiCol_TableRowBgAlt] = hex(0xFFFFFF, 0.03f);
    c[ImGuiCol_ScrollbarBg] = hex(0, 0);
    c[ImGuiCol_ScrollbarGrab] = hex(0x2B3040);
}

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* reg = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* bold = "C:\\Windows\\Fonts\\segoeuib.ttf";
    if (!fs::exists(reg) || !fs::exists(bold)) return; // fall back to ImGui's built-in font
    io.Fonts->AddFontFromFileTTF(reg, 18);
    fontBold = io.Fonts->AddFontFromFileTTF(bold, 18);
    fontTitle = io.Fonts->AddFontFromFileTTF(bold, 26);
    fontHuge = io.Fonts->AddFontFromFileTTF(bold, 44);
}

static ImFont* font(ImFont* f) { return f ? f : ImGui::GetFont(); }
static ImTextureID texId(GLuint t) { return (ImTextureID)(intptr_t)t; }

static bool primaryButton(const char* label, ImVec2 size = {-1, 44}) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 0.36f, 0.36f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.2f, 1));
    ImGui::PushFont(font(fontBold));
    bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    return r;
}

static bool navButton(const char* label, bool active) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(1, 1, 1, 0.10f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.63f, 0.7f, 1));
    ImGui::PushFont(font(fontBold));
    bool r = ImGui::Button(label);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    return r;
}

static void pageTitle(const char* text) {
    ImGui::PushFont(font(fontTitle));
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// Small rounded label, e.g. platform or status.
static void pill(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 bg, ImU32 fg = IM_COL32(255, 255, 255, 235)) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    dl->AddRectFilled(pos, pos + sz + ImVec2(16, 6), bg, 99);
    dl->AddText(pos + ImVec2(8, 3), fg, text);
}

// Generated cover (no official artwork): gradient in the card's color, soft shapes placed from seed, big title.
// Callers clip to the card's rect.
static void gameArt(ImDrawList* dl, uint32_t color, const char* text, ImVec2 p, ImVec2 size, float bright, uint32_t seed = 0,
                    bool shapes = true) {
    // Rounded top cap, then a vertical gradient that starts at the cap's color so the seam is invisible.
    ImU32 top = rgb(color, 1.05f * bright), bottom = rgb(color, 0.5f * bright);
    dl->AddRectFilled(p, p + ImVec2(size.x, 30), top, 14, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor(p + ImVec2(0, 14), p + size, top, top, bottom, bottom);
    auto rnd = [&](int i) { return ((seed >> (i * 5)) & 31) / 31.0f; }; // 0..1 from 5 bits of the seed
    float w = size.x, h = size.y;
    if (shapes) {
    for (int i = 0; i < 3; ++i) { // faint diagonal bands
        float x = w * (0.3f + 0.14f * i + 0.12f * rnd(2));
        dl->AddLine(p + ImVec2(x, h), p + ImVec2(x + h * 0.7f, 0), IM_COL32(255, 255, 255, 12), h * 0.08f);
    }
    dl->AddCircleFilled(p + ImVec2(w * (0.7f + 0.25f * rnd(0)), h * (0.05f + 0.3f * rnd(1))), h * (0.42f + 0.18f * rnd(2)),
                        IM_COL32(255, 255, 255, 24), 64);
    dl->AddCircleFilled(p + ImVec2(w * (0.05f + 0.25f * rnd(3)), h * (0.9f + 0.15f * rnd(4))), h * 0.38f, IM_COL32(0, 0, 0, 34), 64);
    dl->AddCircle(p + ImVec2(w * (0.58f + 0.22f * rnd(5)), h * (0.45f + 0.1f * rnd(4))), h * (0.18f + 0.08f * rnd(1)),
                  IM_COL32(255, 255, 255, 48), 64, 3);
    }
    ImFont* f = font(fontTitle);
    float fs = w < 250 ? f->FontSize : f->FontSize * 1.3f;
    dl->AddText(f, fs, p + ImVec2(14, h - fs * 2.4f), IM_COL32(255, 255, 255, 255), text, nullptr, w - 28);
}

static uint32_t artSeed(const Game& g) { return (uint32_t)std::hash<std::string>{}(g.id); }

static void drawArt(ImDrawList* dl, const Game& g, ImVec2 p, ImVec2 size, float bright) {
    gameArt(dl, g.color, shortTitle(g), p, size, bright, artSeed(g));
}

static void gameTile(const Game& g, ImVec2 size) {
    ImGui::PushID(g.id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("tile", size)) selected = selected == &g ? nullptr : &g;
    bool hov = ImGui::IsItemHovered();
    bool unsupported = g.sys == Sys::Unsupported;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float artH = size.y - 88;

    dl->AddRectFilled(p, p + size, hov ? IM_COL32(36, 40, 54, 255) : IM_COL32(28, 31, 42, 255), 14);
    dl->PushClipRect(p, p + ImVec2(size.x, artH), true);
    drawArt(dl, g, p, ImVec2(size.x, artH), unsupported ? 0.45f : hov ? 1.1f : 1.0f);
    dl->PopClipRect();

    dl->PushClipRect(p, p + ImVec2(size.x - 8, size.y), true);
    dl->AddText(font(fontBold), font(fontBold)->FontSize, p + ImVec2(12, artH + 8), IM_COL32(235, 237, 243, 255), shortTitle(g));
    dl->AddText(p + ImVec2(12, artH + 32), IM_COL32(150, 156, 172, 255), g.platform);
    dl->PopClipRect();
    const char* status = unsupported ? "Not supported" : hasRom(g) ? "Ready" : "Not added";
    ImU32 dot = unsupported ? IM_COL32(120, 125, 140, 255) : hasRom(g) ? IM_COL32(74, 222, 128, 255) : IM_COL32(250, 204, 21, 255);
    dl->AddCircleFilled(p + ImVec2(17, artH + 65), 4, dot);
    dl->AddText(p + ImVec2(28, artH + 55), IM_COL32(150, 156, 172, 255), status);
    dl->AddText(p + ImVec2(size.x - 12 - ImGui::CalcTextSize(g.year).x, artH + 55), IM_COL32(150, 156, 172, 255), g.year);

    if (selected == &g) dl->AddRect(p, p + size, ImGui::GetColorU32(kAccent), 14, 0, 2.5f);
    if (running == &g) pill(dl, p + ImVec2(10, 10), "PLAYING", IM_COL32(0, 0, 0, 150));
    ImGui::PopID();
}

static void drawGenerationList() {
    pageTitle("Generations");
    ImGui::TextDisabled("A launcher for the Pokemon series, Gen I-IX. Unofficial fan project; see Donate for details.");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("gens");
    ImGui::PopStyleColor();

    float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    int cols = avail > 1100 ? 3 : avail > 700 ? 2 : 1;
    ImVec2 size((avail - gap * (cols - 1)) / cols, 150);
    for (int gen = 1; gen <= kCustomGen; ++gen) {
        const GenInfo& gi = kGens[gen];
        bool custom = gen == kCustomGen;
        int total = 0, ready = 0, first = 9999, last = 0;
        for (const Game* g : games)
            if (g->gen == gen) {
                total++, ready += hasRom(*g);
                if (g->year[0]) first = std::min(first, std::atoi(g->year)), last = std::max(last, std::atoi(g->year));
            }
        if ((gen - 1) % cols) ImGui::SameLine();
        ImGui::PushID(gen);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("gen", size)) openGen = gen, selected = nullptr;
        float b = ImGui::IsItemHovered() ? 1.12f : 1.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Art stops 15px short, then a rounded bottom cap in the gradient's end color.
        dl->PushClipRect(p, p + size, true);
        gameArt(dl, gi.color, "", p, size - ImVec2(0, 15), b, 0, false);
        dl->AddRectFilled(p + ImVec2(0, size.y - 16), p + size, rgb(gi.color, 0.5f * b), 16, ImDrawFlags_RoundCornersBottom);
        if (!custom) { // the generation's mascot as a faint white silhouette on the right
            std::string n = std::to_string(gi.mascot);
            if (const Image* m = image(dirs.art / ("gen_mascot_" + n + ".png"),
                    "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/other/official-artwork/" + n + ".png", true)) {
                float h = size.y * 1.05f, w = h * m->w / m->h;
                ImVec2 q = p + ImVec2(size.x - w - 8, (size.y - h) / 2);
                dl->AddCircleFilled(q + ImVec2(w, h) * 0.5f, h * 0.48f, IM_COL32(255, 255, 255, 16), 64); // soft halo
                dl->AddImage(texId(m->tex), q, q + ImVec2(w, h), {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 60));
            }
        } else { // cartridge outline with a plus
            ImVec2 c = p + ImVec2(size.x - 80, size.y * 0.5f);
            ImU32 col = IM_COL32(255, 255, 255, 60);
            dl->AddRect(c - ImVec2(36, 46), c + ImVec2(36, 46), col, 8, 0, 4);
            dl->AddRectFilled(c - ImVec2(22, 34), c + ImVec2(22, -4), col, 4);
            dl->AddLine(c + ImVec2(-12, 22), c + ImVec2(12, 22), col, 4);
            dl->AddLine(c + ImVec2(0, 10), c + ImVec2(0, 34), col, 4);
        }
        dl->PopClipRect();

        ImFont* huge = font(fontHuge);
        std::string big = custom ? std::string("Custom") : std::string("Gen ") + gi.numeral;
        dl->AddText(huge, huge->FontSize, p + ImVec2(24, 16), IM_COL32(255, 255, 255, 255), big.c_str());
        dl->AddText(font(fontTitle), font(fontTitle)->FontSize, p + ImVec2(26, 20 + huge->FontSize), IM_COL32(255, 255, 255, 220),
                    custom ? "Your own ROMs" : gi.region);
        std::string years = first == last ? std::to_string(first) : std::to_string(first) + " - " + std::to_string(last);
        std::string meta = (custom ? "" : years + "   |   ") + std::to_string(total) + (total == 1 ? " game" : " games");
        dl->AddText(p + ImVec2(26, size.y - 34), IM_COL32(255, 255, 255, 200), meta.c_str());
        if (ready) { // only worth showing once something is added
            std::string r = std::to_string(ready) + " ready";
            pill(dl, p + ImVec2(26 + ImGui::CalcTextSize(meta.c_str()).x + 12, size.y - 37), r.c_str(), IM_COL32(40, 170, 90, 220));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void drawPanel(const Game& g) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->PushClipRect(p, p + ImVec2(w, 230), true);
    drawArt(dl, g, p, ImVec2(w, 230), 1.0f);
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(p + ImVec2(w - 40, 10));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.35f));
    if (ImGui::Button("X", {30, 30})) selected = nullptr;
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(p + ImVec2(0, 244));

    ImGui::PushFont(font(fontTitle));
    ImGui::TextWrapped("%s", g.title);
    ImGui::PopFont();
    if (g.gen == kCustomGen) ImGui::TextDisabled("%s  |  Custom ROM", g.platform);
    else ImGui::TextDisabled("%s  |  %s  |  %s", g.platform, g.year, g.remake ? "Remake" : "Original");
    if (auto it = links.find(g.id); it != links.end()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Linked, not copied: %s", it->second.u8string().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    if (g.gen == kCustomGen) {
        CustomGame& c = *std::find_if(customGames.begin(), customGames.end(), [&](CustomGame& x) { return &x.game == &g; });
        bool busy = running == &g || ext.game == &g || (download.state != 0 && download.game == &g);
        static char nameBuf[256];
        static const Game* editing = nullptr;
        if (editing != &g) snprintf(nameBuf, sizeof nameBuf, "%s", c.title.c_str()), editing = &g;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##name", "Name", nameBuf, sizeof nameBuf, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
            if (nameBuf[0]) {
                c.title = nameBuf;
                c.game.title = c.title.c_str(); // the string may have reallocated
                saveCustom();
            }
        }
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Remove from Custom", {-1, 0})) {
            removeRom(g); // a linked original is left untouched
            c.removed = true;
            selected = nullptr;
            editing = nullptr;
            refreshGames();
            saveCustom();
            ImGui::EndDisabled();
            return;
        }
        ImGui::EndDisabled();
        ImGui::Dummy({0, 4});
    }

    if (g.sys == Sys::Unsupported) {
        ImGui::TextWrapped("There is no %s emulator core in Capsule, so this game can't be played here.", g.platform);
        return;
    }

    if (download.state != 0 && download.game == &g) { // copy/download in progress for this game
        uint64_t got = download.got, total = download.total;
        char label[64];
        snprintf(label, sizeof label, "%.1f / %s GB", got / 1073741824.0,
                 total ? std::to_string(total / 1073741824.0).substr(0, 4).c_str() : "?");
        ImGui::TextUnformatted(download.isCopy ? "Copying game file..." : "Downloading game file...");
        ImGui::ProgressBar(total ? (float)got / total : -1.0f * (float)ImGui::GetTime(), {-1, 0}, label);
        if (ImGui::Button(download.isCopy ? "Cancel copy" : "Cancel download", {-1, 0})) download.cancel = true;
        return;
    }
    if (g.sys == Sys::NSW && hasRom(g)) {
        bool here = ext.game == &g;
        if (primaryButton(here ? "Resume" : "Play")) {
            if (here) view = V_GAME;
            else launchExternal(g);
        }
        ImGui::TextWrapped("Runs inside Capsule through your Switch emulator (set in Settings), which handles saves, "
                           "controls and speed. RetroAchievements has no Switch support.");
        ImGui::Dummy({0, 6});
        ImGui::SeparatorText("Game file");
        if (ImGui::Button("Replace from file...", {-1, 0})) addRomFromFile(g);
        ImGui::BeginDisabled(here);
        if (ImGui::Button("Remove game file", {-1, 0})) removeRom(g);
        ImGui::EndDisabled();
        return;
    }
    if (hasRom(g)) {
        if (primaryButton(running == &g ? "Resume" : "Play")) {
            if (running == &g) view = V_GAME;
            else startGame(g);
        }
        int used = 0;
        for (int s = 0; s < kSlots; ++s) used += fs::exists(statePath(g, s));
        ImGui::TextDisabled("In-game save: %s   |   Save states: %d / %d",
                            fs::exists(sramPath(g)) ? "yes" : "none yet", used, kSlots);
        if (raConsole(g.sys) && ImGui::Button("Achievements", {-1, 0})) {
            achGame = &g;
            view = V_ACHIEVEMENTS;
        }
        ImGui::Dummy({0, 6});
        ImGui::SeparatorText("ROM");
        if (ImGui::Button("Replace from file...", {-1, 0})) addRomFromFile(g);
        ImGui::BeginDisabled(running == &g);
        if (ImGui::Button("Remove ROM", {-1, 0})) removeRom(g);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Removing the ROM keeps your saves.");
        return;
    }

    ImGui::TextWrapped("This game isn't added yet. Add a ROM dumped from your own %s.",
                       g.sys == Sys::N3DS ? "game card (decrypted .3ds/.cci/.cxi)"
                       : g.sys == Sys::NSW ? "game card or eShop purchase (.xci/.nsp)" : "cartridge");
    if (primaryButton("Add ROM from file...")) addRomFromFile(g);
    ImGui::Dummy({0, 6});
    ImGui::SeparatorText("Download from your own link");
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##url", "https://... (your backup on a cloud drive, NAS or server)", urlBuf, sizeof urlBuf);
        ImGui::BeginDisabled(download.state != 0 || !urlBuf[0]);
        if (ImGui::Button("Download", {-1, 0})) startDownload(g, urlBuf);
        ImGui::EndDisabled();
        if (download.state != 0) ImGui::TextDisabled("Another download or copy is running.");
        else ImGui::TextDisabled("Direct link to a %s file.", g.sys == Sys::N3DS ? ".3ds/.cci/.cxi"
                                 : g.sys == Sys::NSW ? ".xci/.nsp" : romExt(g.sys));
    }
}

static void drawGameGrid() {
    const GenInfo& gi = kGens[openGen];
    if (navButton("< Generations", false)) {
        openGen = 0;
        selected = nullptr;
        return;
    }
    ImGui::PushFont(font(fontTitle));
    if (openGen == kCustomGen) ImGui::TextUnformatted("Custom ROMs");
    else ImGui::Text("Generation %s  -  %s", gi.numeral, gi.region);
    ImGui::PopFont();

    // Side panel slides in when a game is selected.
    if (selected) panelGame = selected;
    float target = selected ? kPanelW : 0;
    panelW += (target - panelW) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14);
    if (std::fabs(target - panelW) < 0.5f) panelW = target;
    if (panelW == 0) panelGame = nullptr;

    float gap = ImGui::GetStyle().ItemSpacing.x;
    float gridW = ImGui::GetContentRegionAvail().x - (panelW > 0 ? panelW + gap : 0);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("grid", {gridW, 0});
    ImGui::PopStyleColor();
    const ImVec2 tile(200, 268);
    bool custom = openGen == kCustomGen;
    for (int remake = 0; remake < 2; ++remake) {
        bool any = std::any_of(games.begin(), games.end(), [&](const Game* g) { return g->gen == openGen && g->remake == (bool)remake; });
        if (!any && !(custom && !remake)) continue;
        if (!custom) {
            ImGui::PushFont(font(fontBold));
            ImGui::TextDisabled(remake ? "REMAKES" : "ORIGINALS");
            ImGui::PopFont();
        }
        float avail = ImGui::GetContentRegionAvail().x;
        int cols = std::max(1, (int)((avail + gap) / (tile.x + gap))), i = 0;
        for (const Game* g : games) {
            if (g->gen != openGen || g->remake != (bool)remake) continue;
            if (i++ % cols) ImGui::SameLine();
            gameTile(*g, tile);
        }
        if (custom) { // "add" tile at the end of the custom grid
            if (i++ % cols) ImGui::SameLine();
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("add", tile)) addCustomRom();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 col = ImGui::IsItemHovered() ? IM_COL32(200, 205, 220, 255) : IM_COL32(110, 116, 135, 255);
            dl->AddRect(p, p + tile, col, 14, 0, 2);
            ImVec2 c = p + ImVec2(tile.x / 2, tile.y / 2 - 16);
            dl->AddLine(c - ImVec2(18, 0), c + ImVec2(18, 0), col, 3);
            dl->AddLine(c - ImVec2(0, 18), c + ImVec2(0, 18), col, 3);
            const char* label = "Add custom ROM";
            dl->AddText(font(fontBold), font(fontBold)->FontSize, p + ImVec2((tile.x - ImGui::CalcTextSize(label).x) / 2, tile.y / 2 + 16), col, label);
            const char* hint = "GB, GBC, GBA, DS, 3DS, Switch";
            dl->AddText(p + ImVec2((tile.x - ImGui::CalcTextSize(hint).x) / 2, tile.y / 2 + 40), IM_COL32(110, 116, 135, 255), hint);
        }
        ImGui::Dummy({0, 8});
    }
    ImGui::EndChild();

    if (panelW > 0 && panelGame) {
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18, 18});
        ImGui::BeginChild("panel", {panelW, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
        if (panelW > kPanelW * 0.6f) drawPanel(*panelGame);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}

static void drawGame() {
    if (showChrome) {
        if (ImGui::Button("Close game")) {
            stopGame();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button(paused ? "Resume" : "Pause")) paused = !paused;
        ImGui::SameLine();
        std::string ffLabel = "Fast forward x" + std::to_string(cfg.ffSpeed);
        if (ffToggle) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        if (ImGui::Button(ffLabel.c_str())) ffToggle = !ffToggle;
        if (ffToggle) ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Save state")) saveState();
        ImGui::SameLine();
        if (ImGui::Button("Load state")) loadState();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::SliderInt("##slot", &slot, 0, kSlots - 1, "Slot %d");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(fs::exists(statePath(*running, slot)) ? "used" : "empty");
        ImGui::SameLine();
        fullscreenButton();
    }

    GLuint tex = core.hwRender ? core.fboTex : frameTex;
    if (!tex || !core.fw || !core.fh) {
        ImGui::TextUnformatted("Starting...");
        return;
    }
    // Software frames fill frameTex exactly; OpenGL cores draw into the corner of a larger FBO.
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (core.hwRender) {
        float u = (float)core.fw / core.fboW, v = (float)core.fh / core.fboH;
        uv0 = core.fboBottomLeft ? ImVec2(0, v) : ImVec2(0, 0);
        uv1 = core.fboBottomLeft ? ImVec2(u, 0) : ImVec2(u, v);
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float s = std::max(0.0f, std::min(avail.x / core.fw, avail.y / core.fh));
    ImVec2 size(core.fw * s, core.fh * s);
    ImVec2 cur = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cur.x + (avail.x - size.x) / 2, cur.y + (avail.y - size.y) / 2));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Image(texId(tex), size, uv0, uv1);

    // Mouse acts as the touch screen; libretro pointer range is -0x7fff..0x7fff over the whole frame.
    ImGuiIO& io = ImGui::GetIO();
    float nx = size.x > 0 ? (io.MousePos.x - p0.x) / size.x : -1;
    float ny = size.y > 0 ? (io.MousePos.y - p0.y) / size.y : -1;
    bool inside = nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1;
    core.ptrX = (int16_t)std::clamp((nx * 2 - 1) * 0x7fff, -32767.0f, 32767.0f);
    core.ptrY = (int16_t)std::clamp((ny * 2 - 1) * 0x7fff, -32767.0f, 32767.0f);
    core.ptrDown = inside && io.MouseDown[0] && ImGui::IsWindowHovered();

    if (paused) pill(ImGui::GetWindowDrawList(), p0 + ImVec2(12, 12), "PAUSED", IM_COL32(0, 0, 0, 170));
}

// One achievement row: badge (RetroAchievements serves a grayscale "_lock" badge when locked), title, description, points.
static void achievementRow(const rc_client_achievement_t* a) {
    bool unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
    ImGui::PushID((int)a->id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 76;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), unlocked ? IM_COL32(34, 38, 52, 255) : IM_COL32(24, 27, 36, 255), 12);
    const char* url = unlocked ? a->badge_url : a->badge_locked_url;
    const Image* badge = url && url[0] ? image(badgePath(url), url) : nullptr;
    ImVec2 bp = p + ImVec2(10, 10);
    if (badge) dl->AddImageRounded(texId(badge->tex), bp, bp + ImVec2(56, 56), {0, 0}, {1, 1},
                                   unlocked ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 200), 8);
    else dl->AddRectFilled(bp, bp + ImVec2(56, 56), IM_COL32(45, 50, 64, 255), 8);

    ImU32 titleCol = unlocked ? IM_COL32(250, 250, 252, 255) : IM_COL32(130, 135, 150, 255);
    ImU32 descCol = unlocked ? IM_COL32(170, 176, 192, 255) : IM_COL32(100, 105, 120, 255);
    std::string pts = std::to_string(a->points) + " pts";
    float ptsW = ImGui::CalcTextSize(pts.c_str()).x + 16;
    dl->PushClipRect(p, p + ImVec2(w - ptsW - 20, h), true);
    dl->AddText(font(fontBold), font(fontBold)->FontSize, p + ImVec2(78, 12), titleCol, a->title);
    dl->AddText(p + ImVec2(78, 38), descCol, a->description);
    dl->PopClipRect();
    pill(dl, p + ImVec2(w - ptsW - 12, 12), pts.c_str(), unlocked ? ImGui::GetColorU32(kAccent) : IM_COL32(45, 50, 64, 255),
         unlocked ? IM_COL32_WHITE : IM_COL32(130, 135, 150, 255));
    if (unlocked && a->unlock_time) {
        char date[32];
        std::strftime(date, sizeof date, "%Y-%m-%d", std::localtime(&a->unlock_time));
        dl->AddText(p + ImVec2(w - ImGui::CalcTextSize(date).x - 12, 44), descCol, date);
    }
    ImGui::Dummy({w, h});
    if (ImGui::IsItemHovered() && a->description[0]) ImGui::SetTooltip("%s", a->description);
    ImGui::PopID();
}

static void drawAchievementList() {
    rc_client_t* cl = ra::client();
    const rc_client_game_t* info = rc_client_get_game_info(cl);
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(cl, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                                                          RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    // Flattened, without the server's warning entry (which rc_client_get_user_game_summary would count).
    std::vector<const rc_client_achievement_t*> unlockedList, lockedList;
    uint32_t points = 0, pointsDone = 0;
    for (uint32_t b = 0; list && b < list->num_buckets; ++b)
        for (uint32_t i = 0; i < list->buckets[b].num_achievements; ++i) {
            const rc_client_achievement_t* a = list->buckets[b].achievements[i];
            if (ra::isClientWarning(a)) continue;
            bool unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
            (unlocked ? unlockedList : lockedList).push_back(a);
            points += a->points, pointsDone += unlocked ? a->points : 0;
        }
    // Most recent unlock first; locked ones keep the set's own order.
    std::stable_sort(unlockedList.begin(), unlockedList.end(), [](auto* x, auto* y) { return x->unlock_time > y->unlock_time; });
    size_t done = unlockedList.size(), total = done + lockedList.size();

    ImGui::PushFont(font(fontTitle));
    ImGui::TextUnformatted(info && info->title ? info->title : achGame->title);
    ImGui::PopFont();
    char label[96];
    snprintf(label, sizeof label, "%zu of %zu unlocked   |   %u of %u points", done, total, pointsDone, points);
    ImGui::ProgressBar(total ? (float)done / total : 0, {-1, 0}, label);
    if (running != achGame) ImGui::TextDisabled("Achievements unlock while you play this game.");
    if (ra::unrecognizedClient()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.3f, 1));
        ImGui::TextWrapped("RetroAchievements does not list Capsule as an approved emulator, so unlocks count as "
                           "softcore only (no hardcore points).");
        ImGui::PopStyleColor();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Sort");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    int sort = cfg.achUnlockedFirst ? 0 : 1;
    if (ImGui::Combo("##sort", &sort, "Unlocked first\0Locked first\0")) {
        cfg.achUnlockedFirst = sort == 0;
        saveConfig();
    }
    for (int section = 0; section < 2; ++section) {
        bool showUnlocked = (section == 0) == cfg.achUnlockedFirst;
        auto& items = showUnlocked ? unlockedList : lockedList;
        if (items.empty()) continue;
        std::string header = (showUnlocked ? "Unlocked (" : "Locked (") + std::to_string(items.size()) + ")";
        ImGui::SeparatorText(header.c_str());
        for (auto* a : items) achievementRow(a);
    }
    if (list) rc_client_destroy_achievement_list(list);
}

static void drawAchievements() {
    pageTitle("Achievements");
    const rc_client_user_t* user = ra::user();
    if (!user) {
        ImGui::BeginChild("login", {std::min(ImGui::GetContentRegionAvail().x, 460.0f), 0}, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
        ImGui::PushFont(font(fontBold));
        ImGui::TextUnformatted("Sign in to RetroAchievements");
        ImGui::PopFont();
        ImGui::TextWrapped("Earn and track achievements for Gen I-V games. Create a free account at retroachievements.org.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##user", "Username", raUserBuf, sizeof raUserBuf);
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputTextWithHint("##pass", "Password", raPassBuf, sizeof raPassBuf,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::BeginDisabled(ra::loggingIn() || !raUserBuf[0] || !raPassBuf[0]);
        if (primaryButton(ra::loggingIn() ? "Signing in..." : "Sign in") || (enter && !ra::loggingIn() && raUserBuf[0] && raPassBuf[0])) {
            ra::loginPassword(raUserBuf, raPassBuf);
            memset(raPassBuf, 0, sizeof raPassBuf); // the password is never kept; RetroAchievements returns a token
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Capsule saves a login token, not your password. Unlocks are softcore,\nso save states and fast forward stay allowed.");
        ImGui::EndChild();
        return;
    }

    ImGui::TextDisabled("Signed in as %s   |   %u softcore points", user->display_name, user->score_softcore);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 90);
    if (ImGui::Button("Sign out", {90, 0})) {
        ra::logout();
        return;
    }

    // Games that RetroAchievements can identify: added ROMs for GB/GBC/GBA/DS.
    std::vector<const Game*> list;
    for (const Game* g : games)
        if (raConsole(g->sys) && hasRom(*g)) list.push_back(g);
    if (list.empty()) {
        ImGui::TextWrapped("Add a Gen I-V ROM in the Library to see its achievements.");
        return;
    }
    if (running && raConsole(running->sys)) achGame = running;
    if (!achGame || std::find(list.begin(), list.end(), achGame) == list.end()) achGame = list[0];
    ra::loadGame(achGame->id, raConsole(achGame->sys), romPath(*achGame).u8string());

    ImGui::BeginChild("achgames", {300, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    for (const Game* g : list) {
        bool locked = running && running != g; // rc_client tracks one game; the running one wins
        ImGui::BeginDisabled(locked);
        ImGui::PushID(g->id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##g", achGame == g, 0, {0, 48})) achGame = g;
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Close the running game to browse others");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(p + ImVec2(2, 2), p + ImVec2(46, 46), rgb(g->color, 1.1f), rgb(g->color, 1.1f),
                                    rgb(g->color, 0.6f), rgb(g->color, 0.6f));
        char initial[2] = {shortTitle(*g)[0], 0};
        ImVec2 is = font(fontBold)->CalcTextSizeA(font(fontBold)->FontSize, FLT_MAX, 0, initial);
        dl->AddText(font(fontBold), font(fontBold)->FontSize, p + ImVec2(24 - is.x / 2, 24 - is.y / 2), IM_COL32_WHITE, initial);
        dl->AddText(font(fontBold), font(fontBold)->FontSize, p + ImVec2(56, 4), IM_COL32(235, 237, 243, 255), shortTitle(*g));
        dl->AddText(p + ImVec2(56, 26), IM_COL32(150, 156, 172, 255), running == g ? "Playing now" : g->platform);
        ImGui::PopID();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("achlist", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (ra::state()) {
    case ra::State::Loading:
    case ra::State::None:
        ImGui::TextDisabled("Loading achievements...");
        break;
    case ra::State::NoAchievements:
        ImGui::TextWrapped("RetroAchievements has no achievements for this ROM. Achievement sets match specific ROM versions; "
                           "check the game's page on retroachievements.org for the supported ones.");
        break;
    case ra::State::Error:
        ImGui::TextWrapped("Could not load achievements: %s", ra::error().c_str());
        if (ImGui::Button("Retry")) ra::loadGame(achGame->id, raConsole(achGame->sys), romPath(*achGame).u8string());
        break;
    case ra::State::Ready:
        drawAchievementList();
        break;
    }
    ImGui::EndChild();
}

static void drawSettings() {
    pageTitle("Settings");
    ImGui::BeginChild("settings", {std::min(ImGui::GetContentRegionAvail().x, 760.0f), 0}, ImGuiChildFlags_AlwaysUseWindowPadding);

    ImGui::SeparatorText("Keybinds");
    ImGui::TextDisabled("Click a binding, then press a key or gamepad button. Right-click a binding to clear it.");
    ImGui::TextDisabled(pads.empty() ? "No gamepad connected." : "Gamepad connected: %s", pads.empty() ? "" : SDL_GameControllerName(pads[0]));
    if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < A_COUNT; ++i) {
            if (i == A_UP || i == A_FF_HOLD) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushFont(font(fontBold));
                ImGui::TextDisabled(i == A_UP ? "CONTROLLER" : "HOTKEYS");
                ImGui::PopFont();
            }
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kActions[i].label);
            for (int dev = 0; dev < 2; ++dev) {
                ImGui::TableNextColumn();
                ImGui::PushID(dev);
                bool waiting = capture == i && capturePad == (bool)dev;
                std::string label = waiting ? (dev ? "Press a button... (Esc cancels)" : "Press a key...")
                                  : dev ? padName(cfg.pad[i]) : (cfg.keys[i] ? SDL_GetScancodeName(cfg.keys[i]) : "");
                if (label.empty()) label = "-";
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                if (ImGui::Button(label.c_str(), {-1, 0})) capture = i, capturePad = dev;
                if (waiting) ImGui::PopStyleColor();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (dev) cfg.pad[i] = -1;
                    else cfg.keys[i] = SDL_SCANCODE_UNKNOWN;
                    capture = -1;
                    saveConfig();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset keybinds to defaults")) {
        resetKeys();
        saveConfig();
    }

    ImGui::SeparatorText("Emulation");
    ImGui::SetNextItemWidth(240);
    if (ImGui::SliderInt("Fast forward speed", &cfg.ffSpeed, 2, 10, "x%d")) saveConfig();
    ImGui::TextDisabled("DS/3DS touch screen: click or drag on the bottom screen with the mouse.");
    ImGui::TextDisabled("3DS circle pad: left stick of a gamepad.");

    ImGui::SeparatorText("Switch emulator (Gen VIII-IX, Switch remakes)");
    ImGui::TextDisabled("Switch games open in a Switch emulator you install yourself, with your own console keys.");
    static char emuBuf[1024], argsBuf[512];
    static bool emuInit = false;
    if (!emuInit) {
        snprintf(emuBuf, sizeof emuBuf, "%s", cfg.switchEmu.c_str());
        snprintf(argsBuf, sizeof argsBuf, "%s", cfg.switchArgs.c_str());
        emuInit = true;
    }
    ImGui::SetNextItemWidth(-110);
    if (ImGui::InputTextWithHint("##emu", "Path to the emulator .exe", emuBuf, sizeof emuBuf)) {
        cfg.switchEmu = emuBuf;
        saveConfig();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...", {-1, 0})) {
        fs::path exe = openFileDialog(L"Programs (*.exe)\0*.exe\0");
        if (!exe.empty()) {
            cfg.switchEmu = exe.u8string();
            snprintf(emuBuf, sizeof emuBuf, "%s", cfg.switchEmu.c_str());
            saveConfig();
        }
    }
    ImGui::SetNextItemWidth(240);
    if (ImGui::InputText("Launch arguments", argsBuf, sizeof argsBuf)) {
        cfg.switchArgs = argsBuf;
        saveConfig();
    }
    ImGui::TextDisabled("{rom} is replaced with the game file. Some emulators want \"-g {rom}\" or \"-f -g {rom}\".");

    ImGui::SeparatorText("Folders");
    ImGui::TextDisabled("ROMs");
    ImGui::TextWrapped("%s", dirs.roms.u8string().c_str());
    ImGui::TextDisabled("Saves");
    ImGui::TextWrapped("%s", dirs.saves.u8string().c_str());
    ImGui::TextDisabled("Save states");
    ImGui::TextWrapped("%s", dirs.states.u8string().c_str());
    ImGui::EndChild();
}

static void drawPopups() {
    Uint32 now = SDL_GetTicks();
    popups.erase(std::remove_if(popups.begin(), popups.end(), [&](const Popup& p) { return now - p.start > kPopupMs; }), popups.end());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 size(340, 84);
    float y = vp->WorkPos.y + vp->WorkSize.y - 16;
    for (const Popup& p : popups) {
        float t = (now - p.start) / 1000.0f, remaining = (kPopupMs - (now - p.start)) / 1000.0f;
        float slide = std::min(1.0f, t / 0.25f);                      // rise in over 0.25 s
        float alpha = std::clamp(std::min(t / 0.25f, remaining / 0.4f), 0.0f, 1.0f); // fade out over the last 0.4 s
        ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - size.x - 16, y - size.y + (1 - slide) * 40);
        int a = (int)(alpha * 255);
        dl->AddRectFilled(pos, pos + size, IM_COL32(23, 26, 35, a * 245 / 255), 10);
        dl->AddRectFilled(pos, pos + ImVec2(4, size.y), (ImGui::GetColorU32(kAccent) & 0x00FFFFFF) | ((ImU32)a << 24), 10, ImDrawFlags_RoundCornersLeft);
        ImVec2 bp = pos + ImVec2(14, 12);
        if (const Image* badge = p.badgeUrl.empty() ? nullptr : image(badgePath(p.badgeUrl), p.badgeUrl))
            dl->AddImageRounded(texId(badge->tex), bp, bp + ImVec2(60, 60), {0, 0}, {1, 1}, IM_COL32(255, 255, 255, a), 6);
        else
            dl->AddRectFilled(bp, bp + ImVec2(60, 60), IM_COL32(45, 50, 64, a), 6);
        dl->AddText(pos + ImVec2(88, 14), IM_COL32(150, 156, 172, a), "Achievement Unlocked");
        dl->PushClipRect(pos, pos + size - ImVec2(10, 0), true);
        dl->AddText(font(fontBold), font(fontBold)->FontSize, pos + ImVec2(88, 34), IM_COL32(250, 250, 252, a), p.title.c_str());
        dl->PopClipRect();
        std::string pts = std::to_string(p.points) + " points";
        dl->AddText(pos + ImVec2(88, 56), IM_COL32(150, 156, 172, a), pts.c_str());
        y -= (size.y + 10) * slide;
    }
}

static const char* kDisclaimer =
    "Capsule is an unofficial fan project, not affiliated with or endorsed by Nintendo, Game Freak, Creatures or "
    "The Pokemon Company. Pokemon and game names are trademarks of their owners and are used only to identify "
    "compatible games. Capsule ships no games.";

static void drawDonate() {
    pageTitle("Support Capsule");
    ImGui::BeginChild("donate", {std::min(ImGui::GetContentRegionAvail().x, 520.0f), 0},
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::TextWrapped("Capsule is free. If you enjoy it, you can buy the developer a coffee through PayPal. "
                       "Donations are optional and don't unlock anything.");
    ImGui::Dummy({0, 6});
    // Only ever opens a PayPal https link, so a bad value can't launch anything else.
    bool valid = !strncmp(kDonateUrl, "https://paypal.me/", 18) || !strncmp(kDonateUrl, "https://www.paypal.com/", 23);
    if (!valid) {
        ImGui::TextDisabled("Donations aren't set up in this build yet.");
    } else {
        if (primaryButton("Donate with PayPal")) {
            int n = MultiByteToWideChar(CP_UTF8, 0, kDonateUrl, -1, nullptr, 0);
            std::wstring url(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, kDonateUrl, -1, url.data(), n);
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::TextDisabled("Opens %s in your browser.", kDonateUrl);
    }
    ImGui::Dummy({0, 10});
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", kDisclaimer);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// Logo: a capsule pill (red/white halves) and the name.
static void drawLogo() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos() + ImVec2(0, 7);
    const ImVec2 size(34, 18);
    float r = size.y / 2;
    dl->AddRectFilled(p, p + ImVec2(size.x / 2, size.y), ImGui::GetColorU32(kAccent), r, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(p + ImVec2(size.x / 2, 0), p + size, IM_COL32(240, 242, 248, 255), r, ImDrawFlags_RoundCornersRight);
    dl->AddRect(p, p + size, IM_COL32(14, 16, 22, 255), r, 0, 2);
    ImGui::Dummy({size.x + 8, 0});
    ImGui::SameLine(0, 0);
    ImGui::PushFont(font(fontTitle));
    ImGui::TextUnformatted("Capsule");
    ImGui::PopFont();
}

static void drawUpdate() {
    int st = update.state;
    if (st == 0) return;
    ImGui::SameLine();
    if (st == 1) {
        std::string label = "Update to " + update.latest;
        if (primaryButton(label.c_str(), {0, 0})) {
            update.th.join(); // the check that found it has finished
            update.state = 2;
            update.th = std::thread(updateWorker);
        }
    } else if (st == 2) {
        uint64_t got = update.got, total = update.total;
        ImGui::ProgressBar(total ? (float)got / total : -1.0f * (float)ImGui::GetTime(), {160, 0}, "Updating...");
    } else if (st == 4) {
        ImGui::TextDisabled("Update failed");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", update.error.c_str());
    }
}

static void drawTopBar() {
    drawLogo();
    ImGui::SameLine(0, 32);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    if (navButton("Library", view == V_LIBRARY)) view = V_LIBRARY;
    if (running || extRunning()) {
        ImGui::SameLine();
        std::string label = std::string("Playing: ") + shortTitle(running ? *running : *ext.game);
        if (navButton(label.c_str(), view == V_GAME)) view = V_GAME;
    }
    ImGui::SameLine();
    ImGui::SameLine();
    if (navButton("Achievements", view == V_ACHIEVEMENTS)) view = V_ACHIEVEMENTS;
    ImGui::SameLine();
    if (navButton("Settings", view == V_SETTINGS)) view = V_SETTINGS;
    ImGui::SameLine();
    if (navButton("Donate", view == V_DONATE)) view = V_DONATE;
    drawUpdate();
    ImGui::Separator();
}

static void pace(double seconds) {
    static Uint64 next = 0;
    Uint64 freq = SDL_GetPerformanceFrequency(), step = (Uint64)(freq * seconds), now = SDL_GetPerformanceCounter();
    if (now > next + step * 4) next = now; // fell behind: resync instead of racing to catch up
    next += step;
    while ((now = SDL_GetPerformanceCounter()) < next)
        if ((next - now) * 1000 / freq > 1) SDL_Delay(1);
}

int main(int, char**) {
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1"); // window/taskbar icon from installer/app.rc
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    char* bp = SDL_GetBasePath();
    dirs.base = fs::u8path(bp ? bp : "./");
    SDL_free(bp);
    dirs.roms = dirs.base / "roms";
    dirs.saves = dirs.base / "saves";
    dirs.states = dirs.base / "states";
    dirs.system = dirs.base / "system";
    dirs.cores = dirs.base / "cores";
    dirs.art = dirs.base / "art";
    dirs.badges = dirs.art / "badges";
    dirs.config = dirs.base / "config.ini";
    for (auto* d : {&dirs.roms, &dirs.saves, &dirs.states, &dirs.system, &dirs.badges}) fs::create_directories(*d);
    loadConfig();
    loadLinks();
    loadCustom();
    refreshGames();

    // OpenGL core profile: 4.3 for the 3DS core, 3.3 if the driver can't do that.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Capsule", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 820,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext glctx = nullptr;
    for (int version : {43, 33}) {
        if (!window || glctx) break;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, version / 10);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, version % 10);
        if ((glctx = SDL_GL_CreateContext(window))) core.glVersion = version;
    }
    if (!window || !glctx || !gl::load()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Capsule", "OpenGL 3.3 or newer is required.", nullptr);
        return 1;
    }
    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(0); // pacing is done by audio / timer
    SDL_SetWindowMinimumSize(window, 800, 560);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE); // WM_HOTKEY for the fullscreen key, see updateFullscreenHotkey()

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    applyStyle();
    loadFonts();
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window, &wm)) {
        mainHwnd = wm.info.win.window;
        // keep our GL drawing from painting over an embedded emulator window
        SetWindowLongPtrW(mainHwnd, GWL_STYLE, GetWindowLongPtrW(mainHwnd, GWL_STYLE) | WS_CLIPCHILDREN);
    }
    for (int i = 0; i < 6; ++i) imgQ.th.emplace_back(imageWorker);

    ra::init(&core, toast, [](const std::string& user, const std::string& token) {
        cfg.raUser = user;
        cfg.raToken = token;
        saveConfig();
    }, [](const rc_client_achievement_t& a) {
        popups.push_back({a.title, a.badge_url ? a.badge_url : "", a.points, SDL_GetTicks()});
    });
    if (!cfg.raUser.empty() && !cfg.raToken.empty()) ra::loginToken(cfg.raUser, cfg.raToken);

    { std::error_code ec; fs::remove(fs::path(exePath()) += ".old", ec); } // left by the last in-app update
    update.th = std::thread(updateCheckWorker);

    bool quit = false;
    int imgSeen = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (update.state == 3) quit = restartAfterQuit = true; // new exe is in place: restart into it
            if (e.type == SDL_SYSWMEVENT && e.syswm.msg->msg.win.msg == WM_HOTKEY) setFullscreen(!fullscreen);
            if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which)) pads.push_back(c);
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                SDL_GameController* c = SDL_GameControllerFromInstanceID(e.cdevice.which);
                pads.erase(std::remove(pads.begin(), pads.end(), c), pads.end());
                if (c) SDL_GameControllerClose(c);
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode sc = e.key.keysym.scancode;
                if (capture >= 0 && capturePad) {
                    if (sc == SDL_SCANCODE_ESCAPE) capture = -1;
                } else if (capture >= 0) {
                    for (auto& k : cfg.keys)
                        if (k == sc) k = SDL_SCANCODE_UNKNOWN;
                    cfg.keys[capture] = sc;
                    capture = -1;
                    saveConfig();
                } else if (!ImGui::GetIO().WantTextInput) {
                    hotkey(sc);
                }
            }
            // Gamepad press: a button, or a stick/trigger crossing the threshold.
            int padPress = -1;
            if (e.type == SDL_CONTROLLERBUTTONDOWN) padPress = e.cbutton.button;
            if (e.type == SDL_CONTROLLERAXISMOTION) {
                static bool active[SDL_CONTROLLER_AXIS_MAX * 2] = {};
                for (int positive = 0; positive < 2; ++positive) {
                    int idx = e.caxis.axis * 2 + positive;
                    bool now = positive ? e.caxis.value > kAxisThreshold : e.caxis.value < -kAxisThreshold;
                    if (now && !active[idx]) padPress = kAxisBase + idx;
                    active[idx] = now;
                }
            }
            if (padPress >= 0) {
                if (capture >= 0 && capturePad) {
                    for (auto& p : cfg.pad)
                        if (p == padPress) p = -1;
                    cfg.pad[capture] = padPress;
                    capture = -1;
                    saveConfig();
                } else if (capture < 0) {
                    hotkey(SDL_SCANCODE_UNKNOWN, padPress);
                }
            }
        }
        pollDownload();
        updateExternal();
        updateFullscreenHotkey();
        ra::pump();
        if (imgQ.fetched != imgSeen) { // new images on disk: retry the ones that had none
            imgSeen = imgQ.fetched;
            for (auto& [path, im] : images)
                if (!im.tex) im.tried = false;
        }

        bool emulating = running && view == V_GAME && !paused;
        bool ff = false;
        if (emulating) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            auto held = [&](int a) { return (cfg.keys[a] != SDL_SCANCODE_UNKNOWN && ks[cfg.keys[a]]) || padHeld(cfg.pad[a]); };
            uint16_t buttons = 0;
            if (capture < 0) {
                for (int i = 0; i < A_COUNT; ++i)
                    if (kActions[i].retroId >= 0 && held(i)) buttons |= 1 << kActions[i].retroId;
                // Left stick works as a D-pad, except on 3DS where it is the circle pad.
                const int stick[4][2] = {{RETRO_DEVICE_ID_JOYPAD_LEFT, axisBind(SDL_CONTROLLER_AXIS_LEFTX, false)},
                                         {RETRO_DEVICE_ID_JOYPAD_RIGHT, axisBind(SDL_CONTROLLER_AXIS_LEFTX, true)},
                                         {RETRO_DEVICE_ID_JOYPAD_UP, axisBind(SDL_CONTROLLER_AXIS_LEFTY, false)},
                                         {RETRO_DEVICE_ID_JOYPAD_DOWN, axisBind(SDL_CONTROLLER_AXIS_LEFTY, true)}};
                if (running->sys != Sys::N3DS)
                    for (auto& s : stick)
                        if (padHeld(s[1])) buttons |= 1 << s[0];
            }
            core.buttons = buttons;
            for (int stick = 0; stick < 2; ++stick)
                for (int axis = 0; axis < 2; ++axis)
                    core.analog[stick][axis] = pads.empty() || capture >= 0 ? 0
                        : SDL_GameControllerGetAxis(pads[0], (SDL_GameControllerAxis)(stick * 2 + axis));
            ff = ffToggle || held(A_FF_HOLD);
            for (int i = 0, n = ff ? cfg.ffSpeed : 1; i < n; ++i) {
                core.run();
                ra::doFrame();
            }
            if (audioDev && !ff) SDL_QueueAudio(audioDev, core.audio.data(), (Uint32)(core.audio.size() * 2));
            core.audio.clear();

            if (SDL_GetTicks() - lastSramSave > 5000) {
                if (!core.saveSram(sramPath(*running))) toast("Failed to write save file!");
                lastSramSave = SDL_GetTicks();
            }
        } else {
            ra::idle();
        }

        if (core.frameDirty && !core.hwRender) {
            if (!frameTex) glGenTextures(1, &frameTex);
            glBindTexture(GL_TEXTURE_2D, frameTex);
            // RGB8 internal format drops the X byte of XRGB8888, so the frame is always opaque.
            if (frameTexW != core.fw || frameTexH != core.fh) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, core.fw, core.fh, 0, GL_BGRA, GL_UNSIGNED_BYTE, core.frame.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                frameTexW = core.fw;
                frameTexH = core.fh;
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, core.fw, core.fh, GL_BGRA, GL_UNSIGNED_BYTE, core.frame.data());
            }
        }
        core.frameDirty = false;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        bool inGame = view == V_GAME && (running || extRunning());
        updateChrome(inGame);
        if (!showChrome) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0}); // game fills the screen
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (!showChrome) ImGui::PopStyleVar();
        if (showChrome) drawTopBar();
        if (view == V_GAME && running) drawGame();
        else if (view == V_GAME && extRunning()) drawExternal();
        else if (view == V_ACHIEVEMENTS) drawAchievements();
        else if (view == V_SETTINGS) drawSettings();
        else if (view == V_DONATE) drawDonate();
        else if (openGen) drawGameGrid();
        else drawGenerationList();

        if (SDL_GetTicks() < toastUntil) {
            ImVec2 sz = ImGui::CalcTextSize(toastMsg.c_str());
            ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - sz.x) / 2, vp->WorkPos.y + vp->WorkSize.y - sz.y - 30);
            auto* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(pos - ImVec2(16, 10), pos + sz + ImVec2(16, 10), IM_COL32(30, 34, 46, 240), 12);
            dl->AddText(pos, IM_COL32(255, 255, 255, 255), toastMsg.c_str());
        }
        drawPopups();
        ImGui::End();

        ImGui::Render();
        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, dw, dh);
        glClearColor(14 / 255.f, 16 / 255.f, 22 / 255.f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        // Normal speed is paced by the audio queue (keeps sound smooth); fast forward and menus by a timer.
        if (emulating && !ff && audioDev) {
            Uint32 limit = (Uint32)(core.sampleRate / core.fps * 4 * 3); // ~3 frames of audio
            while (SDL_GetQueuedAudioSize(audioDev) > limit) SDL_Delay(1);
        } else {
            // While a Switch game runs Capsule only draws a toolbar, so 15 fps leaves the CPU/GPU to the emulator.
            pace(emulating ? 1.0 / core.fps : extRunning() ? 1.0 / 15 : 1.0 / 60);
        }
    }

    stopGame();
    releaseWindow(); // a running Switch emulator outlives Capsule as a normal window
    if (ext.proc) CloseHandle(ext.proc);
    ra::shutdown();
    quitting = true;
    imgQ.cv.notify_all();
    for (std::thread& t : imgQ.th) t.join();
    if (update.th.joinable()) update.th.join();
    if (download.state != 0) {
        download.cancel = true;
        download.th.join();
    }
    for (auto& [path, im] : images)
        if (im.tex) glDeleteTextures(1, &im.tex);
    if (frameTex) glDeleteTextures(1, &frameTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (restartAfterQuit) {
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(exePath().c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    }
    return 0;
}
