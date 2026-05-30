#include "w2s.h"
#include <cmath>

void WorldToScreen::SetMatrices(const CameraData& cam) {
    m_viewMatrix = cam.viewMatrix;
    m_projMatrix = cam.projMatrix;
    m_screenWidth = cam.screenWidth;
    m_screenHeight = cam.screenHeight;
}

bool WorldToScreen::WorldToScreenPoint(const Vector3& worldPos, Vector2& screenOut) {
    // Combined VP = Projection * View (column-major convention)
    Matrix4x4 vp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            vp.m[i][j] = 0;
            for (int k = 0; k < 4; k++) {
                vp.m[i][j] += m_projMatrix.m[i][k] * m_viewMatrix.m[k][j];
            }
        }
    }

    // Transform world position to clip space
    // Using column-major: result = VP * (x, y, z, 1)
    float clipX = worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] +
                  worldPos.z * vp.m[2][0] + vp.m[3][0];
    float clipY = worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] +
                  worldPos.z * vp.m[2][1] + vp.m[3][1];
    float clipZ = worldPos.x * vp.m[0][2] + worldPos.y * vp.m[1][2] +
                  worldPos.z * vp.m[2][2] + vp.m[3][2];
    float clipW = worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] +
                  worldPos.z * vp.m[2][3] + vp.m[3][3];

    // Behind camera check
    if (clipW < 0.1f) return false;

    // Perspective divide to NDC (-1 to 1)
    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;
    float ndcZ = clipZ / clipW;

    // Check if within NDC bounds (with small margin)
    if (fabs(ndcX) > 1.5f || fabs(ndcY) > 1.5f || fabs(ndcZ) > 1.0f)
        return false;

    // Convert NDC to screen coordinates
    screenOut.x = (m_screenWidth / 2.0f) + (ndcX * m_screenWidth / 2.0f);
    screenOut.y = (m_screenHeight / 2.0f) - (ndcY * m_screenHeight / 2.0f);

    return true;
}
