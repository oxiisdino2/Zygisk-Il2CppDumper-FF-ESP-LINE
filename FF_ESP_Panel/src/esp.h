#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DrawText
#undef DrawTextW
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include "structs.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

class ESPRenderer {
public:
    ESPRenderer() = default;
    ~ESPRenderer();

    bool Initialize(HWND overlayWindow);
    void BeginFrame();
    void EndFrame();
    void Render(const std::vector<PlayerInfo>& players, const ESPConfig& config);

    void DrawSnapline(const Vector2& footPos, const ESPConfig& config, bool isEnemy);
    void DrawBox(const Vector2& footPos, const Vector2& headPos, const ESPConfig& config, bool isEnemy);
    void DrawName(const std::string& name, const Vector2& headPos);
    void DrawHealthBar(const Vector2& footPos, const Vector2& headPos, int curHP, int maxHP);
    void DrawDistance(float distance, const Vector2& footPos);

    void SetScreenSize(int w, int h);

private:
    ID2D1Factory* m_d2dFactory = nullptr;
    ID2D1HwndRenderTarget* m_renderTarget = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    IDWriteTextFormat* m_textFormat = nullptr;

    int m_screenWidth = 1920;
    int m_screenHeight = 1080;
    HWND m_overlayWindow = nullptr;

    ID2D1SolidColorBrush* m_brush = nullptr;
    ID2D1SolidColorBrush* m_greenBrush = nullptr;
    ID2D1SolidColorBrush* m_redBrush = nullptr;
    ID2D1SolidColorBrush* m_whiteBrush = nullptr;
    ID2D1SolidColorBrush* m_blackBrush = nullptr;

    bool CreateDeviceResources();
    void DiscardDeviceResources();
};
