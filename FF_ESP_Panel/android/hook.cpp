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

// Player field offsets (from dump.cs)
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

// Check if address is in libil2cpp.so data section
bool is_in_libil2cpp(uintptr_t addr) {
    static uintptr_t lib_start = 0, lib_end = 0;
    if (!lib_start) {
        FILE* maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "libil2cpp.so")) {
                    uintptr_t s, e;
                    char perms[8];
                    sscanf(line, "%lx-%lx %s", &s, &e, perms);
                    if (!lib_start) lib_start = s;
                    lib_end = e;
                }
            }
            fclose(maps);
        }
    }
    return addr >= lib_start && addr < lib_end;
}

// Read a player's fields and add to list
bool try_read_player(void* obj, bool isLocal) {
    if (!obj) return false;

    // Validate player by checking teamId field
    int teamId = *(int*)((uintptr_t)obj + PLAYER_TEAMID);
    if (teamId < 0 || teamId > 99) return false;

    // Validate isDead is a bool (0 or 1)
    uint8_t dead = *(uint8_t*)((uintptr_t)obj + PLAYER_ISDEAD);
    if (dead > 1) return false;

    // Check if CachedTransform pointer is in valid memory
    void** transformPtr = (void**)((uintptr_t)obj + PLAYER_CACHED_TRANSFORM);
    if (!transformPtr || !*transformPtr) return false;
    uintptr_t tf = (uintptr_t)*transformPtr;
    if (tf < 0x10000 || (tf & 7) != 0) return false;

    // All checks passed - this is likely a Player object
    PlayerData pd = {};

    void** namePtr = (void**)((uintptr_t)obj + PLAYER_NICKNAME);
    read_unity_string(*namePtr, pd.name, sizeof(pd.name));

    pd.teamIndex = teamId;
    pd.isDead = (dead != 0);

    // Read position
    void* transform = *transformPtr;
    float* pos = (float*)((uintptr_t)transform + 0x38);
    pd.x = pos[0];
    pd.y = pos[1];
    pd.z = pos[2];
    pd.headX = pos[0];
    pd.headY = pos[1] + 1.8f;
    pd.headZ = pos[2];

    pd.uniqueID = *(uint32_t*)((uintptr_t)obj + ENTITY_UNIQUE_ID);
    pd.isLocal = isLocal;
    pd.curHP = 100;
    pd.maxHP = 100;

    pthread_mutex_lock(&g_data_mutex);
    // Deduplicate by address
    for (auto& existing : g_players) {
        if (existing.uniqueID == pd.uniqueID) {
            pthread_mutex_unlock(&g_data_mutex);
            return true;
        }
    }
    g_players.push_back(pd);
    pthread_mutex_unlock(&g_data_mutex);
    return true;
}

// ===================== Socket Server =====================
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
            uintptr_t dummy = 0;
            write(g_sock_fd, &dummy, sizeof(dummy));
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

// ===================== Memory Scanner =====================
void scan_for_players() {
    g_players.clear();

    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return;

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perms[8] = {}, path[256] = {};
        sscanf(line, "%lx-%lx %s %*lx %*s %*d %[^\n]", &start, &end, perms, path);

        // Only scan readable writable regions (heap, BSS, data)
        bool scan = (perms[0] == 'r' && perms[1] == 'w' &&
                     perms[2] != 'x');
        if (!scan) continue;

        // Skip guard pages and small regions
        if (end - start < 4096) continue;

        for (uintptr_t addr = start; addr + 64 < end; addr += 16) {
            // Potential klass pointer (first 8 bytes of object)
            uintptr_t klass = *(uintptr_t*)addr;
            if (!is_in_libil2cpp(klass)) continue;
            if (klass & 7) continue;  // must be 8-byte aligned

            void* obj = (void*)addr;
            try_read_player(obj, false);
        }
    }
    fclose(maps);
}

// ===================== Collector Thread =====================
void* collector_thread(void*) {
    for (int i = 0; i < 30; i++) {
        scan_for_players();
        if (!g_players.empty()) break;
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
