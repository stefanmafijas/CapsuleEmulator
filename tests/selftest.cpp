// Checks ROM header matching and drives the mGBA core with a tiny synthetic GB ROM.
// Run from the build dir: selftest.exe (needs cores/mgba_libretro.dll next to it).
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../src/core.h"
#include "../src/games.h"

namespace fs = std::filesystem;

static const Game& game(const char* id) {
    for (auto& g : kGames)
        if (!strcmp(g.id, id)) return g;
    assert(!"unknown game id");
    return kGames[0];
}

int main() {
    // Header matching.
    std::vector<uint8_t> gba(0x200, 0);
    memcpy(&gba[0xAC], "BPEE", 4);
    assert(romMatches(game("emerald"), gba));
    assert(!romMatches(game("ruby"), gba));
    std::vector<uint8_t> nds(0x200, 0);
    memcpy(&nds[0x0C], "IPKE", 4);
    assert(romMatches(game("heartgold"), nds) && !romMatches(game("soulsilver"), nds));
    // .3ds (NCSD): partition 0 at 0x20 media units = 0x4000, product code at +0x150.
    std::vector<uint8_t> n3ds(kRomHeadSize, 0);
    memcpy(&n3ds[0x100], "NCSD", 4);
    n3ds[0x120] = 0x20;
    memcpy(&n3ds[0x4150], "CTR-P-EKJA", 10);
    assert(romMatches(game("x"), n3ds) && !romMatches(game("y"), n3ds));
    // .cxi (NCCH): product code at 0x150.
    std::vector<uint8_t> cxi(0x200, 0);
    memcpy(&cxi[0x100], "NCCH", 4);
    memcpy(&cxi[0x150], "CTR-P-A2BA", 10);
    assert(romMatches(game("ultramoon"), cxi) && !romMatches(game("ultrasun"), cxi));
    assert(!romMatches(game("x"), std::vector<uint8_t>(0x200, 0))); // no magic, no match
    // Switch: only the container is recognised (XCI "HEAD" at 0x100, NSP "PFS0" at 0).
    std::vector<uint8_t> xci(0x200, 0), nsp(0x200, 0);
    memcpy(&xci[0x100], "HEAD", 4);
    memcpy(&nsp[0], "PFS0", 4);
    assert(romHeaderName(Sys::NSW, xci) == "XCI" && romHeaderName(Sys::NSW, nsp) == "NSP");
    assert(romMatches(game("scarlet"), xci) && romMatches(game("sword"), nsp));
    assert(!romMatches(game("scarlet"), std::vector<uint8_t>(0x200, 0)));

    // Synthetic MBC1+RAM+battery GB ROM that writes 0x42 to SRAM then loops.
    std::vector<uint8_t> rom(0x8000, 0);
    const uint8_t entry[] = {0x00, 0xC3, 0x50, 0x01};
    memcpy(&rom[0x100], entry, sizeof entry);
    memcpy(&rom[0x134], "POKEMON RED", 11);
    rom[0x147] = 0x03; // MBC1+RAM+BATTERY
    rom[0x149] = 0x02; // 8 KiB RAM
    const uint8_t logo[] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
                            0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
                            0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};
    memcpy(&rom[0x104], logo, sizeof logo); // cores check the boot logo to detect a GB ROM
    uint8_t sum = 0;
    for (int i = 0x134; i < 0x14D; ++i) sum = sum - rom[i] - 1;
    rom[0x14D] = sum;
    const uint8_t code[] = {0x3E, 0x0A, 0xEA, 0x00, 0x00, 0x3E, 0x42, 0xEA, 0x00, 0xA0, 0x18, 0xFE};
    memcpy(&rom[0x150], code, sizeof code);
    assert(romMatches(game("red"), rom) && !romMatches(game("blue"), rom));

    fs::path tmp = fs::temp_directory_path() / "capsule_selftest";
    fs::create_directories(tmp);
    fs::path romFile = tmp / "test.gb", srm = tmp / "test.srm", st = tmp / "test.state";
    fs::remove(srm);
    assert(writeFileAtomic(romFile, rom.data(), rom.size()));

    Core core;
    std::string err;
    if (!core.load("cores/mgba_libretro.dll", romFile, tmp, tmp, err)) {
        fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    for (int i = 0; i < 60; ++i) core.run();
    assert(core.fw == 160 && core.fh == 144 && core.frame.size() == 160 * 144);
    assert(!core.audio.empty());
    assert(core.fps > 59 && core.fps < 61);

    assert(core.saveState(st) && fs::file_size(st) > 0);
    assert(core.loadState(st));

    assert(core.saveSram(srm));
    std::vector<uint8_t> sram;
    assert(readFile(srm, sram) && sram.size() == 8192 && sram[0] == 0x42);
    core.unload();

    // Reload: SRAM must come back from the file.
    assert(core.load("cores/mgba_libretro.dll", romFile, tmp, tmp, err));
    core.loadSram(srm);
    assert(core.saveSram(srm)); // unchanged, no rewrite needed
    core.unload();

    puts("selftest ok");
    return 0;
}
