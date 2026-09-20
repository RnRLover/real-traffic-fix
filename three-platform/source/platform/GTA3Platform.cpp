#include "../common/Platform.h"

#include "../common/Core.h"
#include "CPools.h"
#include "safetyhook.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace rtf::platform {
namespace {
constexpr std::uintptr_t kSlowCarDownForOtherCarAddress = 0x419C60;

safetyhook::InlineHook &NativeTrafficSlowdownHookStorage() {
    static safetyhook::InlineHook hook;
    return hook;
}

void __cdecl NativeTrafficSlowdownHook(
    CEntity *entity, CVehicle *vehicle, float *speed, float currentSpeed) {
    if (rtf::ShouldSkipNativeTrafficSlowdown(entity, vehicle)) return;
    NativeTrafficSlowdownHookStorage().ccall<void>(
        entity, vehicle, speed, currentSpeed);
}

std::uint64_t Mix(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
}
}

const char *ModName() { return "GTA3 Real Traffic Fix"; }
const char *IniFileName() { return "GTA3RealTrafficFix.ini"; }
const char *LogFileName() { return "GTA3RealTrafficFix.log"; }

bool IsNormalDrivingStyle(CVehicle *v) { return v->m_autoPilot.m_nDrivingStyle == DRIVINGSTYLE_STOP_FOR_CARS; }
bool IsHurriedDrivingStyle(CVehicle *v) {
    return v->m_autoPilot.m_nDrivingStyle == DRIVINGSTYLE_AVOID_CARS ||
           v->m_autoPilot.m_nDrivingStyle == DRIVINGSTYLE_PLOUGH_THROUGH;
}
bool IsPloughDrivingStyle(CVehicle *v) {
    return v->m_autoPilot.m_nDrivingStyle == DRIVINGSTYLE_PLOUGH_THROUGH;
}
bool IsWaitingAction(CVehicle *v) { return v->m_autoPilot.m_nAnimationId == TEMPACT_WAIT; }
bool IsReverseAction(CVehicle *v) { return v->m_autoPilot.m_nAnimationId == TEMPACT_REVERSE; }
bool HasTemporaryAction(CVehicle *v) { return v->m_autoPilot.m_nAnimationId != TEMPACT_NONE; }
bool IsStoppingMission(CVehicle *v) {
    const eCarMission m = v->m_autoPilot.m_nCarMission;
    return m == MISSION_NONE || m == MISSION_WAITFORDELETION ||
           m == MISSION_EMERGENCYVEHICLE_STOP || m == MISSION_STOP_FOREVER;
}
bool HasMovementIntent(CVehicle *v) { return v && !IsStoppingMission(v) && !IsWaitingAction(v); }
std::uint64_t MovementSignature(CVehicle *v) {
    std::uint64_t h = static_cast<unsigned int>(v->m_autoPilot.m_nCarMission);
    h = Mix(h, static_cast<unsigned int>(v->m_autoPilot.m_nDrivingStyle));
    h = Mix(h, reinterpret_cast<std::uintptr_t>(v->m_autoPilot.m_pTargetCar));
    return h;
}

SpeedObservation ObserveRequestedSpeed(CVehicle *v) {
    if (!v || !HasMovementIntent(v)) return {};
    SpeedObservation result;
    result.kind = v->m_autoPilot.m_nCarMission == MISSION_CRUISE
        ? SpeedSourceKind::AmbientTraffic
        : SpeedSourceKind::MovementTask;
    result.value = static_cast<unsigned char>(v->m_autoPilot.m_nCruiseSpeed);
    return result;
}

float RoadSpeedMultiplier(CVehicle *) { return 1.0f; }

bool ReadUpcomingPathPoints(
    CVehicle *v, CVector &current, CVector &next, CVector &following) {
    if (!v) return false;
    const auto nodeIndex = [](const CNodeAddress &address) {
        return static_cast<int>(static_cast<unsigned short>(address.m_wAreaId));
    };
    const int count = ThePaths.m_nNumCarPathNodes;
    const auto valid = [count](int index) { return index >= 0 && index < count; };
    int currentIndex = nodeIndex(v->m_autoPilot.m_currentAddress);
    int nextIndex = nodeIndex(v->m_autoPilot.m_startingRouteNode);
    int followingIndex = v->m_autoPilot.m_nPathFindNodesCount > 0
        ? nodeIndex(v->m_autoPilot.m_aPathFindNodesInfo[0]) : -1;
    if (!valid(currentIndex) || !valid(nextIndex)) return false;
    if (!valid(followingIndex)) {
        followingIndex = nextIndex;
        nextIndex = currentIndex;
        currentIndex = nodeIndex(v->m_autoPilot.m_PreviousRouteNode);
    }
    if (!valid(currentIndex) || !valid(nextIndex) || !valid(followingIndex))
        return false;
    current = ThePaths.m_aPathNodes[currentIndex].GetPosition();
    next = ThePaths.m_aPathNodes[nextIndex].GetPosition();
    following = ThePaths.m_aPathNodes[followingIndex].GetPosition();
    return true;
}

void KeepCurrentLaneOnStraight(CVehicle *v) {
    if (!v) return;
    // plugin-sdk names these two GTA III bytes as path directions, while the
    // original game and re3 use the same offsets for current/next lane.
    auto *raw = reinterpret_cast<unsigned char *>(&v->m_autoPilot);
    raw[0x2C] = raw[0x2B];
}

