#include "../common/Platform.h"
#include "../common/Core.h"

#include "CPools.h"
#include "CTimer.h"
#include "CCarAI.h"
#include "GameVersion.h"
#include "safetyhook.hpp"

#include <algorithm>
#include <cmath>

namespace rtf::platform {
namespace {
constexpr std::uintptr_t kSlowCarDownForOtherCarAddress = 0x42D0E0;

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

const char *ModName() { return "GTASA Real Traffic Fix"; }
const char *IniFileName() { return "GTASARealTrafficFix.ini"; }
const char *LogFileName() { return "GTASARealTrafficFix.log"; }

bool IsNormalDrivingStyle(CVehicle *v) { return v->m_autoPilot.m_nCarDrivingStyle == DRIVINGSTYLE_STOP_FOR_CARS; }
bool IsHurriedDrivingStyle(CVehicle *v) {
    return v->m_autoPilot.m_nCarDrivingStyle == DRIVINGSTYLE_AVOID_CARS ||
           v->m_autoPilot.m_nCarDrivingStyle == DRIVINGSTYLE_PLOUGH_THROUGH;
}
bool IsPloughDrivingStyle(CVehicle *v) {
    return v->m_autoPilot.m_nCarDrivingStyle == DRIVINGSTYLE_PLOUGH_THROUGH;
}
bool IsWaitingAction(CVehicle *v) { return v->m_autoPilot.m_nTempAction == 1; }
bool IsReverseAction(CVehicle *v) {
    const int action = v->m_autoPilot.m_nTempAction;
    return action == 3 || action == 13 || action == 14 || action == 22;
}
bool HasTemporaryAction(CVehicle *v) { return v->m_autoPilot.m_nTempAction != 0; }
bool IsStoppingMission(CVehicle *v) {
    const eCarMission m = v->m_autoPilot.m_nCarMission;
    return m == MISSION_NONE || m == MISSION_WAITFORDELETION ||
           m == MISSION_EMERGENCYVEHICLE_STOP || m == MISSION_STOP_FOREVER;
}
bool HasMovementIntent(CVehicle *v) { return v && !IsStoppingMission(v) && !IsWaitingAction(v); }
std::uint64_t MovementSignature(CVehicle *v) {
    std::uint64_t h = static_cast<unsigned int>(v->m_autoPilot.m_nCarMission);
    h = Mix(h, static_cast<unsigned int>(v->m_autoPilot.m_nCarDrivingStyle));
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

float RoadSpeedMultiplier(CVehicle *vehicle) {
    if (!vehicle) return 1.0f;

    // The game updates field_41 from the active path node and passes this exact
    // value to FindSpeedMultiplierWithSpeedFromNodes. Rebuilding the value from
    // highway flags swapped ordinary and non-highway roads (1.0 became 0.65).
    const signed char speedCode = vehicle->m_autoPilot.field_41;
    const float multiplier =
        CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(speedCode);
    return std::isfinite(multiplier) && multiplier > 0.0f ? multiplier : 1.0f;
}

bool ReadUpcomingPathPoints(
    CVehicle *v, CVector &current, CVector &next, CVector &following) {
    if (!v) return false;
    const auto valid = [](const CNodeAddress &address) {
        constexpr int areaCount = NUM_PATH_MAP_AREAS + NUM_PATH_INTERIOR_AREAS;
        return address.m_nAreaId >= 0 && address.m_nAreaId < areaCount &&
               address.m_nNodeId >= 0 &&
               ThePaths.m_pPathNodes[address.m_nAreaId] &&
               static_cast<unsigned int>(address.m_nNodeId) <
                   ThePaths.m_dwNumNodes[address.m_nAreaId];
    };
    CNodeAddress currentAddress = v->m_autoPilot.m_currentAddress;
    CNodeAddress nextAddress = v->m_autoPilot.m_startingRouteNode;
    CNodeAddress followingAddress;
    followingAddress.Clear();
    if (v->m_autoPilot.m_nPathFindNodesCount > 0)
        followingAddress = v->m_autoPilot.m_aPathFindNodesInfo[0];
    if (!valid(currentAddress) || !valid(nextAddress)) return false;
    if (!valid(followingAddress)) {
        followingAddress = nextAddress;
        nextAddress = currentAddress;
        currentAddress = v->m_autoPilot.field_8;
    }
    if (!valid(currentAddress) || !valid(nextAddress) || !valid(followingAddress))
        return false;
    current = ThePaths.m_pPathNodes[currentAddress.m_nAreaId]
        [currentAddress.m_nNodeId].GetNodeCoors();
    next = ThePaths.m_pPathNodes[nextAddress.m_nAreaId]
        [nextAddress.m_nNodeId].GetNodeCoors();
    following = ThePaths.m_pPathNodes[followingAddress.m_nAreaId]
        [followingAddress.m_nNodeId].GetNodeCoors();
    return true;
}

void KeepCurrentLaneOnStraight(CVehicle *v) {
    if (v) v->m_autoPilot.m_nNextLane = v->m_autoPilot.m_nCurrentLane;
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
    if (v) v->m_fBreakPedal = std::max(v->m_fBreakPedal, 1.0f);
}

int VehicleStatus(CVehicle *v) { return static_cast<int>(v->m_nStatus); }
bool IsSupportedGroundVehicle(CVehicle *v) {
    if (!v) return false;
    switch (v->m_nVehicleSubClass) {
    case VEHICLE_AUTOMOBILE:
    case VEHICLE_BIKE:
    case VEHICLE_BMX:
    case VEHICLE_MTRUCK:
    case VEHICLE_QUAD:
        return true;
    default:
        return false;
    }
}
bool IsTwoWheeler(CVehicle *v) {
    return v && (v->m_nVehicleSubClass == VEHICLE_BIKE || v->m_nVehicleSubClass == VEHICLE_BMX);
}
bool IsAutomobileForPlacement(CVehicle *v) {
    return v && (v->m_nVehicleSubClass == VEHICLE_AUTOMOBILE || v->m_nVehicleSubClass == VEHICLE_MTRUCK);
}
bool CanSwitchToRealPhysics(CVehicle *v) {
    return v && (v->m_nVehicleSubClass == VEHICLE_AUTOMOBILE ||
                 v->m_nVehicleSubClass == VEHICLE_BMX ||
                 v->m_nVehicleSubClass == VEHICLE_MTRUCK);
}
void SwitchToRealPhysics(CVehicle *v) { CCarCtrl::SwitchVehicleToRealPhysics(v); }
bool IsTaxiVehicle(CVehicle *v) {
    if (!IsAutomobileForPlacement(v)) return false;
    auto *mi = reinterpret_cast<CVehicleModelInfo *>(CModelInfo::GetModelInfo(v->m_nModelIndex));
    return mi && mi->m_nVehicleClass == 6;
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

CVector WorldCoordWithOffset(CEntity *e, const CVector &o) {
    return Multiply3x3(*e->m_matrix, o) + e->m_matrix->pos;
}
float FindGroundZ(const CVector &c, bool &found) {
    return CWorld::FindGroundZFor3DCoord(c.x, c.y, c.z, &found, nullptr);
}
void SetVehiclePosition(CVehicle *v, const CVector &p) { v->SetPosn(p); }

bool ProcessLineOfSight(CVehicle *vehicle, const CVector &start, const CVector &end, CColPoint &point, CEntity *&entity) {
    CEntity *previous = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = vehicle;
    const bool hit = CWorld::ProcessLineOfSight(
        start, end, point, entity, true, true, true, true, false, false, false, false);
    CWorld::pIgnoreEntity = previous;
    return hit;
}

bool CalcScreenCoors(const CVector &world, RwV3d &screen, float &width, float &height) {
    return CSprite::CalcScreenCoors({world.x, world.y, world.z}, &screen, &width, &height, true, true);
}
void SetupDebugFont(float edgeScale) {
    CFont::SetBackground(false, false);
    CFont::SetJustify(false);
    CFont::SetProportional(false);
    CFont::SetEdge(edgeScale > 0.4f ? 1 : 0);
}

bool TriggerStuckRecovery(CVehicle *v, unsigned int now, unsigned int thresholdMs) {
    if (!v) return false;
    (void)thresholdMs;
    if (HasTemporaryAction(v)) return false;
    v->m_autoPilot.m_nTempAction = 3;
    v->m_autoPilot.m_nTempActionTime = now +
        (v->m_nCreatedBy == MISSION_VEHICLE ? 1500u : 750u);
    return true;
}


void CollectTrafficVehicles(std::vector<CVehicle *> &vehicles) {
    vehicles.clear();
    auto *pool = CPools::ms_pVehiclePool;
    if (!pool) return;
    vehicles.reserve(pool->m_nSize);
    for (int index = 0; index < pool->m_nSize; ++index) {
        if (CVehicle *vehicle = pool->GetAt(index)) vehicles.push_back(vehicle);
    }
}

void ProcessRandomVehicleSpawnProtection() {
    auto *pool = CPools::ms_pVehiclePool;
    if (!pool)
        return;

    const unsigned int now = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
    for (int index = 0; index < pool->m_nSize; ++index) {
        CVehicle *vehicle = pool->GetAt(index);
        if (!vehicle ||
            vehicle->m_nCreatedBy != RANDOM_VEHICLE ||
            vehicle->m_autoPilot.m_nCarMission != MISSION_CRUISE ||
            static_cast<unsigned int>(now - vehicle->m_nCreationTime) > 500u) {
            continue;
        }

        if (IsHurriedDrivingStyle(vehicle)) {
            vehicle->m_autoPilot.m_nCarDrivingStyle =
                static_cast<eCarDrivingStyle>(
                    IsTwoWheeler(vehicle) ? 6 : DRIVINGSTYLE_STOP_FOR_CARS);
        }
    }
}

void ProcessPlayerBikeSirenYield() {
    // The stock game calls this from CAutomobile::ProcessControl only. Bikes
    // never reach that call site, although the native routine supports them.
    if ((CTimer::m_FrameCounter & 7u) != 5u)
        return;

    CVehicle *vehicle = FindPlayerVehicle(-1, false);
    if (!vehicle || vehicle->m_nVehicleSubClass != VEHICLE_BIKE ||
        !vehicle->bSirenOrAlarm || !vehicle->UsesSiren()) {
        return;
    }

    CCarAI::MakeWayForCarWithSiren(vehicle);
}

void InstallNativeTrafficSlowdownHook() {
    auto &hook = NativeTrafficSlowdownHookStorage();
    if (hook || !plugin::IsGameVersion10us()) return;
    hook = safetyhook::create_inline(
        reinterpret_cast<void *>(kSlowCarDownForOtherCarAddress),
        reinterpret_cast<void *>(&NativeTrafficSlowdownHook));
}
} // namespace rtf::platform
