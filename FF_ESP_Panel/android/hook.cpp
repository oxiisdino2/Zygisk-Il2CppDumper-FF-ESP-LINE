// Free Fire ESP Hook Module
// Compile for ARM64: $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++ \
//   -shared -fPIC -o libffhook.so hook.cpp -static-libstdc++
//
// This module is loaded via Zygisk into com.dts.freefireth
// It hooks game functions and sends player data to the Windows overlay via socket

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
typedef uintptr_t Il2CppObject;
typedef uintptr_t Il2CppClass;
typedef uintptr_t MethodInfo;

struct String {
    Il2CppObject obj;
    int32_t length;
    uint16_t chars[];
};

struct Array {
    Il2CppObject obj;
    Il2CppObject bounds;
    int32_t max_length;
};

struct List {
    Il2CppObject obj;
    Array* items;
    int32_t size;
    int32_t version;
};

// Player struct offsets (from dump.cs)
#define PLAYER_ISDEAD          0x50
#define PLAYER_TEAMID          0x29C
#define PLAYER_PLAYERID        0x2A8
#define PLAYER_NICKNAME        0x2E8
#define PLAYER_CACHED_TRANSFORM 0x38
#define TRANSFORM_POSITION     0x38
#define ENTITY_UNIQUE_ID       0x3C

// ===================== Hooking infrastructure =====================
// Simple PLT hook - replace GOT entries
// In production, use a proper hooking library (SandHook, Dobby, etc.)

typedef void* (*orig_CurrentLocalPlayer_t)(const MethodInfo*);
orig_CurrentLocalPlayer_t orig_CurrentLocalPlayer = nullptr;

// Socket for communication with Windows overlay
int g_sock_fd = -1;
pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

struct PlayerData {
    float x, y, z;          // position
    float headX, headY, headZ;
    char name[64];           // nickname
    int teamIndex;
    int curHP, maxHP;
    int curAP;
    bool isDead;
    bool isLocal;
    uint32_t uniqueID;
};

std::vector<PlayerData> g_players;
void* g_localPlayer = nullptr;

// ===================== Socket Server Thread =====================
void* socket_thread(void*) {
    // Use abstract socket (Android local namespace)
    // ADB can forward: adb forward tcp:38300 localabstract:ff_esp
    const char* sock_name = "ff_esp";

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return nullptr;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // Abstract socket: first byte of sun_path is null
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, sock_name, sizeof(addr.sun_path) - 2);

    unlink(sock_name);
    bind(server_fd, (struct sockaddr*)&addr,
         offsetof(struct sockaddr_un, sun_path) + 1 + strlen(sock_name));
    listen(server_fd, 1);

    while (true) {
        g_sock_fd = accept(server_fd, nullptr, nullptr);
        if (g_sock_fd < 0) continue;

        // Client connected - send player data in a loop
        while (true) {
            pthread_mutex_lock(&g_data_mutex);
            int count = (int)g_players.size();

            // Send header: local player address + player count
            write(g_sock_fd, &g_localPlayer, sizeof(g_localPlayer));
            write(g_sock_fd, &count, sizeof(count));

            // Send each player's data
            for (int i = 0; i < count; i++) {
                write(g_sock_fd, &g_players[i], sizeof(PlayerData));
            }
            pthread_mutex_unlock(&g_data_mutex);

            usleep(50000); // 50ms = 20 FPS update
        }

        close(g_sock_fd);
        g_sock_fd = -1;
    }
    return nullptr;
}

// ===================== Helper function to read Il2Cpp strings =====================
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

// ===================== Hooked Functions =====================
// Hook 1: GameFacade.CurrentLocalPlayer - capture local player
void* hooked_CurrentLocalPlayer(const MethodInfo* method) {
    void* player = orig_CurrentLocalPlayer(method);
    if (player) {
        g_localPlayer = player;
    }
    return player;
}

// ===================== Player Data Collector =====================
void collect_player_data(void* player, bool isLocal) {
    if (!player) return;

    // Read from libunity.so for Transform
    // We use direct memory reads since we're in-process
    PlayerData pd = {};

    // Read name
    void** namePtr = (void**)((uintptr_t)player + PLAYER_NICKNAME);
    read_unity_string(*namePtr, pd.name, sizeof(pd.name));

    // Read team ID
    pd.teamIndex = *(int*)((uintptr_t)player + PLAYER_TEAMID);

    // Read dead flag
    pd.isDead = *(bool*)((uintptr_t)player + PLAYER_ISDEAD);

    // Read position via Transform
    void** transformPtr = (void**)((uintptr_t)player + PLAYER_CACHED_TRANSFORM);
    if (transformPtr && *transformPtr) {
        void* transform = *transformPtr;
        float* pos = (float*)((uintptr_t)transform + TRANSFORM_POSITION);
        pd.x = pos[0];
        pd.y = pos[1];
        pd.z = pos[2];
        pd.headX = pos[0];
        pd.headY = pos[1] + 1.8f;
        pd.headZ = pos[2];
    }

    // Read unique ID
    pd.uniqueID = *(uint32_t*)((uintptr_t)player + ENTITY_UNIQUE_ID);
    pd.isLocal = isLocal;
    pd.curHP = 100;
    pd.maxHP = 100;

    // NOTE: For actual HP, you need to call get_CurHP() virtual function.
    // This requires knowing the vtable slot index and calling through it.
    // For now we use the vtable offset from dump.cs:
    // VTable slot 45 = get_CurHP (varies by game version)

    pthread_mutex_lock(&g_data_mutex);
    g_players.push_back(pd);
    pthread_mutex_unlock(&g_data_mutex);
}

// ===================== Hooking =====================
// Automatically called when the library is loaded
__attribute__((constructor))
void init() {
    // Start socket server in background thread
    pthread_t tid;
    pthread_create(&tid, nullptr, socket_thread, nullptr);
    pthread_detach(tid);

    // Wait a bit for game to initialize
    sleep(5);

    // Hook GameFacade.CurrentLocalPlayer
    void* handle = dlopen("libil2cpp.so", RTLD_NOW);
    if (!handle) return;

    void* funcAddr = dlsym(handle, "COW_GameFacade__CurrentLocalPlayer");
    if (!funcAddr) {
        // Fallback: find by offset if symbols stripped
        // Use /proc/self/maps to find libil2cpp.so base
        FILE* maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[256];
            uintptr_t base = 0;
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "libil2cpp.so")) {
                    base = strtoul(line, nullptr, 16);
                    break;
                }
            }
            fclose(maps);
            if (base) {
                funcAddr = (void*)(base + 0x5F52F04); // CurrentLocalPlayer RVA
            }
        }
    }

    if (funcAddr) {
        // Install hook (simplified - in production use a hook library)
        orig_CurrentLocalPlayer = (orig_CurrentLocalPlayer_t)funcAddr;
        // mprotect to make writable, replace first instructions
        // This is a simplified illustration - real hooking is more complex
    }

    // Hook player update method to collect all players
    // In practice, you'd hook Entity update or ReplicationEntity register
}
