#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>

#include "overlay.h"
#include "esp.h"
#include "memory.h"
#include "w2s.h"

Overlay g_overlay;
ESPRenderer g_esp;
MemoryReader g_memory;
WorldToScreen g_w2s;
std::atomic<bool> g_running{ true };
std::vector<PlayerInfo> g_players;
std::mutex g_playerMutex;

void ToggleConsole() {
    static bool consoleVisible = false;
    consoleVisible = !consoleVisible;
    if (consoleVisible) {
        AllocConsole();
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        std::cout << "[+] FF ESP Panel - Debug Console\n";
    } else {
        FreeConsole();
    }
}

void ESPLoop() {
    while (g_running) {
        if (!g_memory.IsGameRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // Read player data from the injected .so via socket
        auto players = g_memory.ReadPlayersFromSocket();

        if (!players.empty()) {
            Vector3 localPos = players[0].position;
            for (auto& p : players) {
                if (p.isDead) continue;
                p.distance = localPos.Distance(p.position);
                p.onScreen = g_w2s.WorldToScreenPoint(p.position, p.screenPos);
                Vector2 headScr;
                if (g_w2s.WorldToScreenPoint(p.headPosition, headScr)) {
                    p.screenHeadPos = headScr;
                    if (!p.onScreen) {
                        p.onScreen = true;
                        p.screenPos = headScr;
                    }
                }
                if (p.distance > 300.0f) p.onScreen = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_playerMutex);
            g_players = std::move(players);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void HandleHotkeys() {
    while (g_running) {
        if (GetAsyncKeyState(VK_F1) & 1) ToggleConsole();
        if (GetAsyncKeyState(VK_F2) & 1) g_overlay.GetConfig().snaplines = !g_overlay.GetConfig().snaplines;
        if (GetAsyncKeyState(VK_F3) & 1) g_overlay.GetConfig().boxes = !g_overlay.GetConfig().boxes;
        if (GetAsyncKeyState(VK_F4) & 1) g_overlay.GetConfig().names = !g_overlay.GetConfig().names;
        if (GetAsyncKeyState(VK_F5) & 1) g_overlay.GetConfig().health = !g_overlay.GetConfig().health;
        if (GetAsyncKeyState(VK_F6) & 1) g_overlay.GetConfig().distance = !g_overlay.GetConfig().distance;
        if (GetAsyncKeyState(VK_END) & 1) g_running = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void RenderLoop() {
    while (g_running) {
        g_esp.BeginFrame();
        {
            std::lock_guard<std::mutex> lock(g_playerMutex);
            bool connected = g_memory.IsConnected();
            g_esp.Render(g_players, g_overlay.GetConfig(), connected);
        }
        g_esp.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    ToggleConsole();

    std::cout << R"(
╔═══════════════════════════════════════╗
║        FF ESP PANEL v1.0             ║
╚═══════════════════════════════════════╝

)";

    if (!g_memory.Connect()) {
        std::cout << "[!] ADB auto-detect failed.\n";
        std::cout << "Enter the full path to adb.exe/HD-Adb.exe (or leave blank to exit):\n> ";
        std::string customPath;
        std::getline(std::cin, customPath);
        if (!customPath.empty()) {
            g_memory.SetADBPath(customPath);
            if (!g_memory.Connect()) {
                std::cout << "[!] Still failed.\n";
                system("pause");
                return 1;
            }
        } else {
            std::cout << "[!] Make sure:\n";
            std::cout << "  1. Bluestacks/MSI Emulator is running\n";
            std::cout << "  2. Free Fire is open\n";
            std::cout << "  3. The Zygisk hook module is installed\n";
            system("pause");
            return 1;
        }
    }

    HWND targetWnd = nullptr;
    const char* classNames[] = {
        "BlueStacksApp", "BSever_BstKey1", "Qt5QWindowIcon",
        "QWidget", "MainWindowWindow", "WindowsForms10.Window.8.app.0.141b42a_r11_ad1",
        "MSIEmulatorWindow", "Window_8_App_0_141b42a_r11_ad1",
        nullptr
    };
    for (int i = 0; classNames[i]; i++) {
        targetWnd = FindWindowA(classNames[i], nullptr);
        if (targetWnd) break;
    }
    if (!targetWnd) {
        // Try finding by window title
        const char* titles[] = {
            "BlueStacks", "BlueStacks App Player", "MSI App Player",
            "Free Fire", nullptr
        };
        for (int i = 0; titles[i]; i++) {
            targetWnd = FindWindowA(nullptr, titles[i]);
            if (targetWnd) break;
        }
    }
    if (!targetWnd) {
        std::cout << "[!] Emulator window not found.\n";
        std::cout << "[!] Listing visible windows for debugging:\n";
        EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;
            char cls[128] = {}, title[256] = {};
            GetClassNameA(hwnd, cls, sizeof(cls));
            GetWindowTextA(hwnd, title, sizeof(title));
            if (strlen(title) > 0)
                std::cout << "  class='" << cls << "' title='" << title << "'\n";
            return TRUE;
        }, 0);
        std::cout << "[!] Make sure Bluestacks/MSI Emulator is open.\n";
        system("pause");
        return 1;
    }

    if (!g_overlay.Create(targetWnd)) {
        std::cout << "[!] Overlay creation failed.\n";
        return 1;
    }
    if (!g_esp.Initialize(g_overlay.GetWindow())) {
        std::cout << "[!] ESP renderer init failed.\n";
        return 1;
    }

    std::cout << "[+] Overlay active. Hotkeys:\n";
    std::cout << "  F1=Console  F2=Lines  F3=Boxes  F4=Names  F5=HP  F6=Dist  END=Exit\n";

    std::thread espThread(ESPLoop);
    std::thread hotkeyThread(HandleHotkeys);
    std::thread renderThread(RenderLoop);

    g_overlay.Run();

    g_running = false;
    if (espThread.joinable()) espThread.join();
    if (hotkeyThread.joinable()) hotkeyThread.join();
    if (renderThread.joinable()) renderThread.join();

    return 0;
}