unsigned int GetCruiseSpeed(CVehicle *v) { return static_cast<unsigned char>(v->m_autoPilot.m_nCruiseSpeed); }
void SetCruiseSpeed(CVehicle *v, unsigned int speed) {
    v->m_autoPilot.m_nCruiseSpeed = static_cast<decltype(v->m_autoPilot.m_nCruiseSpeed)>(static_cast<unsigned char>(speed));
}
float CruiseStorageMaximum() { return 255.0f; }
float BrakeDeceleration(CVehicle *v) {
    return v && v->m_pHandlingData ? v->m_pHandlingData->m_fBrakeDeceleration : 0.0f;
}
void ApplyFullBrake(CVehicle *v) {
    if (v) v->m_fBrakePedal = std::max(v->m_fBrakePedal, 1.0f);
}

int VehicleStatus(CVehicle *v) { return static_cast<int>(v->m_nStatus); }
bool IsSupportedGroundVehicle(CVehicle *v) {
    return v && CModelInfo::GetVehicleModelType(v->m_nModelIndex) == VEHICLE_AUTOMOBILE;
}
bool IsTwoWheeler(CVehicle *) { return false; }
bool IsAutomobileForPlacement(CVehicle *v) { return IsSupportedGroundVehicle(v); }
bool CanSwitchToRealPhysics(CVehicle *v) { return IsSupportedGroundVehicle(v); }
void SwitchToRealPhysics(CVehicle *v) {
    if ((v->m_nStatus & 1) == 0) {
        v->m_nStatus |= 1;
        CCarCtrl::SwitchVehicleToRealPhysics(v);
    }
}
bool IsTaxiVehicle(CVehicle *v) {
    if (!IsAutomobileForPlacement(v)) return false;
    auto *mi = reinterpret_cast<CVehicleModelInfo *>(CModelInfo::GetModelInfo(v->m_nModelIndex));
    return mi && mi->m_nVehicleClass == CARCLASS_TAXI;
}
bool IsMissionVehicleExcluded(CVehicle *v) {
    return v && v->m_nCreatedBy == MISSION_VEHICLE && v->m_pDriver && !v->m_pDriver->IsPlayer();
}
bool IsPlayerDrivenMissionVehicle(CVehicle *v) {
    return v && v->m_nCreatedBy == MISSION_VEHICLE && v->m_pDriver && v->m_pDriver->IsPlayer();
}
bool CanProcessStatus(CVehicle *v, bool playerDriver) {
    if (!v || v->m_nStatus == STATUS_ABANDONED || v->m_nStatus == STATUS_WRECKED) return false;
    return playerDriver || v->m_nStatus == STATUS_SIMPLE || v->m_nStatus == STATUS_PHYSICS;
}

CVector WorldCoordWithOffset(CEntity *e, const CVector &o) { return e->TransformFromObjectSpace(o); }
float FindGroundZ(const CVector &c, bool &found) { return CWorld::FindGroundZFor3DCoord(c.x, c.y, c.z, &found); }
void SetVehiclePosition(CVehicle *v, const CVector &p) { v->SetPosition(p.x, p.y, p.z); }

bool ProcessLineOfSight(CVehicle *vehicle, const CVector &start, const CVector &end, CColPoint &point, CEntity *&entity) {
    CEntity *previous = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = vehicle;
    const bool hit = CWorld::ProcessLineOfSight(
        start, end, point, entity, true, true, true, true, false, false, false);
    CWorld::pIgnoreEntity = previous;
    return hit;
}

bool CalcScreenCoors(const CVector &world, RwV3d &screen, float &width, float &height) {
    return CSprite::CalcScreenCoors({world.x, world.y, world.z}, &screen, &width, &height, true);
}
void SetupDebugFont(float) {
    CFont::SetJustifyOff();
    CFont::SetCentreOff();
    CFont::SetRightJustifyOff();
    CFont::SetBackgroundOff();
    CFont::SetPropOff();
}

bool TriggerStuckRecovery(CVehicle *v, unsigned int now, unsigned int) {
    if (!v || HasTemporaryAction(v)) return false;
    v->m_autoPilot.m_nAnimationId = TEMPACT_REVERSE;
    v->m_autoPilot.m_nAnimationTime = now + 1500u;
    return true;
}
void CollectTrafficVehicles(std::vector<CVehicle *> &vehicles) {
    vehicles.clear();
    auto *pool = CPools::GetVehiclePool();
    if (!pool) return;
    vehicles.reserve(pool->m_nSize);
    for (int index = 0; index < pool->m_nSize; ++index) {
        if (CVehicle *vehicle = pool->GetAt(index)) vehicles.push_back(vehicle);
    }
}

void ProcessRandomVehicleSpawnProtection() {
    auto *pool = CPools::GetVehiclePool();
    if (!pool)
        return;

    static std::vector<unsigned char> observedIds;
    if (observedIds.size() != static_cast<std::size_t>(pool->m_nSize))
        observedIds.assign(pool->m_nSize, 0xFFu);

    for (int index = 0; index < pool->m_nSize; ++index) {
        const unsigned char id = pool->m_byteMap[index].IntValue();
        if (observedIds[index] == id)
            continue;
        observedIds[index] = id;

        CVehicle *vehicle = pool->GetAt(index);
        if (vehicle && vehicle->m_nCreatedBy == RANDOM_VEHICLE &&
            vehicle->m_autoPilot.m_nCarMission == MISSION_CRUISE &&
            IsHurriedDrivingStyle(vehicle)) {
            vehicle->m_autoPilot.m_nDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        }
    }
}
void ProcessPlayerBikeSirenYield() {}
void InstallNativeTrafficSlowdownHook() {
    auto &hook = NativeTrafficSlowdownHookStorage();
    if (hook) return;
    hook = safetyhook::create_inline(
        reinterpret_cast<void *>(kSlowCarDownForOtherCarAddress),
        reinterpret_cast<void *>(&NativeTrafficSlowdownHook));
}
} // namespace rtf::platform
