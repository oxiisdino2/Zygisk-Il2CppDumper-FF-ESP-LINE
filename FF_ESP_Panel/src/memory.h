#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include "structs.h"

class MemoryReader {
public:
    MemoryReader();
    ~MemoryReader();

    bool Connect();
    void Disconnect();
    bool IsConnected() const { return m_connected; }
    void SetADBPath(const std::string& path) { m_adbPath = path; }

    bool StartSocketForward();
    std::string ExecuteADB(const std::string& cmd);
    bool ConnectToHookSocket();
    std::vector<PlayerInfo> ReadPlayersFromSocket();
    bool IsGameRunning();
    bool ReadProcessMemory(uintptr_t address, void* buffer, size_t size);

private:
    std::atomic<bool> m_connected{ false };
    std::string m_adbPath;
    int m_sockFd = -1;
    int m_forwardPort = 38300;

    bool FindADB();
};
