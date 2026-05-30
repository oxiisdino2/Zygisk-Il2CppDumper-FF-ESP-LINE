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
#include <android/log.h>
#include <vector>
#include <string>
#define LOG_TAG "FF_ESP_Hook"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

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
    LOGD("dlsym: il2cpp_class_from_name=%p", il2cpp_class_from_name);
    LOGD("dlsym: il2cpp_class_get_field_from_name=%p", il2cpp_class_get_field_from_name);
    LOGD("dlsym: il2cpp_field_static_get_value=%p", il2cpp_field_static_get_value);
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
    LOGD("scan_api: dict=0x%p", dict);
    if (!dict) {
        LOGD("scan_api: sAllEntities not found via API, trying fallback");
        void* localPlayer = get_static_field_value("COW", "GameFacade", "LocalPlayerUserID");
        LOGD("scan_api: LocalPlayerUserID=0x%p", localPlayer);
        return;
    }

    uint32_t is32bit = (sizeof(void*) == 4);
    int entriesOff = is32bit ? 12 : 24;
    int countOff   = is32bit ? 16 : 28;

    void* entriesPtr = *(void**)((uintptr_t)dict + entriesOff);
    int count = *(int*)((uintptr_t)dict + countOff);
    LOGD("scan_api: entriesPtr=0x%p count=%d", entriesPtr, count);
    if (!entriesPtr || count <= 0 || count > 200) return;

    int entrySize = is32bit ? 16 : 24;
    int valueOff = is32bit ? 12 : 16;

    int found = 0;
    for (int i = 0; i < count; i++) {
        uintptr_t entry = (uintptr_t)entriesPtr + (uintptr_t)(i * entrySize);
        void* entity = *(void**)(entry + valueOff);
        if (!entity) continue;
        try_read_player(entity);
        found++;
    }
    LOGD("scan_api: found %d players from %d entries", (int)g_players.size(), found);
}

uintptr_t get_libil2cpp_base() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) { LOGD("get_base: failed to open maps"); return 0; }
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so")) { base = strtoul(line, nullptr, 16); break; }
    }
    fclose(maps);
    LOGD("libil2cpp base: 0x%lx", base);
    return base;
}

// Find MethodInfo by scanning data section for pointer to function RVA
void* find_method_info() {
    uintptr_t base = get_libil2cpp_base();
    if (!base) { LOGD("find_method_info: no base"); return nullptr; }

    uintptr_t funcAddr = base + 0x5F52F04;
    LOGD("find_method_info: searching for funcAddr=0x%lx", funcAddr);
    void* result = nullptr;
    int scanned = 0;

    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) { LOGD("find_method_info: cannot open maps"); return nullptr; }
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perms[8] = {}, path[256] = {};
        sscanf(line, "%lx-%lx %s %*lx %*s %*d %[^\n]", &start, &end, perms, path);
        if (!strstr(path, "libil2cpp.so")) continue;
        if (perms[0]!='r' || perms[1]!='w') continue;

        for (uintptr_t addr = start; addr + 16 <= end; addr += 8) {
            scanned++;
            if (*(uintptr_t*)addr == funcAddr) {
                result = (void*)addr;
                LOGD("find_method_info: FOUND at 0x%lx (scanned %d)", addr, scanned);
                break;
            }
        }
        if (result) break;
    }
    fclose(maps);
    LOGD("find_method_info: result=%p scanned=%d", result, scanned);
    return result;
}

void* collector_thread(void*) {
    LOGD("collector_thread: started");
    init_il2cpp_api();
    LOGD("collector_thread: waiting 5s for Il2Cpp init");
    sleep(5);

    // Try Il2Cpp API first
    bool apiWorked = false;
    for (int i = 0; i < 10; i++) {
        LOGD("collector_thread: API scan attempt %d/10", i+1);
        scan_players_via_api();
        if (!g_players.empty()) {
            LOGD("collector_thread: API scan found %zu players!", g_players.size());
            apiWorked = true; break;
        }
        sleep(1);
    }

    // If API didn't work, try MethodInfo scanning + direct call
    if (!apiWorked) {
        LOGD("collector_thread: API failed, trying MethodInfo fallback");
        void* methodInfo = find_method_info();
        LOGD("collector_thread: MethodInfo=%p", methodInfo);
        if (methodInfo) {
            typedef void* (*func_t)(const void*);
            func_t fn = (func_t)(get_libil2cpp_base() + 0x5F52F04);
            LOGD("collector_thread: calling CurrentLocalPlayer with methodInfo");
            for (int i = 0; i < 300; i++) {
                g_players.clear();
                void* localPlayer = fn(methodInfo);
                if (localPlayer) {
                    LOGD("collector_thread: got localPlayer=0x%p", localPlayer);
                    try_read_player(localPlayer);
                    pthread_mutex_lock(&g_data_mutex);
                    for (auto& p : g_players) p.isLocal = true;
                    pthread_mutex_unlock(&g_data_mutex);
                }
                if (!g_players.empty()) { sleep(3); continue; }
                if (i < 10 || i % 50 == 0) LOGD("collector_thread: iteration %d, players=%zu", i, g_players.size());
                sleep(1);
            }
        } else {
            LOGD("collector_thread: MethodInfo not found, no fallback available");
        }
    }
    LOGD("collector_thread: exiting, total players=%zu", g_players.size());
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
