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

    // Validate: teamId must be 0-99
    int teamId = *(int*)((uintptr_t)obj + PLAYER_TEAMID);
    if (teamId < 0 || teamId > 99) return;

    // Validate: isDead must be 0 or 1
    uint8_t dead = *(uint8_t*)((uintptr_t)obj + PLAYER_ISDEAD);
    if (dead > 1) return;

    // Validate: CachedTransform should be a valid pointer
    void** transformPtr = (void**)((uintptr_t)obj + PLAYER_CACHED_TRANSFORM);
    if (!transformPtr || !*transformPtr) return;
    uintptr_t tf = (uintptr_t)*transformPtr;
    if (tf < 0x10000 || (tf & 7) != 0) return;

    // Read name
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
    pd.isLocal = false;
    pd.curHP = 100;
    pd.maxHP = 100;

    pthread_mutex_lock(&g_data_mutex);
    // Deduplicate
    for (auto& existing : g_players) {
        if (existing.uniqueID == pd.uniqueID) {
            pthread_mutex_unlock(&g_data_mutex);
            return;
        }
    }
    g_players.push_back(pd);
    pthread_mutex_unlock(&g_data_mutex);
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
        char perms[8] = {};
        sscanf(line, "%lx-%lx %s", &start, &end, perms);

        // Only scan writable data regions (not executable)
        if (perms[0] != 'r' || perms[1] != 'w') continue;
        if (perms[2] == 'x') continue;

        // Skip very large regions (video memory, etc.)
        size_t size = end - start;
        if (size > 100 * 1024 * 1024) continue;

        for (uintptr_t addr = start; addr + 0x300 < end; addr += 16) {
            try_read_player((void*)addr);
        }
    }
    fclose(maps);
}

// ===================== Collector Thread =====================
void* collector_thread(void*) {
    for (int i = 0; i < 600; i++) {
        scan_for_players();
        if (!g_players.empty()) {
            sleep(3);
            continue;
        }
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
