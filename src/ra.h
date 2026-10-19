#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "rc_client.h"

struct Core;

// RetroAchievements through rcheevos' rc_client. Softcore only, so save states and fast forward stay allowed.
// Everything here runs on the main thread; HTTP requests run on worker threads and come back through pump().
namespace ra {

enum class State { None, Loading, Ready, NoAchievements, Error };

// notify: short message for the user. onLogin: called with (username, token), or ("", "") on sign-out/failure.
// onUnlock: an achievement triggered during play.
void init(Core* core, std::function<void(const std::string&)> notify,
          std::function<void(const std::string&, const std::string&)> onLogin,
          std::function<void(const rc_client_achievement_t&)> onUnlock);
void shutdown();
void pump(); // deliver finished HTTP responses; call every frame

void loginPassword(const std::string& user, const std::string& password);
void loginToken(const std::string& user, const std::string& token);
void logout();
bool loggingIn();
const rc_client_user_t* user(); // null when signed out

// Fetch the achievement set for a ROM (by hash). No-op if that game is already loaded, unless force.
// rc_client checks achievement addresses against memory at load, so the running game must be
// loaded after attachCore.
void loadGame(const std::string& gameId, uint32_t consoleId, const std::string& romPath, bool force = false);
const std::string& gameId();
State state();
const std::string& error();

// The server answered with its "Warning: Unknown Emulator" placeholder: unlocks from this client are softcore only.
bool unrecognizedClient();
bool isClientWarning(const rc_client_achievement_t* a);

// Running game: map the core's memory so achievements can trigger.
void attachCore(uint32_t consoleId);
void detachCore();
void doFrame(); // after every emulated frame
void idle();    // when not emulating
void reset();   // after loading a save state

rc_client_t* client();

} // namespace ra
