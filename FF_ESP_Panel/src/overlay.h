#pragma once
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "structs.h"

class Overlay {
public:
    Overlay();
    ~Overlay();

    bool Create(HWND targetWindow);
    void Destroy();
    void Run();

    void SetPlayers(const std::vector<PlayerInfo>& players) { m_players = players; }
    void SetConfig(const ESPConfig& config) { m_config = config; }
    ESPConfig& GetConfig() { return m_config; }

    HWND GetWindow() const { return m_overlayWnd; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    bool IsRunning() const { return m_running; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_overlayWnd = nullptr;
    HWND m_targetWnd = nullptr;
    HINSTANCE m_instance = nullptr;
    int m_width = 1920;
    int m_height = 1080;
    bool m_running = false;

    std::vector<PlayerInfo> m_players;
    ESPConfig m_config;

    void UpdateWindowPosition();
    bool RegisterWindowClass();
};
