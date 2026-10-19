#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

enum class Sys { GB, GBC, GBA, NDS, N3DS, NSW, Unsupported }; // NSW: Switch, runs in an external emulator

struct Game {
    const char* id;       // file stem for roms/saves/states
    const char* title;
    Sys sys;
    const char* platform; // display name
    int gen;              // generation category it is listed under
    bool remake;
    const char* year;
    const char* match;    // GB: header title substring, GBA/NDS: 3-char game code, 3DS: product code part
    uint32_t color;       // 0xRRGGBB tile color
};

static const int kGenCount = 9;

// Remakes are listed under the generation of the game they remake.
inline const std::vector<Game> kGames = {
    {"red",        "Pokemon Red",              Sys::GB,  "Game Boy",       1, false, "1996", "RED", 0xE3350D},
    {"green",      "Pokemon Green (Japan)",    Sys::GB,  "Game Boy",       1, false, "1996", "GREEN", 0x3FA34D},
    {"blue",       "Pokemon Blue",             Sys::GB,  "Game Boy",       1, false, "1996", "BLUE", 0x3B5BDB},
    {"yellow",     "Pokemon Yellow",           Sys::GB,  "Game Boy",       1, false, "1998", "YELLOW", 0xF2B705},
    {"firered",    "Pokemon FireRed",          Sys::GBA, "Game Boy Advance", 1, true, "2004", "BPR", 0xF26B1D},
    {"leafgreen",  "Pokemon LeafGreen",        Sys::GBA, "Game Boy Advance", 1, true, "2004", "BPG", 0x6CBF3A},
    {"letsgopikachu", "Pokemon Let's Go, Pikachu!", Sys::NSW, "Nintendo Switch", 1, true, "2018", "", 0xE0B000},
    {"letsgoeevee",   "Pokemon Let's Go, Eevee!",   Sys::NSW, "Nintendo Switch", 1, true, "2018", "", 0xA0703F},

    {"gold",       "Pokemon Gold",             Sys::GBC, "Game Boy Color", 2, false, "1999", "GLD", 0xC9A227},
    {"silver",     "Pokemon Silver",           Sys::GBC, "Game Boy Color", 2, false, "1999", "SLV", 0x9AA5B1},
    {"crystal",    "Pokemon Crystal",          Sys::GBC, "Game Boy Color", 2, false, "2000", "CRYSTAL", 0x35B6D4},
    {"heartgold",  "Pokemon HeartGold",        Sys::NDS, "Nintendo DS",    2, true,  "2009", "IPK", 0xD9A21B},
    {"soulsilver", "Pokemon SoulSilver",       Sys::NDS, "Nintendo DS",    2, true,  "2009", "IPG", 0x7D8CA3},

    {"ruby",       "Pokemon Ruby",             Sys::GBA, "Game Boy Advance", 3, false, "2002", "AXV", 0xC2185B},
    {"sapphire",   "Pokemon Sapphire",         Sys::GBA, "Game Boy Advance", 3, false, "2002", "AXP", 0x1E4FB8},
    {"emerald",    "Pokemon Emerald",          Sys::GBA, "Game Boy Advance", 3, false, "2004", "BPE", 0x2E9E6A},
    {"omegaruby",  "Pokemon Omega Ruby",       Sys::N3DS, "Nintendo 3DS",  3, true,  "2014", "ECR", 0x8E1238},
    {"alphasapphire", "Pokemon Alpha Sapphire", Sys::N3DS, "Nintendo 3DS", 3, true,  "2014", "ECL", 0x143A8A},

    {"diamond",    "Pokemon Diamond",          Sys::NDS, "Nintendo DS",    4, false, "2006", "ADA", 0x5B8DEF},
    {"pearl",      "Pokemon Pearl",            Sys::NDS, "Nintendo DS",    4, false, "2006", "APA", 0xD98CA6},
    {"platinum",   "Pokemon Platinum",         Sys::NDS, "Nintendo DS",    4, false, "2008", "CPU", 0x7C7F8A},
    {"brilliantdiamond", "Pokemon Brilliant Diamond", Sys::NSW, "Nintendo Switch", 4, true, "2021", "", 0x3F6FD8},
    {"shiningpearl",     "Pokemon Shining Pearl",     Sys::NSW, "Nintendo Switch", 4, true, "2021", "", 0xC77396},

    {"black",      "Pokemon Black",            Sys::NDS, "Nintendo DS",    5, false, "2010", "IRB", 0x4A4A4A},
    {"white",      "Pokemon White",            Sys::NDS, "Nintendo DS",    5, false, "2010", "IRA", 0xB8B8B8},
    {"black2",     "Pokemon Black 2",          Sys::NDS, "Nintendo DS",    5, false, "2012", "IRE", 0x3A3F55},
    {"white2",     "Pokemon White 2",          Sys::NDS, "Nintendo DS",    5, false, "2012", "IRD", 0xA89F8A},

    {"x",          "Pokemon X",                Sys::N3DS, "Nintendo 3DS",  6, false, "2013", "EKJ", 0x1F5FBF},
    {"y",          "Pokemon Y",                Sys::N3DS, "Nintendo 3DS",  6, false, "2013", "EK2", 0xC0312B},

    {"sun",        "Pokemon Sun",              Sys::N3DS, "Nintendo 3DS",  7, false, "2016", "BND", 0xF59E0B},
    {"moon",       "Pokemon Moon",             Sys::N3DS, "Nintendo 3DS",  7, false, "2016", "BNE", 0x6D28D9},
    {"ultrasun",   "Pokemon Ultra Sun",        Sys::N3DS, "Nintendo 3DS",  7, false, "2017", "A2A", 0xEA580C},
    {"ultramoon",  "Pokemon Ultra Moon",       Sys::N3DS, "Nintendo 3DS",  7, false, "2017", "A2B", 0x7C3AED},

    {"sword",      "Pokemon Sword",            Sys::NSW, "Nintendo Switch", 8, false, "2019", "", 0x2F7FD6},
    {"shield",     "Pokemon Shield",           Sys::NSW, "Nintendo Switch", 8, false, "2019", "", 0xC2185B},
    {"legendsarceus", "Pokemon Legends: Arceus", Sys::NSW, "Nintendo Switch", 8, false, "2022", "", 0x8D7B4A},

    {"scarlet",    "Pokemon Scarlet",          Sys::NSW, "Nintendo Switch", 9, false, "2022", "", 0xD7261E},
    {"violet",     "Pokemon Violet",           Sys::NSW, "Nintendo Switch", 9, false, "2022", "", 0x6B2FA8},
    {"legendsza",  "Pokemon Legends: Z-A",     Sys::NSW, "Nintendo Switch", 9, false, "2025", "", 0x1E9E6A},
};

