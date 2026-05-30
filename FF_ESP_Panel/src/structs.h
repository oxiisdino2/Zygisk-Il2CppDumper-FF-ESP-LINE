#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    float Distance(const Vector3& o) const {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
};

struct Vector4 {
    float x, y, z, w;
};

struct Matrix4x4 {
    float m[4][4];

    float& operator()(int row, int col) { return m[row][col]; }
    const float& operator()(int row, int col) const { return m[row][col]; }

    Vector4 GetRow(int row) const {
        return { m[row][0], m[row][1], m[row][2], m[row][3] };
    }

    static Matrix4x4 Identity() {
        Matrix4x4 mat{};
        for (int i = 0; i < 4; i++) mat.m[i][i] = 1.0f;
        return mat;
    }

    Matrix4x4 Transpose() const {
        Matrix4x4 mat{};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                mat.m[i][j] = m[j][i];
        return mat;
    }
};

struct PlayerInfo {
    uintptr_t address;
    Vector3 position;
    Vector3 headPosition;
    std::string nickName;
    int teamIndex;
    int curHP;
    int maxHP;
    int curAP;
    bool isDead;
    bool isLocalPlayer;
    float distance;
    bool isVisible;
    Vector2 screenPos;
    Vector2 screenHeadPos;
    bool onScreen;
};

struct CameraData {
    uintptr_t cameraAddress;
    Matrix4x4 viewMatrix;
    Matrix4x4 projMatrix;
    int screenWidth;
    int screenHeight;
};

struct ESPConfig {
    bool snaplines = true;
    bool boxes = true;
    bool names = true;
    bool health = true;
    bool distance = true;
    bool visibleOnly = false;
    float lineThickness = 2.0f;
};
