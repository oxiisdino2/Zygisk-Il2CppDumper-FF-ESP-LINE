#include "esp.h"
#include <cmath>

ESPRenderer::~ESPRenderer() {
    DiscardDeviceResources();
    if (m_d2dFactory) m_d2dFactory->Release();
    if (m_dwriteFactory) m_dwriteFactory->Release();
    if (m_textFormat) m_textFormat->Release();
}

bool ESPRenderer::Initialize(HWND overlayWindow) {
    m_overlayWindow = overlayWindow;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&m_dwriteFactory);
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(
        L"Arial", nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-US", &m_textFormat);
    if (FAILED(hr)) return false;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return CreateDeviceResources();
}

void ESPRenderer::SetScreenSize(int w, int h) {
    m_screenWidth = w;
    m_screenHeight = h;
}

bool ESPRenderer::CreateDeviceResources() {
    if (m_renderTarget) return true;
    if (!m_overlayWindow) return false;

    RECT rc;
    GetClientRect(m_overlayWindow, &rc);
    m_screenWidth = rc.right - rc.left;
    m_screenHeight = rc.bottom - rc.top;

    if (m_screenWidth <= 0 || m_screenHeight <= 0) return false;

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(m_overlayWindow,
            D2D1::SizeU(m_screenWidth, m_screenHeight)),
        &m_renderTarget);

    if (FAILED(hr)) return false;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &m_brush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 1, 0, 1), &m_greenBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &m_redBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_whiteBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &m_blackBrush);

    return true;
}

void ESPRenderer::DiscardDeviceResources() {
    if (m_renderTarget) m_renderTarget->Release();
    m_renderTarget = nullptr;
    if (m_brush) { m_brush->Release(); m_brush = nullptr; }
    if (m_greenBrush) { m_greenBrush->Release(); m_greenBrush = nullptr; }
    if (m_redBrush) { m_redBrush->Release(); m_redBrush = nullptr; }
    if (m_whiteBrush) { m_whiteBrush->Release(); m_whiteBrush = nullptr; }
    if (m_blackBrush) { m_blackBrush->Release(); m_blackBrush = nullptr; }
}

void ESPRenderer::BeginFrame() {
    if (!CreateDeviceResources()) return;
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
}

void ESPRenderer::EndFrame() {
    if (m_renderTarget) {
        HRESULT hr = m_renderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
        }
    }
}

void ESPRenderer::Render(const std::vector<PlayerInfo>& players, const ESPConfig& config, bool connected) {
    if (!m_renderTarget) return;

    if (!connected) {
        DrawStatus("Waiting for hook module...");
        return;
    }

    if (players.empty()) {
        DrawStatus("Connected - no players found");
        return;
    }

    for (const auto& p : players) {
        if (p.isDead) continue;
        if (!p.onScreen) continue;

        bool isEnemy = !p.isLocalPlayer && (p.teamIndex != players[0].teamIndex);
        bool isLocal = p.isLocalPlayer;

        if (config.snaplines)
            DrawSnapline(p.screenPos, config, isEnemy, isLocal);
        if (config.boxes)
            DrawBox(p.screenPos, p.screenHeadPos, config, isEnemy, isLocal);
        if (config.names && !p.nickName.empty())
            DrawName(p.nickName, p.screenHeadPos, isLocal);
        if (config.health)
            DrawHealthBar(p.screenPos, p.screenHeadPos, p.curHP, p.maxHP);
        if (config.distance)
            DrawDistance(p.distance, p.screenPos);
    }
}

void ESPRenderer::DrawSnapline(const Vector2& footPos, const ESPConfig& config, bool isEnemy, bool isLocal) {
    D2D1_COLOR_F color;
    if (isLocal) color = D2D1::ColorF(0, 1, 1, 0.8f);
    else if (isEnemy) color = D2D1::ColorF(1, 0, 0, 0.8f);
    else color = D2D1::ColorF(0, 1, 0, 0.8f);
    m_brush->SetColor(color);
    m_renderTarget->DrawLine(
        D2D1::Point2F((float)m_screenWidth / 2.0f, (float)m_screenHeight),
        D2D1::Point2F(footPos.x, footPos.y),
        m_brush, config.lineThickness);
}