inline const char* romExt(Sys s) {
    switch (s) {
    case Sys::GB:   return ".gb";
    case Sys::GBC:  return ".gbc";
    case Sys::GBA:  return ".gba";
    case Sys::NDS:  return ".nds";
    case Sys::N3DS: return ".3ds";
    case Sys::NSW:  return ".nsp"; // or .xci, chosen from the file header
    default:        return "";
    }
}

inline const char* coreDll(Sys s) {
    switch (s) {
    case Sys::NDS:  return "melondsds_libretro.dll";
    case Sys::N3DS: return "azahar_libretro.dll";
    default:        return "mgba_libretro.dll";
    }
}

// Bytes of the file start that romHeaderName needs (3DS product code sits at 0x4150 in a .3ds).
static const size_t kRomHeadSize = 0x8000;

// Name stored in the ROM header: GB title, GBA/NDS 4-char game code, 3DS product code ("CTR-P-EKJA").
inline std::string romHeaderName(Sys s, const std::vector<uint8_t>& head) {
    size_t off = 0, len = 0;
    if (s == Sys::GB || s == Sys::GBC) off = 0x134, len = 16;
    else if (s == Sys::GBA) off = 0xAC, len = 4;
    else if (s == Sys::NDS) off = 0x0C, len = 4;
    else if (s == Sys::N3DS && head.size() >= 0x200) {
        auto magic = [&](const char* m) { return std::string(head.begin() + 0x100, head.begin() + 0x104) == m; };
        size_t ncch = 0; // .cxi: NCCH header at file start
        if (magic("NCSD")) // .3ds/.cci: partition 0 offset, in 0x200-byte media units
            ncch = (head[0x120] | head[0x121] << 8 | head[0x122] << 16 | (size_t)head[0x123] << 24) * 0x200;
        else if (!magic("NCCH")) return "";
        off = ncch + 0x150, len = 16;
    } else if (s == Sys::NSW) { // Switch titles are encrypted, so only the container format is checked
        if (head.size() >= 0x104 && std::string(head.begin() + 0x100, head.begin() + 0x104) == "HEAD") return "XCI";
        if (head.size() >= 4 && std::string(head.begin(), head.begin() + 4) == "PFS0") return "NSP";
        return "";
    }
    std::string out;
    for (size_t i = off; i < off + len && i < head.size() && head[i]; ++i)
        out += (char)std::toupper(head[i]);
    return out;
}

inline bool romMatches(const Game& g, const std::vector<uint8_t>& head) {
    std::string name = romHeaderName(g.sys, head);
    if (g.sys == Sys::GBA || g.sys == Sys::NDS) return name.compare(0, 3, g.match) == 0;
    return !name.empty() && name.find(g.match) != std::string::npos;
}
