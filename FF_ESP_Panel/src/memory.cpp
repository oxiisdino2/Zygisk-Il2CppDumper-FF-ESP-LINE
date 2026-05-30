#include "memory.h"

// Must define these BEFORE including Windows.h to prevent winsock.h conflict
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#undef DrawText
#undef DrawTextW

#include <iostream>
#include <sstream>
#include <regex>
#include <thread>
#include <chrono>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

#define ADB_TIMEOUT 10000

MemoryReader::MemoryReader() {}

MemoryReader::~MemoryReader() {
    Disconnect();
}

bool MemoryReader::FindADB() {
    const char* paths[] = {
        "adb",
        "adb.exe",
        // Bluestacks 5 variants (common)
        R"(C:\Program Files\BlueStacks_nxt\adb.exe)",
        R"(C:\Program Files\BlueStacks_nxt\HD-Adb.exe)",
        R"(C:\Program Files\BlueStacks_msi5\adb.exe)",
        R"(C:\Program Files\BlueStacks_msi5\HD-Adb.exe)",
        R"(C:\Program Files (x86)\BlueStacks_nxt\adb.exe)",
        R"(C:\Program Files (x86)\BlueStacks_nxt\HD-Adb.exe)",
        // Bluestacks 4
        R"(C:\Program Files\BlueStacks\adb.exe)",
        R"(C:\Program Files\BlueStacks\HD-Adb.exe)",
        R"(C:\Program Files (x86)\BlueStacks\adb.exe)",
        R"(C:\Program Files (x86)\BlueStacks\HD-Adb.exe)",
        // MSI Emulator
        R"(C:\Program Files\MSI\MsiEmulator\adb.exe)",
        R"(C:\Program Files (x86)\MSI\MsiEmulator\adb.exe)",
        // User home / AppData
        R"(C:\ProgramData\BlueStacks_nxt\adb.exe)",
        R"(C:\ProgramData\BlueStacks_nxt\HD-Adb.exe)",
        R"(C:\ProgramData\BlueStacks_msi5\adb.exe)",
        R"(C:\ProgramData\BlueStacks_msi5\HD-Adb.exe)",
    };
    for (auto& p : paths) {
        std::string result = ExecuteADB(std::string("\"") + p + "\" version");
        if (result.find("Android Debug Bridge") != std::string::npos) {
            m_adbPath = p;
            std::cout << "[+] ADB found: " << p << "\n";
            return true;
        }
    }
    return false;
}

std::string MemoryReader::ExecuteADB(const std::string& cmd) {
    std::string result;
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string fullCmd = "cmd /c " + cmd;
    char cmdLine[8192];
    strncpy_s(cmdLine, fullCmd.c_str(), _TRUNCATE);

    if (CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, ADB_TIMEOUT);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(hWrite);

    char buf[4096];
    DWORD read;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
        buf[read] = 0;
        result += buf;
    }
    CloseHandle(hRead);
    return result;
}

bool MemoryReader::Connect() {
    if (!FindADB()) {
        std::cerr << "[!] ADB not found.\n";
        return false;
    }

    std::string result = ExecuteADB("\"" + m_adbPath + "\" devices");
    if (result.find("device") == std::string::npos) {
        std::cerr << "[!] No emulator detected.\n";
        return false;
    }

    if (!StartSocketForward()) {
        std::cerr << "[!] Socket forward failed.\n";
        return false;
    }

    m_connected = true;
    std::cout << "[+] Connected via ADB\n";
    return true;
}

void MemoryReader::Disconnect() {
    m_connected = false;
    if (m_sockFd >= 0) {
        closesocket(m_sockFd);
        m_sockFd = -1;
    }
}

bool MemoryReader::StartSocketForward() {
    ExecuteADB("\"" + m_adbPath + "\" forward --remove-all");
    std::stringstream ss;
    ss << "\"" << m_adbPath << "\" forward tcp:" << m_forwardPort
       << " localabstract:ff_esp";
    std::string result = ExecuteADB(ss.str());
    return result.find("error") == std::string::npos;
}

bool MemoryReader::ConnectToHookSocket() {
    if (m_sockFd >= 0) return true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    m_sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockFd < 0) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_forwardPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(m_sockFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(m_sockFd);
        m_sockFd = -1;
        return false;
    }
    return true;
}

std::vector<PlayerInfo> MemoryReader::ReadPlayersFromSocket() {
    std::vector<PlayerInfo> players;

    if (!ConnectToHookSocket()) return players;

    uintptr_t localPlayerPtr = 0;
    int count = 0;

    int n = recv(m_sockFd, (char*)&localPlayerPtr, sizeof(localPlayerPtr), 0);
    if (n != (int)sizeof(localPlayerPtr)) { closesocket(m_sockFd); m_sockFd = -1; return players; }

    n = recv(m_sockFd, (char*)&count, sizeof(count), 0);
    if (n != (int)sizeof(count) || count <= 0 || count > 100) return players;

#pragma pack(push, 1)
    struct RawPlayerData {
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
#pragma pack(pop)

    std::vector<RawPlayerData> raw(count);
    int expected = count * (int)sizeof(RawPlayerData);
    int received = 0;
    char* buf = (char*)raw.data();

    while (received < expected) {
        n = recv(m_sockFd, buf + received, expected - received, 0);
        if (n <= 0) break;
        received += n;
    }

    for (int i = 0; i < count; i++) {
        PlayerInfo pi{};
        pi.position = Vector3(raw[i].x, raw[i].y, raw[i].z);
        pi.headPosition = Vector3(raw[i].headX, raw[i].headY, raw[i].headZ);
        pi.nickName = raw[i].name;
        pi.teamIndex = raw[i].teamIndex;
        pi.curHP = raw[i].curHP;
        pi.maxHP = raw[i].maxHP;
        pi.curAP = raw[i].curAP;
        pi.isDead = raw[i].isDead;
        pi.isLocalPlayer = raw[i].isLocal;
        players.push_back(pi);
    }

    // Ensure local player is first
    for (size_t i = 0; i < players.size(); i++) {
        if (players[i].isLocalPlayer && i != 0) {
            std::swap(players[0], players[i]);
            break;
        }
    }

    return players;
}

bool MemoryReader::IsGameRunning() {
    return ConnectToHookSocket();
}

bool MemoryReader::ReadProcessMemory(uintptr_t address, void* buffer, size_t size) {
    std::stringstream ss;
    ss << "\"" << m_adbPath << "\" shell su -c \"dd if=/proc/$(pidof "
       << "com.dts.freefireth" << ")/mem bs=" << size
       << " skip=" << (address / 4096) << " count=1 2>/dev/null\"";

    std::string result = ExecuteADB(ss.str());
    if (result.size() >= size) {
        memcpy(buffer, result.data(), size);
        return true;
    }
    return false;
}
