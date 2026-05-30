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


// ===================== Il2Cpp Structures =====================
struct String {
    uintptr_t klass;
    uintptr_t monitor;
    int32_t length;
    uint16_t chars[];
};

// Player struct offsets (from dump.cs)
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
uintptr_t g_libil2cpp_base = 0;

// ===================== Socket Server Thread =====================
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
    bind(server_fd, (struct sockaddr*)&addr,
         offsetof(struct sockaddr_un, sun_path) + 1 + strlen(sock_name));
    listen(server_fd, 1);

    while (true) {
        g_sock_fd = accept(server_fd, nullptr, nullptr);
        if (g_sock_fd < 0) continue;

        while (true) {
            pthread_mutex_lock(&g_data_mutex);
            int count = (int)g_players.size();
            uintptr_t localPlayerPtr = 0;
            write(g_sock_fd, &localPlayerPtr, sizeof(localPlayerPtr));
            write(g_sock_fd, &count, sizeof(count));
            for (int i = 0; i < count; i++) {
                write(g_sock_fd, &g_players[i], sizeof(PlayerData));
            }
            pthread_mutex_unlock(&g_data_mutex);
            usleep(50000);
        }
        close(g_sock_fd);
        g_sock_fd = -1;
    }
    return nullptr;
}

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

void collect_player_data(void* player, bool isLocal) {
    if (!player) return;
    PlayerData pd = {};

    void** namePtr = (void**)((uintptr_t)player + PLAYER_NICKNAME);
    read_unity_string(*namePtr, pd.name, sizeof(pd.name));

    pd.teamIndex = *(int*)((uintptr_t)player + PLAYER_TEAMID);
    pd.isDead = *(bool*)((uintptr_t)player + PLAYER_ISDEAD);

    void** transformPtr = (void**)((uintptr_t)player + PLAYER_CACHED_TRANSFORM);
    if (transformPtr && *transformPtr) {
        void* transform = *transformPtr;
        float* pos = (float*)((uintptr_t)transform + 0x38);
        pd.x = pos[0];
        pd.y = pos[1];
        pd.z = pos[2];
        pd.headX = pos[0];
        pd.headY = pos[1] + 1.8f;
        pd.headZ = pos[2];
    }

    pd.uniqueID = *(uint32_t*)((uintptr_t)player + ENTITY_UNIQUE_ID);
    pd.isLocal = isLocal;
    pd.curHP = 100;
    pd.maxHP = 100;

    pthread_mutex_lock(&g_data_mutex);
    g_players.push_back(pd);
    pthread_mutex_unlock(&g_data_mutex);
}

uintptr_t get_libil2cpp_base() {
    if (g_libil2cpp_base) return g_libil2cpp_base;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so")) {
            g_libil2cpp_base = strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(maps);
    return g_libil2cpp_base;
}

// ===================== Call Il2Cpp Function =====================
// Call GameFacade.CurrentLocalPlayer (static, no args)
typedef void* (*getLocalPlayer_t)(const void*);
getLocalPlayer_t g_getLocalPlayer = nullptr;

void* call_CurrentLocalPlayer() {
    if (!g_getLocalPlayer) return nullptr;
    return g_getLocalPlayer(nullptr);
}

// ===================== VTable Scanner =====================
void scan_for_players(uintptr_t playerVtable) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perms[8] = {}, path[256] = {};
        sscanf(line, "%lx-%lx %s %*lx %*s %*d %[^\n]", &start, &end, perms, path);

        // Only scan anonymous heap regions (rw-p with no path, or [heap])
        bool isHeap = (perms[0] == 'r' && perms[1] == 'w' &&
                      (strstr(path, "[heap]") || path[0] == '\0'));
        if (!isHeap) continue;

        for (uintptr_t addr = start; addr + 16 <= end; addr += 16) {
            uintptr_t val = *(volatile uintptr_t*)addr;
            if (val == playerVtable) {
                void* candidate = (void*)addr;
                int teamId = *(int*)((uintptr_t)candidate + PLAYER_TEAMID);
                if (teamId >= 0 && teamId < 100) {
                    collect_player_data(candidate, false);
                }
            }
        }
    }
    fclose(maps);
}

// ===================== Collector Thread =====================
void* collector_thread(void*) {
    sleep(5);

    uintptr_t base = get_libil2cpp_base();
    if (!base) return nullptr;

    g_getLocalPlayer = (getLocalPlayer_t)(base + 0x5F52F04);
    if (!g_getLocalPlayer) return nullptr;

    while (true) {
        g_players.clear();

        void* localPlayer = call_CurrentLocalPlayer();
        if (!localPlayer) {
            sleep(1);
            continue;
        }

        uintptr_t playerVtable = *(uintptr_t*)localPlayer;
        if (!playerVtable) {
            sleep(1);
            continue;
        }

        collect_player_data(localPlayer, true);
        scan_for_players(playerVtable);

        sleep(1);
    }
    return nullptr;
}

// ===================== Init =====================
__attribute__((constructor))
void init() {
    pthread_t tid;
    pthread_create(&tid, nullptr, socket_thread, nullptr);
    pthread_detach(tid);

    pthread_t tid2;
    pthread_create(&tid2, nullptr, collector_thread, nullptr);
    pthread_detach(tid2);
}