void ESPRenderer::DrawBox(const Vector2& footPos, const Vector2& headPos,
                          const ESPConfig& config, bool isEnemy, bool isLocal) {
    D2D1_COLOR_F color;
    if (isLocal) color = D2D1::ColorF(0, 1, 1, 0.8f);
    else if (isEnemy) color = D2D1::ColorF(1, 0, 0, 0.8f);
    else color = D2D1::ColorF(0, 1, 0, 0.8f);
    m_brush->SetColor(color);

    float height = fabs(footPos.y - headPos.y);
    if (height < 5.0f) return;
    float width = height * 0.45f;

    float centerX = (footPos.x + headPos.x) / 2.0f;
    float left = centerX - width / 2.0f;
    float top = headPos.y;
    float right = centerX + width / 2.0f;
    float bottom = footPos.y;
    float t = config.lineThickness;

    m_renderTarget->DrawRectangle(D2D1::RectF(left, top, right, bottom), m_brush, t);

    float cl = width * 0.2f;
    m_renderTarget->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(left + cl, top), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(left, top + cl), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(right, top), D2D1::Point2F(right - cl, top), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(right, top), D2D1::Point2F(right, top + cl), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(left, bottom), D2D1::Point2F(left + cl, bottom), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(left, bottom), D2D1::Point2F(left, bottom - cl), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(right, bottom), D2D1::Point2F(right - cl, bottom), m_brush, t);
    m_renderTarget->DrawLine(D2D1::Point2F(right, bottom), D2D1::Point2F(right, bottom - cl), m_brush, t);
}

void ESPRenderer::DrawName(const std::string& name, const Vector2& headPos, bool isLocal) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wname(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname.data(), wlen);

    m_blackBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.5f));
    m_renderTarget->FillRectangle(
        D2D1::RectF(headPos.x - 65, headPos.y - 22, headPos.x + 65, headPos.y - 4),
        m_blackBrush);

    m_whiteBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    m_renderTarget->DrawText(wname.c_str(), (UINT32)wname.size() - 1, m_textFormat,
        D2D1::RectF(headPos.x - 65, headPos.y - 22, headPos.x + 65, headPos.y - 2),
        m_whiteBrush);
}

void ESPRenderer::DrawHealthBar(const Vector2& footPos, const Vector2& headPos,
                                int curHP, int maxHP) {
    if (maxHP <= 0 || curHP < 0) return;

    float height = fabs(footPos.y - headPos.y);
    if (height < 5.0f) return;
    float barWidth = 4.0f;
    float centerX = (footPos.x + headPos.x) / 2.0f;
    float left = centerX + height * 0.45f / 2.0f + 3.0f;

    m_blackBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
    m_renderTarget->FillRectangle(
        D2D1::RectF(left, headPos.y, left + barWidth, footPos.y), m_blackBrush);

    float pct = (float)curHP / (float)maxHP;
    if (pct > 1.0f) pct = 1.0f;
    if (pct < 0.0f) pct = 0.0f;
    float fillH = height * pct;

    D2D1_COLOR_F c;
    if (pct > 0.6f) c = D2D1::ColorF(0, 1, 0, 1);
    else if (pct > 0.3f) c = D2D1::ColorF(1, 1, 0, 1);
    else c = D2D1::ColorF(1, 0, 0, 1);

    m_brush->SetColor(c);
    m_renderTarget->FillRectangle(
        D2D1::RectF(left, footPos.y - fillH, left + barWidth, footPos.y), m_brush);
}

void ESPRenderer::DrawDistance(float distance, const Vector2& footPos) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0fm", distance);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wbuf(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf.data(), wlen);

    m_blackBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.5f));
    m_renderTarget->FillRectangle(
        D2D1::RectF(footPos.x - 30, footPos.y + 2, footPos.x + 30, footPos.y + 18),
        m_blackBrush);

    m_whiteBrush->SetColor(D2D1::ColorF(1, 1, 0, 1));
    m_renderTarget->DrawText(wbuf.c_str(), (UINT32)wbuf.size() - 1, m_textFormat,
        D2D1::RectF(footPos.x - 30, footPos.y + 2, footPos.x + 30, footPos.y + 20),
        m_whiteBrush);
}

void ESPRenderer::DrawStatus(const std::string& text) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wtext(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

    float cx = (float)m_screenWidth / 2.0f;
    float cy = (float)m_screenHeight / 2.0f;

    m_blackBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.7f));
    m_renderTarget->FillRectangle(
        D2D1::RectF(cx - 180, cy - 20, cx + 180, cy + 20), m_blackBrush);

    m_whiteBrush->SetColor(D2D1::ColorF(0, 1, 1, 1));
    m_renderTarget->DrawText(wtext.c_str(), (UINT32)wtext.size() - 1, m_textFormat,
        D2D1::RectF(cx - 175, cy - 18, cx + 175, cy + 18), m_whiteBrush);
}
