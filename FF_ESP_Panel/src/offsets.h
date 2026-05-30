#pragma once
#include <cstdint>

// Free Fire Thailand (com.dts.freefireth) - Il2Cpp Dump Offsets
// libil2cpp.so base: 0x92180000

namespace Offsets {

    // ====== Core Unity ======
    constexpr uint32_t GameObject_Transform = 0x8; // m_CachedTransform
    constexpr uint32_t Transform_Position = 0x38;   // m_LocalPosition (Vector3)
    constexpr uint32_t Transform_Rotation = 0x44;   // m_LocalRotation (Quaternion)
    constexpr uint32_t Camera_ViewMatrix = 0x2E0;   // Non jitter matrix
    constexpr uint32_t Camera_ProjectionMatrix = 0x3A0;

    // ====== Entity Hierarchy ======
    // MonoBehaviour -> Entity -> ReplicationEntity -> COWReplicationEntity -> AttackableEntity -> Player
    constexpr uint32_t Entity_CachedTransform = 0x38;
    constexpr uint32_t Entity_UniqueID = 0x3C;

    constexpr uint32_t AttackableEntity_IsDead = 0x50;       // bool
    constexpr uint32_t AttackableEntity_Collider = 0x54;      // Collider ptr

    // ====== Player (start at 0x58) ======
    constexpr uint32_t Player_TeamModeID = 0x29C;              // uint
    constexpr uint32_t Player_PlayerID = 0x2A8;                // IHAAMHPPLMG (24 bytes)
    constexpr uint32_t Player_MainCameraTransform = 0x254;     // Transform ptr
    constexpr uint32_t Player_IsClientBot = 0x2EC;             // bool
    constexpr uint32_t Player_OriginalNickName = 0x2E8;        // String ptr
    constexpr uint32_t Player_CharacterController = 0x310;     // CharacterController ptr
    constexpr uint32_t Player_Speed = 0x380;                   // float
    constexpr uint32_t Player_M_AimAssist = 0x424;             // Aim assist ptr
    constexpr uint32_t Player_TeamMapMark = 0x34C;             // Vector3
    constexpr uint32_t Player_ShowMapMark = 0x358;             // bool
    constexpr uint32_t Player_PlayerAttributes = 0x4C0;        // PlayerAttributes ptr
    constexpr uint32_t Player_TeamColorStr = 0x52C;            // String ptr
    constexpr uint32_t Player_IsSkillActive = 0x714;           // bool
    constexpr uint32_t Player_IsCadet = 0x268;                 // bool
    constexpr uint32_t Player_FODACOFDEIG = 0x78;              // Vector3 (internal)

    // ====== PlayerAttributes ======
    constexpr uint32_t PlayerAttributes_SuperArmorSkillEffecting = 0x3C;
    constexpr uint32_t PlayerAttributes_IsSuperArmorEnable = 0x1C8;
    constexpr uint32_t PlayerAttributes_RunSpeedUpScale = 0x1D0;
    constexpr uint32_t PlayerAttributes_ShowEnermyTargetOnMap = 0x108;

    // ====== method offsets (virtual function table slots) ======
    // These are used to call methods by their vtable index
    namespace VTable {
        constexpr int Player_get_CurHP = 45;     // int Player::get_CurHP()
        constexpr int Player_get_MaxHP = 46;     // int Player::get_MaxHP()
        constexpr int Player_get_CurEP = 47;     // int Player::get_CurEP()
        constexpr int Player_get_CurAP = 48;     // int Player::get_CurAP()
        constexpr int Player_get_MaxAP = 49;     // int Player::get_MaxAP()
        constexpr int Player_get_TeamIndex = 50; // int Player::get_TeamIndex()
        constexpr int Player_get_IsDead = 51;    // bool Player::get_IsDead()
        constexpr int Player_IsVisible = 70;     // bool AttackableEntity::IsVisible()
        constexpr int Player_GetAttackableCenterWS = 62; // Vector3 GetAttackableCenterWS()
    }

} // namespace Offsets
