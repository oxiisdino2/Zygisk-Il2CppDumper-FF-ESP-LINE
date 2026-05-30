#pragma once
#include "structs.h"

class WorldToScreen {
public:
    WorldToScreen() = default;

    void SetMatrices(const CameraData& cam);
    bool WorldToScreenPoint(const Vector3& worldPos, Vector2& screenOut);

    const Matrix4x4& GetViewMatrix() const { return m_viewMatrix; }
    const Matrix4x4& GetProjMatrix() const { return m_projMatrix; }

private:
    Matrix4x4 m_viewMatrix;
    Matrix4x4 m_projMatrix;
    int m_screenWidth = 1920;
    int m_screenHeight = 1080;
};
