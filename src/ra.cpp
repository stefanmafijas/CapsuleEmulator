#include "ra.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "core.h"
#include "net.h"
#include "rc_libretro.h"

namespace ra {

// Finished HTTP calls waiting for the main thread. Heap-allocated and never freed, so detached
// request threads still running at exit never touch a destroyed mutex.
struct Mailbox {
    struct Done {
        std::string body;
        int status;
        rc_client_server_callback_t cb;
        void* data;
    };
    std::mutex m;
    std::vector<Done> done;
    std::atomic<bool> quitting{false};
};
static Mailbox* box = new Mailbox;

static rc_client_t* cl = nullptr;
static Core* core = nullptr;
static rc_libretro_memory_regions_t regions;
static bool regionsInit = false;
static std::function<void(const std::string&)> notify;
static std::function<void(const std::string&, const std::string&)> onLogin;
static std::function<void(const rc_client_achievement_t&)> onUnlock;
static std::string curGame, err;
static State st = State::None;
static bool loginBusy = false, unrecognized = false;
static uintptr_t loadSeq = 0; // ignores callbacks from a load that was superseded

static uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t num, rc_client_t*) {
    if (regionsInit) return rc_libretro_memory_read(&regions, address, buffer, num);
    // No game running (browsing the list): report zeros instead of "unreadable", or rc_client would
    // permanently disable every achievement as having an invalid address.
    memset(buffer, 0, num);
    return num;
}

static void RC_CCONV coreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info) {
    info->data = core->memoryData(id, &info->size);
}

static void RC_CCONV serverCall(const rc_api_request_t* req, rc_client_server_callback_t cb, void* data, rc_client_t*) {
    std::string url = req->url, ct = req->content_type ? req->content_type : "application/x-www-form-urlencoded";
    bool isPost = req->post_data != nullptr;
    std::string post = isPost ? req->post_data : "";
    std::thread([=] {
        std::string body;
        HttpResult r = httpRequest(url, isPost ? &post : nullptr, ct, [&](const char* d, size_t n) {
            body.append(d, n);
            return body.size() < (8u << 20);
        }, &box->quitting);
        int status = r.error.empty() ? r.status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        std::lock_guard<std::mutex> lock(box->m);
        box->done.push_back({std::move(body), status, cb, data});
    }).detach();
}

static void RC_CCONV logMessage(const char* msg, const rc_client_t*) { fprintf(stderr, "[rcheevos] %s\n", msg); }

static void RC_CCONV onEvent(const rc_client_event_t* e, rc_client_t*) {
    switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        onUnlock(*e->achievement);
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        notify("Every achievement in this game earned!");
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        notify(std::string("RetroAchievements: ") + e->server_error->error_message);
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        notify("RetroAchievements offline; unlocks will be sent when it reconnects");
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        notify("RetroAchievements reconnected");
        break;
    }
}

static void RC_CCONV loginDone(int result, const char* msg, rc_client_t*, void* fromToken) {
    loginBusy = false;
    if (result == RC_OK) {
        const rc_client_user_t* u = rc_client_get_user_info(cl);
        onLogin(u->username, u->token);
        notify(std::string("Signed in to RetroAchievements as ") + u->display_name);
    } else {
        if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN) onLogin("", "");
        notify(std::string(fromToken ? "RetroAchievements sign-in expired: " : "Sign-in failed: ") + (msg ? msg : "unknown error"));
    }
}

bool isClientWarning(const rc_client_achievement_t* a) {
    return strncmp(a->title, "Warning: Unknown Emulator", 25) == 0;
}

static void RC_CCONV loadDone(int result, const char* msg, rc_client_t*, void* seq) {
    if ((uintptr_t)seq != loadSeq) return;
    if (result == RC_OK) {
        st = rc_client_has_achievements(cl) ? State::Ready : State::NoAchievements;
        unrecognized = false;
        rc_client_achievement_list_t* list = rc_client_create_achievement_list(cl, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
                                                                              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
        for (uint32_t b = 0; list && b < list->num_buckets; ++b)
            for (uint32_t i = 0; i < list->buckets[b].num_achievements; ++i)
                unrecognized |= isClientWarning(list->buckets[b].achievements[i]);
        if (list) rc_client_destroy_achievement_list(list);
    } else if (result == RC_NO_GAME_LOADED) { // hash not known to RetroAchievements
        st = State::NoAchievements;
    } else {
        st = State::Error;
        err = msg ? msg : "unknown error";
    }
}

void init(Core* c, std::function<void(const std::string&)> n, std::function<void(const std::string&, const std::string&)> l,
          std::function<void(const rc_client_achievement_t&)> u) {
    core = c;
    notify = std::move(n);
    onLogin = std::move(l);
    onUnlock = std::move(u);
    cl = rc_client_create(readMemory, serverCall);
    rc_client_enable_logging(cl, RC_CLIENT_LOG_LEVEL_WARN, logMessage);
    rc_client_set_event_handler(cl, onEvent);
    rc_client_set_hardcore_enabled(cl, 0);
    char clause[128];
    rc_client_get_user_agent_clause(cl, clause, sizeof clause);
    setUserAgent(std::string("Capsule/1.0 ") + clause);
}

void shutdown() {
    box->quitting = true;
    detachCore();
    if (cl) rc_client_destroy(cl);
    cl = nullptr;
}

void pump() {
    std::vector<Mailbox::Done> done;
    {
        std::lock_guard<std::mutex> lock(box->m);
        done.swap(box->done);
    }
    for (auto& d : done) {
        rc_api_server_response_t resp{d.body.c_str(), d.body.size(), d.status};
        d.cb(&resp, d.data);
    }
}

void loginPassword(const std::string& u, const std::string& p) {
    loginBusy = true;
    rc_client_begin_login_with_password(cl, u.c_str(), p.c_str(), loginDone, nullptr);
}

void loginToken(const std::string& u, const std::string& t) {
    loginBusy = true;
    rc_client_begin_login_with_token(cl, u.c_str(), t.c_str(), loginDone, (void*)1);
}

void logout() {
    rc_client_unload_game(cl);
    rc_client_logout(cl);
    curGame.clear();
    st = State::None;
    onLogin("", "");
}

bool loggingIn() { return loginBusy; }
const rc_client_user_t* user() { return cl ? rc_client_get_user_info(cl) : nullptr; }

void loadGame(const std::string& id, uint32_t consoleId, const std::string& romPath, bool force) {
    if (id == curGame && st != State::Error && !force) return;
    rc_client_unload_game(cl);
    curGame = id;
    st = State::Loading;
    err.clear();
    rc_client_begin_identify_and_load_game(cl, consoleId, romPath.c_str(), nullptr, 0, loadDone, (void*)++loadSeq);
}

const std::string& gameId() { return curGame; }
State state() { return st; }
bool unrecognizedClient() { return unrecognized; }
const std::string& error() { return err; }

void attachCore(uint32_t consoleId) {
    detachCore();
    regionsInit = rc_libretro_memory_init(&regions, core->memoryMap(), coreMemoryInfo, consoleId) != 0;
}

void detachCore() {
    if (regionsInit) rc_libretro_memory_destroy(&regions);
    regionsInit = false;
}

void doFrame() {
    if (regionsInit) rc_client_do_frame(cl);
    else rc_client_idle(cl);
}

void idle() { rc_client_idle(cl); }
void reset() { rc_client_reset(cl); }
rc_client_t* client() { return cl; }

} // namespace ra
