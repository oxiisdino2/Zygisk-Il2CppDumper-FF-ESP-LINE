#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <vector>
#include <string>

#define PLAYER_ISDEAD          0x50
#define PLAYER_TEAMID          0x29C
#define PLAYER_NICKNAME        0x2E8
#define PLAYER_CACHED_TRANSFORM 0x38
#define ENTITY_UNIQUE_ID       0x3C

int g_sock_fd = -1;
pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

struct PlayerData {
    float x, y, z;
    float headX, headY, headZ;
    char name[64];
    int teamIndex;
    int curHP, maxHP;
    int curAP;
    bool isDead;
    bool isLocal;
    uint32_t uniqueID;
};

std::vector<PlayerData> g_players;

struct String {
    uintptr_t klass;
    uintptr_t monitor;
    int32_t length;
    uint16_t chars[];
};

void read_unity_string(void* str_ptr, char* out, int max_len) {
    if (!str_ptr) { out[0] = 0; return; }
    String* s = (String*)str_ptr;
    int len = s->length;
    if (len > max_len - 1) len = max_len - 1;
    for (int i = 0; i < len; i++) {
        out[i] = (s->chars[i] < 128) ? (char)s->chars[i] : '?';
    }
    out[len] = 0;
}

void try_read_player(void* obj) {
    if (!obj) return;
    int teamId = *(int*)((uintptr_t)obj + PLAYER_TEAMID);
    if (teamId < 0 || teamId > 99) return;
    uint8_t dead = *(uint8_t*)((uintptr_t)obj + PLAYER_ISDEAD);
    if (dead > 1) return;
    void** transformPtr = (void**)((uintptr_t)obj + PLAYER_CACHED_TRANSFORM);
    if (!transformPtr || !*transformPtr) return;

    PlayerData pd = {};
    void** namePtr = (void**)((uintptr_t)obj + PLAYER_NICKNAME);
    read_unity_string(*namePtr, pd.name, sizeof(pd.name));
    pd.teamIndex = teamId;
    pd.isDead = (dead != 0);
    void* transform = *transformPtr;
    float* pos = (float*)((uintptr_t)transform + 0x38);
    pd.x = pos[0]; pd.y = pos[1]; pd.z = pos[2];
    pd.headX = pos[0]; pd.headY = pos[1] + 1.8f; pd.headZ = pos[2];
    pd.uniqueID = *(uint32_t*)((uintptr_t)obj + ENTITY_UNIQUE_ID);
    pd.isLocal = false;
    pd.curHP = 100; pd.maxHP = 100;

    pthread_mutex_lock(&g_data_mutex);
    for (auto& existing : g_players) {
        if (existing.uniqueID == pd.uniqueID) { pthread_mutex_unlock(&g_data_mutex); return; }
    }
    g_players.push_back(pd);
    pthread_mutex_unlock(&g_data_mutex);
}

void* socket_thread(void*) {
    const char* sock_name = "ff_esp";
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return nullptr;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, sock_name, sizeof(addr.sun_path) - 2);
    unlink(sock_name);
    bind(server_fd, (struct sockaddr*)&addr, offsetof(struct sockaddr_un, sun_path) + 1 + strlen(sock_name));
    listen(server_fd, 1);
    while (true) {
        g_sock_fd = accept(server_fd, nullptr, nullptr);
        if (g_sock_fd < 0) continue;
        while (true) {
            pthread_mutex_lock(&g_data_mutex);
            int count = (int)g_players.size();
            uintptr_t dummy = 0;
            write(g_sock_fd, &dummy, sizeof(dummy));
            write(g_sock_fd, &count, sizeof(count));
            for (int i = 0; i < count; i++)
                write(g_sock_fd, &g_players[i], sizeof(PlayerData));
            pthread_mutex_unlock(&g_data_mutex);
            usleep(50000);
        }
        close(g_sock_fd); g_sock_fd = -1;
    }
    return nullptr;
}

// Il2Cpp API via dlsym
void* (*il2cpp_class_from_name)(const char*, const char*) = nullptr;
void* (*il2cpp_class_get_field_from_name)(void*, const char*) = nullptr;
void  (*il2cpp_field_static_get_value)(void*, void*) = nullptr;

void init_il2cpp_api() {
    il2cpp_class_from_name = (decltype(il2cpp_class_from_name))dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    il2cpp_class_get_field_from_name = (decltype(il2cpp_class_get_field_from_name))dlsym(RTLD_DEFAULT, "il2cpp_class_get_field_from_name");
    il2cpp_field_static_get_value = (decltype(il2cpp_field_static_get_value))dlsym(RTLD_DEFAULT, "il2cpp_field_static_get_value");
}

void* get_static_field_value(const char* ns, const char* cls, const char* field) {
    if (!il2cpp_class_from_name || !il2cpp_class_get_field_from_name || !il2cpp_field_static_get_value)
        return nullptr;
    void* klass = il2cpp_class_from_name(ns, cls);
    if (!klass) return nullptr;
    void* f = il2cpp_class_get_field_from_name(klass, field);
    if (!f) return nullptr;
    void* result = nullptr;
    il2cpp_field_static_get_value(f, &result);
    return result;
}

void scan_players_via_api() {
    g_players.clear();

    // Get BaseGame.sAllEntities dictionary
    void* dict = get_static_field_value("GCommon", "BaseGame", "sAllEntities");
    if (!dict) {
        // Fallback: try to get local player via GameFacade
        void* localPlayer = get_static_field_value("COW", "GameFacade", "LocalPlayerUserID");
        // LocalPlayerUserID is a string, not a Player pointer. Try scanning instead.
        return;
    }

    // dict is Dictionary<uint, Entity>*
    // Read its internal entries array and count
    // On 32-bit: header(8) + _buckets(4) + _entries(4) + _count(4) = entries at +12, count at +16
    // On 64-bit: header(16) + _buckets(8) + _entries(8) + _count(4) = entries at +24, count at +28
    
    uint32_t is32bit = (sizeof(void*) == 4);
    int entriesOff = is32bit ? 12 : 24;
    int countOff   = is32bit ? 16 : 28;

    void* entriesPtr = *(void**)((uintptr_t)dict + entriesOff);
    int count = *(int*)((uintptr_t)dict + countOff);
    if (!entriesPtr || count <= 0 || count > 200) return;

    // Entry struct: { int hashCode(4), int next(4), uint key(4), void* value(4/8) }
    int entrySize = is32bit ? 16 : 24;
    int valueOff = is32bit ? 12 : 16;

    for (int i = 0; i < count; i++) {
        uintptr_t entry = (uintptr_t)entriesPtr + (uintptr_t)(i * entrySize);
        void* entity = *(void**)(entry + valueOff);
        if (!entity) continue;
        try_read_player(entity);
    }
}

void* collector_thread(void*) {
    init_il2cpp_api();
    sleep(5);
    for (int i = 0; i < 300; i++) {
        scan_players_via_api();
        if (!g_players.empty()) { sleep(3); continue; }
        sleep(1);
    }
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t tid;
    pthread_create(&tid, nullptr, socket_thread, nullptr);
    pthread_detach(tid);
    pthread_t tid2;
    pthread_create(&tid2, nullptr, collector_thread, nullptr);
    pthread_detach(tid2);
}
