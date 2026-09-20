#pragma once

#include "plugin.h"
#include "CAutomobile.h"
#include "CCarCtrl.h"
#include "CFont.h"
#include "CSprite.h"
#include "CWorld.h"
#include "CModelInfo.h"
#include "CPed.h"
#include "CVehicleModelInfo.h"
#include "eCarMission.h"
#include "eEntityStatus.h"
#include "eVehicleType.h"

#include <cstdint>
#include <vector>

namespace rtf::platform {

enum class SpeedSourceKind {
    None,
    AmbientTraffic,
    MovementTask
};

struct SpeedObservation {
    SpeedSourceKind kind = SpeedSourceKind::None;
    unsigned int value = 0;
};

const char *ModName();
const char *IniFileName();
const char *LogFileName();

bool IsNormalDrivingStyle(CVehicle *vehicle);
bool IsHurriedDrivingStyle(CVehicle *vehicle);
bool IsPloughDrivingStyle(CVehicle *vehicle);
bool IsWaitingAction(CVehicle *vehicle);
bool IsReverseAction(CVehicle *vehicle);
bool HasTemporaryAction(CVehicle *vehicle);
bool IsStoppingMission(CVehicle *vehicle);
bool HasMovementIntent(CVehicle *vehicle);
std::uint64_t MovementSignature(CVehicle *vehicle);
SpeedObservation ObserveRequestedSpeed(CVehicle *vehicle);
float RoadSpeedMultiplier(CVehicle *vehicle);
bool ReadUpcomingPathPoints(
    CVehicle *vehicle,
    CVector &current,
    CVector &next,
    CVector &following);
void KeepCurrentLaneOnStraight(CVehicle *vehicle);

unsigned int GetCruiseSpeed(CVehicle *vehicle);
void SetCruiseSpeed(CVehicle *vehicle, unsigned int speed);
float CruiseStorageMaximum();
float BrakeDeceleration(CVehicle *vehicle);
void ApplyFullBrake(CVehicle *vehicle);

int VehicleStatus(CVehicle *vehicle);
bool IsSupportedGroundVehicle(CVehicle *vehicle);
bool IsTwoWheeler(CVehicle *vehicle);
bool IsAutomobileForPlacement(CVehicle *vehicle);
bool CanSwitchToRealPhysics(CVehicle *vehicle);
void SwitchToRealPhysics(CVehicle *vehicle);
bool IsTaxiVehicle(CVehicle *vehicle);
bool IsMissionVehicleExcluded(CVehicle *vehicle);
bool IsPlayerDrivenMissionVehicle(CVehicle *vehicle);
bool CanProcessStatus(CVehicle *vehicle, bool playerDriver);

CVector WorldCoordWithOffset(CEntity *entity, const CVector &offset);
float FindGroundZ(const CVector &coord, bool &foundGround);
void SetVehiclePosition(CVehicle *vehicle, const CVector &position);
bool ProcessLineOfSight(CVehicle *vehicle, const CVector &start, const CVector &end, CColPoint &point, CEntity *&entity);
bool CalcScreenCoors(const CVector &world, RwV3d &screen, float &width, float &height);
void SetupDebugFont(float edgeScale);

bool TriggerStuckRecovery(CVehicle *vehicle, unsigned int now, unsigned int thresholdMs);
void CollectTrafficVehicles(std::vector<CVehicle *> &vehicles);
void ProcessRandomVehicleSpawnProtection();
void ProcessPlayerBikeSirenYield();
void InstallNativeTrafficSlowdownHook();

} // namespace rtf::platform
