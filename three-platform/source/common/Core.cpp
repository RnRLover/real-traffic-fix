/*
    Real Traffic Fix - shared core for GTA III, Vice City and San Andreas.
    Based on Real Traffic Fix by Valdir da Costa Junior (2020), MIT License.

    Only engine-independent traffic logic belongs in this file. Every direct
    game-layout decision is delegated to one of the three platform adapters.
*/

#include "Core.h"
#include "Platform.h"

#include "CCamera.h"
#include "CColModel.h"
#include "CColPoint.h"
#include "CEntity.h"
#include "CFont.h"
#include "CSprite.h"
#include "CTimer.h"
#include "CVector.h"
#include "CWorld.h"
#include "tHandlingData.h"
#include "common.h"
#include "enums/eScriptCommands.h"
#include "extensions/ScriptCommands.h"
#include "plugin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>

using namespace plugin;

namespace rtf {
namespace {

constexpr float kTimeStepMagic = 50.0f / 30.0f;
constexpr float kAiSpeedScale = 60.0f;
constexpr float kGameFramesPerSecond = 50.0f;
constexpr float kDebugLineHeight = 0.14f;
constexpr float kDebugTextDistance = 30.0f;
constexpr float kForwardProbeMinDistance = 2.0f;
constexpr float kRearProbeMinDistance = 1.5f;
constexpr unsigned int kObstacleClearConfirmationMs = 250;
constexpr unsigned char kGroundFailureConfirmationFrames = 3;
constexpr float kMinimumBrakeDeceleration = 1.5f;
constexpr float kMaximumBrakeDeceleration = 10.0f;
constexpr float kHandlingBrakeEfficiency = 0.65f;
constexpr float kComfortableBrakeScale = 0.40f;
constexpr float kComfortableBrakeMin = 0.75f;
constexpr float kComfortableBrakeMax = 3.5f;
constexpr float kComfortableStopMargin = 0.75f;
constexpr float kEmergencyReactionSeconds = 0.08f;
constexpr float kEmergencyBaseMargin = 0.35f;
constexpr float kEmergencyPedMargin = 0.75f;
constexpr float kEmergencyHardStopSeconds = 0.10f;
constexpr float kEmergencyHardStopMinDistance = 0.50f;
constexpr float kEmergencyReleaseMargin = 0.75f;
constexpr float kEmergencyBrakeReserve = 1.75f;
constexpr float kEmergencyMaximumDeceleration = 16.0f;
constexpr float kEmergencyStoppingDistanceFraction = 0.55f;
constexpr float kHurriedMinimumContactSpeedAi = 3.0f;
constexpr float kHurriedPassingSpeedDeltaAi = 2.0f;
constexpr float kHurriedImpactReleaseToleranceAi = 0.5f;
constexpr unsigned int kStaticBlockConfirmationMs = 250;
constexpr float kStaticProgressDistance = 0.35f;
constexpr float kCurveMinimumSegmentLength = 3.0f;
constexpr float kCurveLateralAcceleration = 3.25f;
constexpr float kCurveReactionMargin = 7.0f;
constexpr float kSteerLimitStart = 0.08f;
constexpr float kSteerLimitFull = 0.50f;
constexpr float kStraightLaneAngle = 0.10f;
constexpr float kOncomingDirectionDotThreshold = -0.75f;
constexpr float kOncomingLateralSafetyMargin = 0.75f;
constexpr float kParallelDirectionDotThreshold = 0.80f;
constexpr float kParallelLateralSafetyMargin = 0.60f;
constexpr float kParkedDirectionDotThreshold = 0.75f;
constexpr float kParkedLateralSafetyMargin = 0.60f;
constexpr float kParkedMaxSpeedAi = 0.50f;
constexpr float kVehicleHeightSeparation = 2.0f;
constexpr float kRayPriorityMinLookAhead = 12.0f;
constexpr float kRayPriorityMaxLookAhead = 30.0f;
constexpr float kRayPriorityMaxPairDistance = 50.0f;
constexpr float kRayPriorityMaxHeightDifference = 3.0f;
constexpr float kRayPriorityTieDistance = 1.0f;
constexpr float kRayPrioritySteerTieThreshold = 0.03f;
constexpr float kRayPrioritySameFlowDot = 0.85f;
constexpr float kRayPriorityBodyClearance = 1.5f;
constexpr unsigned int kRayPriorityMinimumHoldMs = 1500;
constexpr unsigned int kRayPriorityClearConfirmationMs = 250;

struct Config {
    bool enabled = true;
    bool debugMode = false;
    float debugTextScale = 0.25f;
    bool useRealPhysics = true;
    bool fixGroundStuck = true;
    bool onlyInNormalDrivingStyle = false;
    float turningSpeedDecrease = 15.0f;
    float trafficCruiseSpeedMultiplier = 1.0f;
    float frontMultDist = 100.0f;
    float sidesSpeedOffsetDiv = 70.0f;
    float checkGroundHeight = 0.5f;
    float finalGroundHeight = 0.5f;
    float heightDiffLimit = 3.0f;
    int emergencyVelocityIntervention = 1;
#if defined(GTASA)
    float slowTrafficSpeedThreshold = 10.0f;
    float slowTrafficSpeedMultiplier = 2.0f;
#endif
};

enum class PlayerProcessingMode {
    None,
    ExternalAutopilot,
    DebugManual
};

struct VehicleState {
    bool runGroundStuckFix = true;
    bool sourceCruiseSpeedInitialized = false;
    bool lastAppliedCruiseSpeedValid = false;
    bool movementSignatureInitialized = false;
    bool forwardProbeActive = false;
    bool forwardBlocked = false;
    bool forwardStaticObstacle = false;
    bool forwardDynamicObstacle = false;
    bool forwardPedObstacle = false;
    bool emergencyBrakeActive = false;
    bool hurriedImpactLimitActive = false;
    bool reverseEmergencyBrakeActive = false;
    bool reverseProbeActive = false;
    bool reverseBlocked = false;
    bool nativeStuckCheckArmed = false;
    bool rayPriorityYieldActive = false;
    bool rayPriorityYieldRequested = false;
    bool debugProcessed = false;
    unsigned int forwardClearSince = 0;
    unsigned int forwardBlockedSince = 0;
    unsigned int forwardClearFrames = 0;
    unsigned int rayPriorityClearSince = 0;
    unsigned int rayPriorityHoldUntil = 0;
    unsigned int staticBlockSince = 0;
    unsigned int lastAppliedCruiseSpeed = 0;
    unsigned int sourceCruiseSpeed = 0;
    platform::SpeedSourceKind sourceCruiseSpeedKind =
        platform::SpeedSourceKind::None;
    unsigned int lastProcessedFrame = std::numeric_limits<unsigned int>::max();
    unsigned char forwardGroundFailureFrames[3] = {};
    unsigned char reverseGroundFailureFrames[3] = {};
    std::uint64_t movementSignature = 0;
    float forwardNearestHitDistance = std::numeric_limits<float>::max();
    float forwardObstacleSpeed = 0.0f;
    float desiredCruiseSpeed = 0.0f;
    float debugCurrentSpeed = 0.0f;
    float debugSourceSpeed = 0.0f;
    float debugCruiseSpeed = 0.0f;
    float debugRoadMultiplier = 1.0f;
    float debugCurveLimit = std::numeric_limits<float>::max();
    float debugSteerLimit = std::numeric_limits<float>::max();
    float rayPriorityConflictDistance = std::numeric_limits<float>::max();
    CVector rayPriorityConflictPoint = {};
    CVector rayPriorityPartnerDirection = {};
    CVehicle *rayPriorityPartner = nullptr;
    CEntity *staticBlocker = nullptr;
    CVector staticBlockAnchor = {};
    PlayerProcessingMode playerMode = PlayerProcessingMode::None;

    explicit VehicleState(CVehicle *) {}
};

struct CruiseProfile {
    float baseSpeed = 0.0f;
    float cruiseSpeed = 0.0f;
    float roadSpeedMultiplier = 1.0f;
};

struct DebugLine {
    RwIm3DVertex vertices[2] = {};
};

Config cfg;
VehicleExtendedData<VehicleState> vehicleState;
std::ofstream logFile;
std::vector<DebugLine> debugLines;
bool playerAutoDrive = false;
std::uint32_t randomState = 0xA341316Cu;

float RandomFloat(float minimum, float maximum) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    const float unit = static_cast<float>(randomState & 0x00FFFFFFu) /
        static_cast<float>(0x01000000u);
    return minimum + (maximum - minimum) * unit;
}

std::string ModuleFilePath() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&ModuleFilePath),
            &module)) {
        module = nullptr;
    }

    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::string(path, length);
}

std::string DirectoryFromPath(const std::string &path) {
    const std::string::size_type separator = path.find_last_of("\\/");
    return separator == std::string::npos ? std::string{} : path.substr(0, separator);
}

std::string JoinPath(const std::string &directory, const char *fileName) {
    return directory.empty() ? std::string(fileName) : directory + "\\" + fileName;
}

bool FileExists(const std::string &path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string CurrentDirectory() {
    char path[MAX_PATH] = {};
    const DWORD length = GetCurrentDirectoryA(MAX_PATH, path);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::string(path, length);
}

std::string ResolveConfigPath() {
    const std::string moduleDirectory = DirectoryFromPath(ModuleFilePath());
    const std::string moduleConfig = JoinPath(moduleDirectory, platform::IniFileName());
    if (FileExists(moduleConfig)) {
        return moduleConfig;
    }

    const std::string currentDirectoryConfig = JoinPath(CurrentDirectory(), platform::IniFileName());
    if (FileExists(currentDirectoryConfig)) {
        return currentDirectoryConfig;
    }

    return moduleConfig;
}

const std::string &ConfigPath() {
    static const std::string path = ResolveConfigPath();
    return path;
}

std::string LogPath() {
    return JoinPath(DirectoryFromPath(ConfigPath()), platform::LogFileName());
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](unsigned char c) { return !isSpace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](unsigned char c) { return !isSpace(c); }).base(), value.end());
    return value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool TryReadBool(const char *key, bool &result) {
    char buffer[32] = {};
    GetPrivateProfileStringA("Settings", key, "", buffer, sizeof(buffer), ConfigPath().c_str());
    const std::string value = ToLower(Trim(buffer));
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        result = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        result = false;
        return true;
    }
    return false;
}

bool ReadBool(const char *key, bool fallback) {
    bool result = fallback;
    return TryReadBool(key, result) ? result : fallback;
}

float ReadFloat(const char *key, float fallback) {
    char buffer[64] = {};
    char fallbackText[64] = {};
    std::snprintf(fallbackText, sizeof(fallbackText), "%.6f", fallback);
    GetPrivateProfileStringA("Settings", key, fallbackText, buffer, sizeof(buffer), ConfigPath().c_str());

    char *end = nullptr;
    const float value = std::strtof(buffer, &end);
    if (end == buffer || !std::isfinite(value)) {
        return fallback;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    return *end == '\0' ? value : fallback;
}

int ReadInt(const char *key, int fallback) {
    char buffer[32] = {};
    char fallbackText[32] = {};
    std::snprintf(fallbackText, sizeof(fallbackText), "%d", fallback);
    GetPrivateProfileStringA("Settings", key, fallbackText, buffer, sizeof(buffer), ConfigPath().c_str());
    char *end = nullptr;
    const long value = std::strtol(buffer, &end, 10);
    if (end == buffer) return fallback;
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
    return *end == '\0' ? static_cast<int>(value) : fallback;
}

void Log(const char *format, ...) {
    if (!cfg.debugMode || !logFile.is_open()) {
        return;
    }

    char buffer[640] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Logging starts from the ASI constructor. GetTickCount is safe there;
    // the game's CTimer storage is used later by the actual traffic logic.
    logFile << static_cast<unsigned int>(GetTickCount())
            << " " << buffer << "\n";
}

void SanitizeConfig(Config &config) {
    config.debugTextScale = std::clamp(config.debugTextScale, 0.10f, 2.00f);
    config.turningSpeedDecrease = std::clamp(config.turningSpeedDecrease, 0.0f, 100.0f);
    config.trafficCruiseSpeedMultiplier = std::clamp(
        config.trafficCruiseSpeedMultiplier, 0.1f, 10.0f);
    config.frontMultDist = std::clamp(config.frontMultDist, 1.0f, 1000.0f);
    config.sidesSpeedOffsetDiv = std::clamp(config.sidesSpeedOffsetDiv, 1.0f, 1000.0f);
    config.checkGroundHeight = std::clamp(config.checkGroundHeight, 0.0f, 10.0f);
    config.finalGroundHeight = std::clamp(config.finalGroundHeight, 0.0f, 10.0f);
    config.heightDiffLimit = std::clamp(config.heightDiffLimit, 0.1f, 50.0f);
    config.emergencyVelocityIntervention = std::clamp(config.emergencyVelocityIntervention, 0, 2);
#if defined(GTASA)
    config.slowTrafficSpeedThreshold = std::clamp(
        config.slowTrafficSpeedThreshold, 0.0f, platform::CruiseStorageMaximum());
    config.slowTrafficSpeedMultiplier = std::clamp(
        config.slowTrafficSpeedMultiplier, 0.0f, 10.0f);
#endif
}

void LoadConfig(bool initializeLogging) {
    Config next = initializeLogging ? Config{} : cfg;
    next.enabled = ReadBool("Enabled", next.enabled);
    next.debugMode = ReadBool("DebugMode", next.debugMode);
    next.debugTextScale = ReadFloat("DebugTextScale", next.debugTextScale);
    next.useRealPhysics = ReadBool("UseRealPhysics", next.useRealPhysics);
    next.fixGroundStuck = ReadBool("FixGroundStuck", next.fixGroundStuck);
    next.onlyInNormalDrivingStyle = ReadBool("OnlyInNormalDrivingStyle", next.onlyInNormalDrivingStyle);
    next.turningSpeedDecrease = ReadFloat("TurningSpeedDecrease", next.turningSpeedDecrease);
    next.trafficCruiseSpeedMultiplier = ReadFloat(
        "TrafficCruiseSpeedMultiplier", next.trafficCruiseSpeedMultiplier);
    next.frontMultDist = ReadFloat("FrontMultDist", next.frontMultDist);
    next.sidesSpeedOffsetDiv = ReadFloat("SidesSpeedOffsetDiv", next.sidesSpeedOffsetDiv);
    next.checkGroundHeight = ReadFloat("CheckGroundHeight", next.checkGroundHeight);
    next.finalGroundHeight = ReadFloat("FinalGroundHeight", next.finalGroundHeight);
    next.heightDiffLimit = ReadFloat("HeightDiffLimit", next.heightDiffLimit);
    next.emergencyVelocityIntervention = ReadInt(
        "EmergencyVelocityIntervention", next.emergencyVelocityIntervention);
#if defined(GTASA)
    next.slowTrafficSpeedThreshold = ReadFloat(
        "SlowTrafficSpeedThreshold", next.slowTrafficSpeedThreshold);
    next.slowTrafficSpeedMultiplier = ReadFloat(
        "SlowTrafficSpeedMultiplier", next.slowTrafficSpeedMultiplier);
#endif
    SanitizeConfig(next);
    cfg = next;

    bool logOpenedNow = false;
    if (cfg.debugMode && !logFile.is_open()) {
        const std::ios::openmode mode = std::ios::out |
            (initializeLogging ? std::ios::trunc : std::ios::app);
        logFile.open(LogPath(), mode);
        logFile.setf(std::ios::unitbuf);
        logOpenedNow = logFile.is_open();
    } else if (!cfg.debugMode && logFile.is_open()) {
        logFile.close();
    }

    if (cfg.debugMode && (initializeLogging || logOpenedNow)) {
        Log("%s initialized", platform::ModName());
        Log("ini=%s", ConfigPath().c_str());
        Log("Enabled=%d DebugMode=%d UseRealPhysics=%d FixGroundStuck=%d OnlyInNormalDrivingStyle=%d",
            cfg.enabled ? 1 : 0,
            cfg.debugMode ? 1 : 0,
            cfg.useRealPhysics ? 1 : 0,
            cfg.fixGroundStuck ? 1 : 0,
            cfg.onlyInNormalDrivingStyle ? 1 : 0);
    }
}

void ReloadConfigIfDue() {
    // Outside DebugMode the INI is intentionally not polled every frame.
    // restartGameEvent reloads it when a save/new game is loaded.
    if (cfg.debugMode) {
        LoadConfig(false);
    }
}

unsigned int GetCruiseSpeed(CVehicle *vehicle) {
    return platform::GetCruiseSpeed(vehicle);
}

void SetCruiseSpeed(CVehicle *vehicle, unsigned int speed) {
    platform::SetCruiseSpeed(vehicle, speed);
}

CVector WorldCoordWithOffset(CEntity *entity, const CVector &offset) {
    return platform::WorldCoordWithOffset(entity, offset);
}

bool GetEntityDimensions(CEntity *entity, CVector &outMin, CVector &outMax) {
    if (!entity) {
        return false;
    }
    CColModel *colModel = entity->GetColModel();
    if (!colModel) {
        return false;
    }
    outMin = colModel->m_boundBox.m_vecMin;
    outMax = colModel->m_boundBox.m_vecMax;
    return true;
}

float FindGroundZ(const CVector &coord, bool &foundGround) {
    return platform::FindGroundZ(coord, foundGround);
}

void SetVehiclePosition(CVehicle *vehicle, const CVector &position) {
    platform::SetVehiclePosition(vehicle, position);
}

float SpeedKmh(CVehicle *vehicle) {
    return vehicle->m_vecMoveSpeed.Magnitude() * 50.0f * 3.6f;
}

float SignedForwardSpeed(CVehicle *vehicle) {
    const CVector &forward = vehicle->GetForward();
    return vehicle->m_vecMoveSpeed.x * forward.x +
           vehicle->m_vecMoveSpeed.y * forward.y +
           vehicle->m_vecMoveSpeed.z * forward.z;
}

float ForwardSpeedAi(CVehicle *vehicle) {
    const float speed = SignedForwardSpeed(vehicle) * kAiSpeedScale;
    return std::isfinite(speed) ? std::max(speed, 0.0f) : 0.0f;
}

bool IsVehicleStopped(CVehicle *vehicle) {
    return vehicle && plugin::Command<COMMAND_IS_CAR_STOPPED>(vehicle);
}



bool IsSupportedTrafficVehicle(CVehicle *vehicle) {
    return platform::IsSupportedGroundVehicle(vehicle);
}

bool IsTwoWheeler(CVehicle *vehicle) {
    return platform::IsTwoWheeler(vehicle);
}

bool IsAutomobileForPlacement(CVehicle *vehicle) {
    return platform::IsAutomobileForPlacement(vehicle);
}

bool CanSwitchToRealPhysics(CVehicle *vehicle) {
    return platform::CanSwitchToRealPhysics(vehicle);
}

bool IsTaxiVehicle(CVehicle *vehicle) {
    return platform::IsTaxiVehicle(vehicle);
}

bool IsMissionVehicleExcluded(CVehicle *vehicle) {
    if (!platform::IsMissionVehicleExcluded(vehicle)) {
        return false;
    }
    const bool containsPlayer = vehicle && FindPlayerVehicle() == vehicle;
    const bool playerDriver =
        vehicle && vehicle->m_pDriver && vehicle->m_pDriver->IsPlayer();
    // A passenger vehicle remains AI-driven and is always eligible. A mission
    // vehicle driven by the player still requires the export or DebugMode.
    const bool currentPlayerVehicleAllowed = containsPlayer &&
        (!playerDriver || playerAutoDrive || cfg.debugMode);
    return !currentPlayerVehicleAllowed;
}

void UpdateTaxiRoofLight(CVehicle *vehicle) {
    if (!IsTaxiVehicle(vehicle)) {
        return;
    }
    CPed *driver = vehicle->m_pDriver;
    const bool seatedDriver = driver && driver->m_pVehicle == vehicle &&
        vehicle->m_nGettingOutFlags == 0;
    const bool lightOn = cfg.enabled && !IsMissionVehicleExcluded(vehicle) &&
        seatedDriver && vehicle->m_nNumPassengers == 0 &&
        vehicle->m_nNumGettingIn == 0;
    static_cast<CAutomobile *>(vehicle)->SetTaxiLight(lightOn);
}

bool CanProcessVehicle(CVehicle *vehicle) {
    if (!cfg.enabled || !IsSupportedTrafficVehicle(vehicle) || !vehicle->m_pDriver) {
        return false;
    }
    if (IsMissionVehicleExcluded(vehicle)) {
        return false;
    }
    const bool playerDriver = vehicle->m_pDriver->IsPlayer();
    const bool containsPlayer = FindPlayerVehicle() == vehicle;
    const bool passengerPlayerVehicle = containsPlayer && !playerDriver;
    const bool playerVehicleOptedIn = containsPlayer &&
        (passengerPlayerVehicle || playerAutoDrive || cfg.debugMode);
    if (!platform::CanProcessStatus(
            vehicle,
            playerDriver || playerVehicleOptedIn)) {
        return false;
    }
    // Every RenderWare-era GTA has special player-vehicle paths. Do not assume
    // that an NPC driver makes a passenger taxi an ordinary traffic vehicle.
    // NPC-driven passenger vehicles stay in the ordinary AI path. The narrow
    // boolean export and DebugMode opt in a player-driven vehicle.
    if (containsPlayer && !playerVehicleOptedIn) {
        return false;
    }
    return true;
}

bool ResolveCruiseProfile(CVehicle *vehicle, VehicleState &state, CruiseProfile &profile) {
    const bool playerDriver = vehicle->m_pDriver && vehicle->m_pDriver->IsPlayer();
    const bool exportedPlayerVehicle = playerAutoDrive && FindPlayerVehicle() == vehicle;
    const PlayerProcessingMode playerMode = exportedPlayerVehicle
        ? PlayerProcessingMode::ExternalAutopilot
        : (playerDriver && cfg.debugMode
            ? PlayerProcessingMode::DebugManual
            : PlayerProcessingMode::None);
    const bool playerModeChanged = state.playerMode != playerMode;
    state.playerMode = playerMode;
    const platform::SpeedObservation observation =
        platform::ObserveRequestedSpeed(vehicle);
    const unsigned int observed = observation.value;
    const std::uint64_t signature = platform::MovementSignature(vehicle);
    const bool signatureChanged = state.movementSignatureInitialized &&
        signature != state.movementSignature;
    const bool externalWrite = !state.lastAppliedCruiseSpeedValid ||
        observed != state.lastAppliedCruiseSpeed;
#if defined(GTASA)
    // The engine may lower CruiseSpeed while executing a task. The autopilot
    // writes its selected task speed to both fields, so only a matching pair
    // represents a new user-selected speed.
    const bool playerTaskSpeedWrite =
        state.playerMode != PlayerProcessingMode::ExternalAutopilot ||
        (std::isfinite(vehicle->m_autoPilot.m_fMaxTrafficSpeed) &&
         std::fabs(vehicle->m_autoPilot.m_fMaxTrafficSpeed -
             static_cast<float>(observed)) < 0.6f);
#else
    const bool playerTaskSpeedWrite = true;
#endif

    if (!state.movementSignatureInitialized || signatureChanged) {
        state.movementSignature = signature;
        state.movementSignatureInitialized = true;
    }

    if (state.playerMode == PlayerProcessingMode::DebugManual) {
        // A manually driven player vehicle has no reliable AI task speed.
        // Use actual forward speed as the live engine demand; an external
        // non-zero cruise write is still accepted when one exists.
        float manualDemand = std::max(ForwardSpeedAi(vehicle), 1.0f);
        if (externalWrite && observed > 0) {
            manualDemand = static_cast<float>(observed);
        }
        if (!state.sourceCruiseSpeedInitialized) {
            state.sourceCruiseSpeed = static_cast<unsigned int>(std::clamp(
                std::round(manualDemand), 1.0f, platform::CruiseStorageMaximum()));
            state.sourceCruiseSpeedKind = platform::SpeedSourceKind::MovementTask;
            state.sourceCruiseSpeedInitialized = true;
        }
    } else {
        if (observation.kind == platform::SpeedSourceKind::None) {
            // WAIT/REVERSE temporarily hides the task's cruise field in all
            // three games. Keep the last real task demand until the action
            // finishes instead of forgetting the vehicle's source speed.
            if (!state.sourceCruiseSpeedInitialized ||
                !platform::HasTemporaryAction(vehicle)) {
                return false;
            }
        }

        // Ambient NPC speed and movement-task speed are intentionally separate
        // sources. The platform adapter decides which engine field is authoritative.
        // The only trustworthy source update is a write which did not come
        // from RTF itself. This covers ambient NPC changes and every movement
        // task without requiring another mod to pass speed through an export.
        if (observation.kind != platform::SpeedSourceKind::None &&
            !state.sourceCruiseSpeedInitialized) {
            if (observed == 0) {
                state.sourceCruiseSpeedInitialized = false;
                return false;
            }
            state.sourceCruiseSpeed = observed;
            state.sourceCruiseSpeedKind = observation.kind;
            state.sourceCruiseSpeedInitialized = true;
            Log("source cruise observed vehicle=%p speed=%u kind=%s signature=%llu",
                vehicle,
                observed,
                observation.kind == platform::SpeedSourceKind::AmbientTraffic ? "NPC" : "TASK",
                static_cast<unsigned long long>(signature));
        }

        if (observation.kind != platform::SpeedSourceKind::None &&
            externalWrite && observed > 0 && playerTaskSpeedWrite &&
            (signatureChanged || playerModeChanged ||
             state.playerMode == PlayerProcessingMode::ExternalAutopilot)) {
            const unsigned int previous = state.sourceCruiseSpeed;
            state.sourceCruiseSpeed = observed;
            state.sourceCruiseSpeedKind = observation.kind;
            state.sourceCruiseSpeedInitialized = true;
            if (previous != observed) {
                Log("source cruise changed vehicle=%p speed=%u->%u",
                    vehicle, previous, observed);
            }
        }

        if (!state.sourceCruiseSpeedInitialized) {
            return false;
        }
    }


    const bool scaleNativeTrafficSpeed =
        state.playerMode == PlayerProcessingMode::None &&
        state.sourceCruiseSpeedKind == platform::SpeedSourceKind::AmbientTraffic;
#if defined(GTASA)
    const float slowTrafficMultiplier =
        scaleNativeTrafficSpeed &&
        cfg.slowTrafficSpeedThreshold > 0.0f &&
        cfg.slowTrafficSpeedMultiplier > 0.0f &&
        static_cast<float>(state.sourceCruiseSpeed) < cfg.slowTrafficSpeedThreshold
            ? cfg.slowTrafficSpeedMultiplier
            : 1.0f;
#else
    constexpr float slowTrafficMultiplier = 1.0f;
#endif
    const float sourceMultiplier = scaleNativeTrafficSpeed
        ? cfg.trafficCruiseSpeedMultiplier
        : 1.0f;
    profile.baseSpeed = std::clamp(
        static_cast<float>(state.sourceCruiseSpeed) *
            slowTrafficMultiplier * sourceMultiplier,
        1.0f,
        platform::CruiseStorageMaximum());
    profile.roadSpeedMultiplier = scaleNativeTrafficSpeed
        ? std::clamp(
            platform::RoadSpeedMultiplier(vehicle),
            0.1f,
            10.0f)
        : 1.0f;
    profile.cruiseSpeed = std::clamp(
        profile.baseSpeed * profile.roadSpeedMultiplier,
        1.0f,
        platform::CruiseStorageMaximum());
    return true;
}

void ApplyCruiseSpeed(CVehicle *vehicle, VehicleState &state, float speed) {
    const unsigned int value = static_cast<unsigned int>(
        std::clamp(std::round(speed), 0.0f, platform::CruiseStorageMaximum()));
    SetCruiseSpeed(vehicle, value);
    state.lastAppliedCruiseSpeed = value;
    state.lastAppliedCruiseSpeedValid = true;
}

float NormalizeCruiseSpeed(float speed, float minimum, float maximum) {
    if (maximum < minimum) {
        maximum = minimum;
    }
    return std::clamp(speed, minimum, maximum);
}

void ApplyDesiredCruiseSpeed(
    CVehicle *vehicle,
    VehicleState &state,
    const CruiseProfile &profile,
    float desiredSpeed) {
    desiredSpeed = NormalizeCruiseSpeed(desiredSpeed, 0.0f, profile.cruiseSpeed);
    state.desiredCruiseSpeed = desiredSpeed;
    ApplyCruiseSpeed(vehicle, state, desiredSpeed);
}

void DrawDebugText(
    CVehicle *vehicle,
    const CVector &modelMax,
    float &zOffset,
    float fontSizeMultiplier,
    const char *format,
    ...) {
    if (!cfg.debugMode ||
        (TheCamera.GetPosition() - vehicle->GetPosition()).Magnitude() > kDebugTextDistance) {
        return;
    }

    char text[160] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    const CVector worldPosition = WorldCoordWithOffset(
        vehicle, {0.0f, 0.0f, modelMax.z + zOffset + 0.5f});
    RwV3d screenPosition = {};
    float screenWidth = 0.0f;
    float screenHeight = 0.0f;
    zOffset += kDebugLineHeight;

    if (!platform::CalcScreenCoors(worldPosition, screenPosition, screenWidth, screenHeight)) {
        return;
    }

    const float fontScaleX =
        (screenWidth / 75.0f) * fontSizeMultiplier * cfg.debugTextScale;
    const float fontScaleY =
        (screenHeight / 75.0f) * fontSizeMultiplier * cfg.debugTextScale;
    if (fontScaleX <= 0.1f || fontScaleY <= 0.1f) {
        return;
    }

    platform::SetupDebugFont(fontScaleX);
    CFont::SetFontStyle(2);
    CFont::SetColor({255, 255, 255, 255});
    CFont::SetScale(fontScaleX, fontScaleY);
    CFont::PrintString(screenPosition.x, screenPosition.y, text);
}

void QueueDebugLine(const CVector &start, const CVector &end, bool obstacle) {
    if (!cfg.debugMode) {
        return;
    }

    DebugLine line;
    RwIm3DVertexSetPos(&line.vertices[0], start.x, start.y, start.z);
    RwIm3DVertexSetPos(&line.vertices[1], end.x, end.y, end.z);
    const unsigned char red = obstacle ? 255 : 0;
    const unsigned char green = obstacle ? 0 : 255;
    RwIm3DVertexSetRGBA(&line.vertices[0], red, green, 0, 255);
    RwIm3DVertexSetRGBA(&line.vertices[1], red, green, 0, 255);
    debugLines.push_back(line);
}

void DrawDebugLines() {
    if (debugLines.empty()) {
        return;
    }

    unsigned int previousZTest = TRUE;
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &previousZTest);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void *>(FALSE));
    for (const DebugLine &line : debugLines) {
        if (RwIm3DTransform(const_cast<RwIm3DVertex *>(line.vertices), 2, nullptr, 0)) {
            RwIm3DRenderLine(0, 1);
            RwIm3DEnd();
        }
    }
    RwRenderStateSet(
        rwRENDERSTATEZTESTENABLE,
        reinterpret_cast<void *>(static_cast<uintptr_t>(previousZTest)));
    debugLines.clear();
}

bool GroundExistsNear(CVector coord, float maxZ) {
    coord.z += maxZ;
    bool foundGround = false;
    FindGroundZ(coord, foundGround);
    return foundGround;
}

bool RepositionVehicle(CVehicle *vehicle) {
    if (!IsAutomobileForPlacement(vehicle)) {
        return false;
    }

    const CVector original = vehicle->GetPosition();
    for (int attempt = 0; attempt < 3; ++attempt) {
        CVector candidate = original;
        candidate.x += RandomFloat(-0.5f, 0.5f);
        candidate.y += RandomFloat(-0.5f, 0.5f);
        candidate.z += 2.0f;

        bool foundGround = false;
        const float groundZ = FindGroundZ(candidate, foundGround);
        if (!foundGround) {
            continue;
        }

        candidate.z = groundZ + 0.5f;
        SetVehiclePosition(vehicle, candidate);
        static_cast<CAutomobile *>(vehicle)->PlaceOnRoadProperly();
        Log("repositioned vehicle=%p", vehicle);
        return true;
    }
    return false;
}

bool FixGroundStuck(CVehicle *vehicle, const CVector &modelMin, const CVector &modelMax) {
    if (!IsAutomobileForPlacement(vehicle)) {
        return false;
    }

    CVector rear = WorldCoordWithOffset(vehicle, {0.0f, modelMin.y * 0.9f, 0.0f});
    bool foundRearGround = false;
    FindGroundZ(rear, foundRearGround);
    if (!foundRearGround && GroundExistsNear(rear, modelMax.z)) {
        return RepositionVehicle(vehicle);
    }

    CVector front = WorldCoordWithOffset(vehicle, {0.0f, modelMax.y * 0.9f, 0.0f});
    bool foundFrontGround = false;
    FindGroundZ(front, foundFrontGround);
    if (!foundFrontGround && GroundExistsNear(front, modelMax.z)) {
        return RepositionVehicle(vehicle);
    }
    return false;
}

bool VehicleYieldsTo(CEntity *entity, CVehicle *priorityVehicle);
bool IsSafelySeparatedTrafficVehicle(CEntity *entity, CVehicle *vehicle);

bool ProbeLine(
    CVehicle *vehicle,
    const CVector &start,
    const CVector &end,
    CColPoint &colPoint,
    CEntity *&entity) {
    const CVector ray = end - start;
    const float length = ray.Magnitude();
    if (!std::isfinite(length) || length < 0.001f) return false;
    const CVector direction = ray * (1.0f / length);
    CVector cursor = start;
    for (int pass = 0; pass < 4; ++pass) {
        if (!platform::ProcessLineOfSight(
                vehicle, cursor, end, colPoint, entity)) return false;
#ifdef GTASA
        const bool ownTowVehicle = entity && entity->m_nType == ENTITY_TYPE_VEHICLE &&
            (entity == vehicle->m_pTrailer || entity == vehicle->m_pTractor);
#else
        const bool ownTowVehicle = false;
#endif
        if (!ownTowVehicle && !VehicleYieldsTo(entity, vehicle) &&
            !IsSafelySeparatedTrafficVehicle(entity, vehicle)) return true;

        float advance = (colPoint.m_vecPoint - start).Magnitude() + 0.5f;
        CVector ignoredMin, ignoredMax;
        if (GetEntityDimensions(entity, ignoredMin, ignoredMax)) {
            const CVector relative = entity->GetPosition() - start;
            const float centreAlongRay = relative.x * direction.x +
                relative.y * direction.y + relative.z * direction.z;
            const float radius = std::sqrt(
                std::max(std::abs(ignoredMin.x), std::abs(ignoredMax.x)) *
                    std::max(std::abs(ignoredMin.x), std::abs(ignoredMax.x)) +
                std::max(std::abs(ignoredMin.y), std::abs(ignoredMax.y)) *
                    std::max(std::abs(ignoredMin.y), std::abs(ignoredMax.y)));
            advance = std::max(advance, centreAlongRay + radius + 0.25f);
        }
        if (advance >= length) return false;
        cursor = start + direction * advance;
    }
    return false;
}

enum class ProbeEndStatus : unsigned char {
    Ready,
    MissingGround,
    UnsafeHeight
};

ProbeEndStatus PrepareProbeEnd(CVector &end, float heightDiffLimit) {
    CVector test = end;
    test.z += cfg.checkGroundHeight;
    bool foundGround = false;
    const float groundZ = FindGroundZ(test, foundGround);
    if (!foundGround) {
        return ProbeEndStatus::MissingGround;
    }
    if (std::abs(groundZ - end.z) > heightDiffLimit) {
        return ProbeEndStatus::UnsafeHeight;
    }
    end.z = groundZ + cfg.finalGroundHeight;
    return ProbeEndStatus::Ready;
}

float ObstacleForwardSpeedAi(CEntity *entity, CVehicle *vehicle) {
    if (!entity || !vehicle) {
        return 0.0f;
    }

    CVector velocity;
    if (entity->m_nType == ENTITY_TYPE_VEHICLE) {
        velocity = static_cast<CVehicle *>(entity)->m_vecMoveSpeed;
    } else if (entity->m_nType == ENTITY_TYPE_PED) {
        velocity = static_cast<CPed *>(entity)->m_vecMoveSpeed;
    } else {
        return 0.0f;
    }

    const CVector &forward = vehicle->GetForward();
    const float speed = (velocity.x * forward.x + velocity.y * forward.y +
        velocity.z * forward.z) * kAiSpeedScale;
    return std::isfinite(speed) ? speed : 0.0f;
}

float EstimatedBrakeDeceleration(CVehicle *vehicle) {
    if (!vehicle || !vehicle->m_pHandlingData) {
        return 4.0f;
    }
    const float handlingDeceleration = platform::BrakeDeceleration(vehicle) *
        kGameFramesPerSecond * kGameFramesPerSecond;
    if (!std::isfinite(handlingDeceleration) || handlingDeceleration <= 0.0f) {
        return 4.0f;
    }
    return std::clamp(
        handlingDeceleration * kHandlingBrakeEfficiency,
        kMinimumBrakeDeceleration,
        kMaximumBrakeDeceleration);
}

float ComfortableObstacleCruiseSpeedAi(
    CVehicle *vehicle,
    float distance,
    float obstacleSpeedAi,
    bool pedestrian) {
    if (!std::isfinite(distance)) {
        return std::numeric_limits<float>::max();
    }
    const float deceleration = std::clamp(
        EstimatedBrakeDeceleration(vehicle) * kComfortableBrakeScale,
        kComfortableBrakeMin,
        kComfortableBrakeMax);
    const float margin = kComfortableStopMargin +
        (pedestrian ? kEmergencyPedMargin : 0.0f);
    const float availableDistance = std::max(distance - margin, 0.0f);
    const float safeRelativeSpeedWorld = std::sqrt(
        2.0f * deceleration * availableDistance);
    const float safeRelativeSpeedAi = safeRelativeSpeedWorld *
        (kAiSpeedScale / kGameFramesPerSecond);
    return std::max(obstacleSpeedAi, 0.0f) + safeRelativeSpeedAi;
}

bool IsStaticObstacle(CEntity *entity) {
    return entity && (entity->m_nType == ENTITY_TYPE_BUILDING ||
                      entity->m_nType == ENTITY_TYPE_OBJECT);
}

bool IsDynamicObstacle(CEntity *entity) {
    return entity && (entity->m_nType == ENTITY_TYPE_VEHICLE ||
                      entity->m_nType == ENTITY_TYPE_PED);
}

struct RayPriorityIntersection {
    bool found = false;
    float firstDistance = std::numeric_limits<float>::max();
    float secondDistance = std::numeric_limits<float>::max();
    CVector point = {};
};

bool CanParticipateInRayPriority(CVehicle *vehicle) {
    return vehicle && CanProcessVehicle(vehicle) &&
           platform::IsNormalDrivingStyle(vehicle) &&
           !platform::IsStoppingMission(vehicle) &&
           !platform::IsReverseAction(vehicle);
}

float DirectionDot2D(CVehicle *first, CVehicle *second) {
    const CVector &a = first->GetForward();
    const CVector &b = second->GetForward();
    const float aLength = std::sqrt(a.x * a.x + a.y * a.y);
    const float bLength = std::sqrt(b.x * b.x + b.y * b.y);
    if (aLength < 0.001f || bLength < 0.001f) return 1.0f;
    return (a.x * b.x + a.y * b.y) / (aLength * bLength);
}

CVector SteeredProbeEndOffset(float lateralOffset, float frontOffset,
                              float distance, float steerAngle, float height) {
    const float yaw = -steerAngle;
    return {
        lateralOffset + std::sin(yaw) * distance,
        frontOffset + std::cos(yaw) * distance,
        height
    };
}

int BuildFrontProbeRays(
    CVehicle *vehicle,
    const CVector &modelMin,
    const CVector &modelMax,
    float distance,
    float speedKmh,
    CVector *start,
    CVector *end) {
    const float frontHeight = std::clamp(modelMin.z * 0.4f, -1.0f, 1.0f);
    const float frontHeightBonus = IsTwoWheeler(vehicle) ? 0.35f : 0.15f;
    const int probeCount = IsTwoWheeler(vehicle) ? 1 : 3;

    start[0] = WorldCoordWithOffset(vehicle,
        {0.0f, modelMax.y, frontHeight + frontHeightBonus});
    end[0] = WorldCoordWithOffset(vehicle,
        SteeredProbeEndOffset(0.0f, modelMax.y, distance,
                              vehicle->m_fSteerAngle,
                              frontHeight / 1.5f + frontHeightBonus * 2.0f));

    if (probeCount == 3) {
        float leftBackOffset = modelMax.y * vehicle->m_fSteerAngle;
        if (leftBackOffset < 0.0f) leftBackOffset = 0.0f;
        start[1] = WorldCoordWithOffset(vehicle,
            {modelMin.x, modelMax.y - leftBackOffset,
             frontHeight + frontHeightBonus});
        end[1] = WorldCoordWithOffset(vehicle,
            SteeredProbeEndOffset(modelMin.x - speedKmh / cfg.sidesSpeedOffsetDiv,
                                  modelMax.y, distance,
                                  vehicle->m_fSteerAngle, frontHeight / 1.5f));

        float rightBackOffset = modelMax.y * vehicle->m_fSteerAngle;
        if (rightBackOffset > 0.0f) rightBackOffset = 0.0f;
        start[2] = WorldCoordWithOffset(vehicle,
            {modelMax.x, modelMax.y + rightBackOffset,
             frontHeight + frontHeightBonus});
        end[2] = WorldCoordWithOffset(vehicle,
            SteeredProbeEndOffset(modelMax.x + speedKmh / cfg.sidesSpeedOffsetDiv,
                                  modelMax.y, distance,
                                  vehicle->m_fSteerAngle, frontHeight / 1.5f));
    }
    return probeCount;
}

bool IntersectPriorityProbeSegments(
    const CVector &firstStart,
    const CVector &firstEnd,
    const CVector &secondStart,
    const CVector &secondEnd,
    float &firstDistance,
    float &secondDistance) {
    const float firstX = firstEnd.x - firstStart.x;
    const float firstY = firstEnd.y - firstStart.y;
    const float secondX = secondEnd.x - secondStart.x;
    const float secondY = secondEnd.y - secondStart.y;
    const float denominator = firstX * secondY - firstY * secondX;
    if (std::abs(denominator) < 0.0001f) return false;

    const float deltaX = secondStart.x - firstStart.x;
    const float deltaY = secondStart.y - firstStart.y;
    const float firstT = (deltaX * secondY - deltaY * secondX) / denominator;
    const float secondT = (deltaX * firstY - deltaY * firstX) / denominator;
    if (firstT < 0.0f || firstT > 1.0f ||
        secondT < 0.0f || secondT > 1.0f) return false;

    const float firstZ = firstStart.z + (firstEnd.z - firstStart.z) * firstT;
    const float secondZ = secondStart.z + (secondEnd.z - secondStart.z) * secondT;
    if (std::abs(firstZ - secondZ) > kRayPriorityMaxHeightDifference) return false;

    firstDistance = std::sqrt(firstX * firstX + firstY * firstY) * firstT;
    secondDistance = std::sqrt(secondX * secondX + secondY * secondY) * secondT;
    return true;
}

RayPriorityIntersection FindCompactRayPriorityIntersection(
    CVehicle *first,
    CVehicle *second) {
    RayPriorityIntersection result;
    CVector firstMin, firstMax, secondMin, secondMax;
    if (!GetEntityDimensions(first, firstMin, firstMax) ||
        !GetEntityDimensions(second, secondMin, secondMax)) return result;

    CVector firstStart[3] = {}, firstEnd[3] = {};
    CVector secondStart[3] = {}, secondEnd[3] = {};
    const float firstLookAhead = std::clamp(
        std::max(first->m_vecMoveSpeed.Magnitude() * cfg.frontMultDist,
                 kRayPriorityMinLookAhead),
        kRayPriorityMinLookAhead, kRayPriorityMaxLookAhead);
    const float secondLookAhead = std::clamp(
        std::max(second->m_vecMoveSpeed.Magnitude() * cfg.frontMultDist,
                 kRayPriorityMinLookAhead),
        kRayPriorityMinLookAhead, kRayPriorityMaxLookAhead);
    const int firstCount = BuildFrontProbeRays(
        first, firstMin, firstMax, firstLookAhead, SpeedKmh(first),
        firstStart, firstEnd);
    const int secondCount = BuildFrontProbeRays(
        second, secondMin, secondMax, secondLookAhead, SpeedKmh(second),
        secondStart, secondEnd);

    float bestCombinedDistance = std::numeric_limits<float>::max();
    for (int firstIndex = 0; firstIndex < firstCount; ++firstIndex) {
        for (int secondIndex = 0; secondIndex < secondCount; ++secondIndex) {
            float firstDistance = 0.0f;
            float secondDistance = 0.0f;
            if (!IntersectPriorityProbeSegments(
                    firstStart[firstIndex], firstEnd[firstIndex],
                    secondStart[secondIndex], secondEnd[secondIndex],
                    firstDistance, secondDistance)) continue;
            const float combinedDistance = firstDistance + secondDistance;
            if (combinedDistance >= bestCombinedDistance) continue;
            bestCombinedDistance = combinedDistance;
            result.found = true;
            result.firstDistance = firstDistance;
            result.secondDistance = secondDistance;
            const CVector firstRay = firstEnd[firstIndex] - firstStart[firstIndex];
            const float firstLength = firstRay.Magnitude();
            if (firstLength > 0.001f) {
                result.point = firstStart[firstIndex] +
                    firstRay * (firstDistance / firstLength);
            }
        }
    }
    return result;
}

void RequestCompactRayPriorityYield(
    CVehicle *vehicle,
    CVehicle *partner,
    float conflictDistance,
    const CVector &conflictPoint) {
    // A stopped vehicle may wait for moving traffic, but moving traffic must
    // never receive a yield request whose priority owner is stopped.
    if (!vehicle || !partner || IsVehicleStopped(partner)) return;
    VehicleState &partnerState = vehicleState.Get(partner);
    // A vehicle which is already yielding cannot simultaneously own priority
    // in another pair. This keeps pairwise decisions from forming a deadlock
    // chain around an intersection.
    if (partnerState.rayPriorityYieldRequested ||
        partnerState.rayPriorityYieldActive) return;
    VehicleState &state = vehicleState.Get(vehicle);
    if ((state.rayPriorityYieldRequested || state.rayPriorityYieldActive) &&
        state.rayPriorityPartner == partner) {
        state.rayPriorityYieldRequested = true;
        return;
    }
    if ((partnerState.rayPriorityYieldRequested ||
         partnerState.rayPriorityYieldActive) &&
        partnerState.rayPriorityPartner == vehicle) return;
    if (state.rayPriorityYieldRequested &&
        conflictDistance >= state.rayPriorityConflictDistance) return;
    state.rayPriorityYieldRequested = true;
    state.rayPriorityConflictDistance = conflictDistance;
    const bool newPartner = state.rayPriorityPartner != partner;
    state.rayPriorityPartner = partner;
    if (!state.rayPriorityYieldActive || newPartner) {
        const unsigned int now =
            static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
        state.rayPriorityHoldUntil = now + kRayPriorityMinimumHoldMs;
        state.rayPriorityConflictPoint = conflictPoint;
        state.rayPriorityPartnerDirection = partner->GetForward();
    }
}

void ClearRayPriorityYield(VehicleState &state);

float VehicleBodyRadius2D(CVehicle *vehicle) {
    CVector minimum, maximum;
    if (!GetEntityDimensions(vehicle, minimum, maximum)) return 2.0f;
    return std::sqrt(
        std::max(std::abs(minimum.x), std::abs(maximum.x)) *
            std::max(std::abs(minimum.x), std::abs(maximum.x)) +
        std::max(std::abs(minimum.y), std::abs(maximum.y)) *
            std::max(std::abs(minimum.y), std::abs(maximum.y)));
}

bool RayPriorityPartnerHasCleared(
    const VehicleState &state,
    const std::vector<CVehicle *> &vehicles) {
    CVehicle *partner = state.rayPriorityPartner;
    if (!partner || std::find(vehicles.begin(), vehicles.end(), partner) == vehicles.end())
        return true;
    const CVector relative = partner->GetPosition() - state.rayPriorityConflictPoint;
    const CVector &direction = state.rayPriorityPartnerDirection;
    const float directionLength = std::sqrt(
        direction.x * direction.x + direction.y * direction.y);
    if (directionLength < 0.001f) return false;
    const float passedDistance =
        (relative.x * direction.x + relative.y * direction.y) / directionLength;
    return passedDistance > VehicleBodyRadius2D(partner) + kRayPriorityBodyClearance;
}

void UpdateCompactRayPriorityArbiter() {
    std::vector<CVehicle *> vehicles;
    platform::CollectTrafficVehicles(vehicles);
    for (CVehicle *vehicle : vehicles) {
        VehicleState &state = vehicleState.Get(vehicle);
        if (state.rayPriorityYieldActive &&
            (!state.rayPriorityPartner ||
             std::find(vehicles.begin(), vehicles.end(),
                       state.rayPriorityPartner) == vehicles.end() ||
             IsVehicleStopped(state.rayPriorityPartner))) {
            ClearRayPriorityYield(state);
        }
        state.rayPriorityYieldRequested = false;
    }

    for (std::size_t firstIndex = 0; firstIndex < vehicles.size(); ++firstIndex) {
        CVehicle *first = vehicles[firstIndex];
        if (!CanParticipateInRayPriority(first)) continue;
        for (std::size_t secondIndex = firstIndex + 1;
             secondIndex < vehicles.size(); ++secondIndex) {
            CVehicle *second = vehicles[secondIndex];
            if (!CanParticipateInRayPriority(second)) continue;
            const CVector relative = second->GetPosition() - first->GetPosition();
            if (relative.x * relative.x + relative.y * relative.y >
                    kRayPriorityMaxPairDistance * kRayPriorityMaxPairDistance ||
                std::abs(relative.z) > kRayPriorityMaxHeightDifference) continue;
            if (DirectionDot2D(first, second) >= kRayPrioritySameFlowDot)
                continue;

            const RayPriorityIntersection intersection =
                FindCompactRayPriorityIntersection(first, second);
            if (!intersection.found) continue;

            CVehicle *yielder = nullptr;
            const bool firstStopped = IsVehicleStopped(first);
            const bool secondStopped = IsVehicleStopped(second);
            if (firstStopped && secondStopped) continue;
            if (firstStopped != secondStopped) {
                yielder = firstStopped ? first : second;
            } else if (std::abs(intersection.firstDistance -
                                intersection.secondDistance) <=
                       kRayPriorityTieDistance) {
                const float firstSteer = std::abs(first->m_fSteerAngle);
                const float secondSteer = std::abs(second->m_fSteerAngle);
                if (std::abs(firstSteer - secondSteer) >
                    kRayPrioritySteerTieThreshold)
                    yielder = firstSteer > secondSteer ? first : second;
            }
            if (!yielder) {
                yielder = intersection.firstDistance > intersection.secondDistance
                    ? first : second;
            }
            const float yielderDistance = yielder == first
                ? intersection.firstDistance : intersection.secondDistance;
            RequestCompactRayPriorityYield(
                yielder, yielder == first ? second : first, yielderDistance,
                intersection.point);
        }
    }

    const unsigned int now =
        static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
    for (CVehicle *vehicle : vehicles) {
        VehicleState &state = vehicleState.Get(vehicle);
        if (state.rayPriorityYieldActive && !state.rayPriorityPartner) {
            ClearRayPriorityYield(state);
            continue;
        }
        if (state.rayPriorityYieldRequested) {
            state.rayPriorityYieldActive = true;
            state.rayPriorityClearSince = 0;
        } else if (state.rayPriorityYieldActive) {
            const bool minimumHoldFinished = now >= state.rayPriorityHoldUntil;
            const bool partnerFinished =
                RayPriorityPartnerHasCleared(state, vehicles);
            if (!minimumHoldFinished || !partnerFinished) {
                state.rayPriorityClearSince = 0;
            } else if (state.rayPriorityClearSince == 0)
                state.rayPriorityClearSince = now;
            else if (now - state.rayPriorityClearSince >=
                     kRayPriorityClearConfirmationMs)
                ClearRayPriorityYield(state);
        }
    }
}


void ClearRayPriorityYield(VehicleState &state) {
    state.rayPriorityYieldActive = false;
    state.rayPriorityYieldRequested = false;
    state.rayPriorityClearSince = 0;
    state.rayPriorityHoldUntil = 0;
    state.rayPriorityConflictDistance = std::numeric_limits<float>::max();
    state.rayPriorityConflictPoint = {};
    state.rayPriorityPartnerDirection = {};
    state.rayPriorityPartner = nullptr;
}


bool VehicleYieldsTo(CEntity *entity, CVehicle *priorityVehicle) {
    if (!entity || entity->m_nType != ENTITY_TYPE_VEHICLE) return false;
    CVehicle *other = static_cast<CVehicle *>(entity);
    VehicleState &otherState = vehicleState.Get(other);
    return (otherState.rayPriorityYieldRequested || otherState.rayPriorityYieldActive) &&
           otherState.rayPriorityPartner == priorityVehicle;
}

bool IsSafelySeparatedOncomingVehicle(CEntity *entity, CVehicle *vehicle) {
    if (!entity || !vehicle || entity == vehicle ||
        entity->m_nType != ENTITY_TYPE_VEHICLE) {
        return false;
    }

    CVehicle *other = static_cast<CVehicle *>(entity);
    const CVector &ownForward = vehicle->GetForward();
    const CVector &otherForward = other->GetForward();
    const float ownLength = std::sqrt(ownForward.x * ownForward.x + ownForward.y * ownForward.y);
    const float otherLength = std::sqrt(otherForward.x * otherForward.x + otherForward.y * otherForward.y);
    if (ownLength < 0.001f || otherLength < 0.001f) {
        return false;
    }

    const float directionDot =
        (ownForward.x * otherForward.x + ownForward.y * otherForward.y) /
        (ownLength * otherLength);
    if (directionDot > kOncomingDirectionDotThreshold) {
        return false;
    }

    const CVector relative = other->GetPosition() - vehicle->GetPosition();
    const float forwardDistance =
        (relative.x * ownForward.x + relative.y * ownForward.y) / ownLength;
    if (forwardDistance <= 0.0f || std::abs(relative.z) > kVehicleHeightSeparation) {
        return false;
    }

    CVector ownMin, ownMax, otherMin, otherMax;
    if (!GetEntityDimensions(vehicle, ownMin, ownMax) ||
        !GetEntityDimensions(other, otherMin, otherMax)) {
        return false;
    }

    const float lateralDistance = std::abs(
        (relative.x * ownForward.y - relative.y * ownForward.x) / ownLength);
    const float ownHalfWidth = std::max(std::abs(ownMin.x), std::abs(ownMax.x));
    const float otherHalfWidth = std::max(std::abs(otherMin.x), std::abs(otherMax.x));
    return lateralDistance >= ownHalfWidth + otherHalfWidth + kOncomingLateralSafetyMargin;
}

bool IsSafelySeparatedParallelOrParkedVehicle(CEntity *entity, CVehicle *vehicle) {
    if (!entity || !vehicle || entity == vehicle ||
        entity->m_nType != ENTITY_TYPE_VEHICLE) {
        return false;
    }

    CVehicle *other = static_cast<CVehicle *>(entity);
    const bool parked = IsVehicleStopped(other);
    const float otherSpeedAi = std::sqrt(
        other->m_vecMoveSpeed.x * other->m_vecMoveSpeed.x +
        other->m_vecMoveSpeed.y * other->m_vecMoveSpeed.y) * kAiSpeedScale;
    if (parked && otherSpeedAi > kParkedMaxSpeedAi) {
        return false;
    }
    if (!parked && otherSpeedAi <= kParkedMaxSpeedAi) {
        return false;
    }

    const CVector &ownForward = vehicle->GetForward();
    const CVector &otherForward = other->GetForward();
    const CVector &otherRight = other->GetRight();
    const float ownLength = std::sqrt(ownForward.x * ownForward.x + ownForward.y * ownForward.y);
    const float otherForwardLength = std::sqrt(
        otherForward.x * otherForward.x + otherForward.y * otherForward.y);
    const float otherRightLength = std::sqrt(
        otherRight.x * otherRight.x + otherRight.y * otherRight.y);
    if (ownLength < 0.001f || otherForwardLength < 0.001f || otherRightLength < 0.001f) {
        return false;
    }

    const float directionDot =
        (ownForward.x * otherForward.x + ownForward.y * otherForward.y) /
        (ownLength * otherForwardLength);
    if (parked) {
        if (std::abs(directionDot) < kParkedDirectionDotThreshold) {
            return false;
        }
    } else if (directionDot < kParallelDirectionDotThreshold) {
        return false;
    }

    const CVector relative = other->GetPosition() - vehicle->GetPosition();
    const float forwardDistance =
        (relative.x * ownForward.x + relative.y * ownForward.y) / ownLength;
    if (forwardDistance <= 0.0f || std::abs(relative.z) > kVehicleHeightSeparation) {
        return false;
    }

    CVector ownMin, ownMax, otherMin, otherMax;
    if (!GetEntityDimensions(vehicle, ownMin, ownMax) ||
        !GetEntityDimensions(other, otherMin, otherMax)) {
        return false;
    }

    const float lateralAxisX = ownForward.y / ownLength;
    const float lateralAxisY = -ownForward.x / ownLength;
    const float lateralDistance = std::abs(
        relative.x * lateralAxisX + relative.y * lateralAxisY);
    const float ownHalfWidth = std::max(std::abs(ownMin.x), std::abs(ownMax.x));
    const float otherHalfLength = std::max(std::abs(otherMin.y), std::abs(otherMax.y));
    const float otherHalfWidth = std::max(std::abs(otherMin.x), std::abs(otherMax.x));
    const float otherLateralSupport =
        std::abs(lateralAxisX * otherForward.x + lateralAxisY * otherForward.y) /
            otherForwardLength * otherHalfLength +
        std::abs(lateralAxisX * otherRight.x + lateralAxisY * otherRight.y) /
            otherRightLength * otherHalfWidth;
    const float margin = parked ? kParkedLateralSafetyMargin : kParallelLateralSafetyMargin;
    return lateralDistance >= ownHalfWidth + otherLateralSupport + margin;
}

bool IsSafelySeparatedTrafficVehicle(CEntity *entity, CVehicle *vehicle) {
    return IsSafelySeparatedOncomingVehicle(entity, vehicle) ||
           IsSafelySeparatedParallelOrParkedVehicle(entity, vehicle);
}

int NearestHitIndex(
    const CVector *start,
    const CColPoint *colPoint,
    const bool *hit,
    int count) {
    int nearestIndex = -1;
    float nearestDistance = std::numeric_limits<float>::max();
    for (int index = 0; index < count; ++index) {
        if (!hit[index]) {
            continue;
        }
        const float distance = (colPoint[index].m_vecPoint - start[index]).Magnitude();
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestIndex = index;
        }
    }
    return nearestIndex;
}

void ClearForwardProbeState(VehicleState &state) {
    state.forwardProbeActive = false;
    state.forwardBlocked = false;
    state.forwardStaticObstacle = false;
    state.forwardDynamicObstacle = false;
    state.forwardPedObstacle = false;
    state.emergencyBrakeActive = false;
    state.hurriedImpactLimitActive = false;
    state.forwardClearSince = 0;
    state.forwardClearFrames = 0;
    state.forwardBlockedSince = 0;
    std::fill(std::begin(state.forwardGroundFailureFrames),
              std::end(state.forwardGroundFailureFrames), 0);
    state.forwardNearestHitDistance = std::numeric_limits<float>::max();
    state.forwardObstacleSpeed = 0.0f;
    state.staticBlocker = nullptr;
    state.staticBlockSince = 0;
}

void ClearProbeState(VehicleState &state) {
    ClearForwardProbeState(state);
    state.reverseProbeActive = false;
    state.reverseBlocked = false;
    state.reverseEmergencyBrakeActive = false;
    std::fill(std::begin(state.reverseGroundFailureFrames),
              std::end(state.reverseGroundFailureFrames), 0);
}

void CancelNativeStuckRecovery(CVehicle *vehicle, VehicleState &state) {
    (void)vehicle;
    state.nativeStuckCheckArmed = false;
    state.forwardBlockedSince = 0;
}

void ReleaseVehicleControl(CVehicle *vehicle, VehicleState &state, bool restoreSourceSpeed) {
    if (restoreSourceSpeed && state.sourceCruiseSpeedInitialized &&
        state.lastAppliedCruiseSpeedValid &&
        GetCruiseSpeed(vehicle) == state.lastAppliedCruiseSpeed) {
        SetCruiseSpeed(vehicle, state.sourceCruiseSpeed);
    }
    CancelNativeStuckRecovery(vehicle, state);
    state.sourceCruiseSpeedInitialized = false;
    state.sourceCruiseSpeedKind = platform::SpeedSourceKind::None;
    state.lastAppliedCruiseSpeedValid = false;
    state.movementSignatureInitialized = false;
    state.playerMode = PlayerProcessingMode::None;
    ClearProbeState(state);
}

bool HasReverseIntent(CVehicle *vehicle) {
    return platform::IsReverseAction(vehicle) || vehicle->m_fGasPedal < -0.01f ||
           SignedForwardSpeed(vehicle) < -0.002f;
}

struct ControlDecision {
    float targetSpeed = 0.0f;
    float velocityFloorAi = 0.0f;
    bool fullBrake = false;
    bool controlledBrake = false;
    bool dampVelocity = false;
};

float SteeringSpeedLimit(CVehicle *vehicle, float cruiseSpeed) {
    const float steer = std::abs(vehicle->m_fSteerAngle);
    if (steer <= kSteerLimitStart || cfg.turningSpeedDecrease <= 0.0f)
        return cruiseSpeed;
    const float amount = std::clamp(
        (steer - kSteerLimitStart) / (kSteerLimitFull - kSteerLimitStart),
        0.0f, 1.0f);
    const float reduction = amount * cfg.turningSpeedDecrease / 100.0f;
    return cruiseSpeed * std::max(1.0f - reduction, 0.15f);
}

float UpcomingCurveSpeedLimit(
    CVehicle *vehicle,
    float cruiseSpeed,
    float &upcomingAngle) {
    upcomingAngle = -1.0f;
    CVector current, next, following;
    if (!platform::ReadUpcomingPathPoints(vehicle, current, next, following))
        return cruiseSpeed;

    CVector incoming = next - current;
    CVector outgoing = following - next;
    incoming.z = 0.0f;
    outgoing.z = 0.0f;
    const float incomingLength = incoming.Magnitude();
    const float outgoingLength = outgoing.Magnitude();
    if (incomingLength < kCurveMinimumSegmentLength ||
        outgoingLength < kCurveMinimumSegmentLength) return cruiseSpeed;

    const float dot = std::clamp(
        (incoming.x * outgoing.x + incoming.y * outgoing.y) /
            (incomingLength * outgoingLength),
        -1.0f, 1.0f);
    const float angle = std::acos(dot);
    upcomingAngle = angle;
    if (angle < 0.12f) return cruiseSpeed;
    const float sine = std::sin(angle * 0.5f);
    if (sine < 0.001f) return cruiseSpeed;

    const float radius = std::clamp(
        std::min(incomingLength, outgoingLength) / (2.0f * sine),
        4.0f, 200.0f);
    const float curveWorldSpeed = std::sqrt(kCurveLateralAcceleration * radius);
    const float curveAiSpeed = curveWorldSpeed *
        (kAiSpeedScale / kGameFramesPerSecond);
    const float currentWorldSpeed = ForwardSpeedAi(vehicle) *
        (kGameFramesPerSecond / kAiSpeedScale);
    const float brakingDistance = std::max(
        (currentWorldSpeed * currentWorldSpeed -
         curveWorldSpeed * curveWorldSpeed) /
            (2.0f * EstimatedBrakeDeceleration(vehicle)),
        0.0f);
    CVector toTurn = next - vehicle->GetPosition();
    toTurn.z = 0.0f;
    if (toTurn.Magnitude() > brakingDistance + kCurveReactionMargin)
        return cruiseSpeed;
    return std::clamp(curveAiSpeed, 1.0f, cruiseSpeed);
}

void UpdateEmergencyBrake(
    CVehicle *vehicle,
    bool &active,
    bool obstacle,
    float freeDistance,
    bool pedestrian,
    bool hardStopOnly,
    float directionalSpeedAi,
    ControlDecision &decision) {
    if (!obstacle || !std::isfinite(freeDistance)) {
        active = false;
        return;
    }

    directionalSpeedAi = std::max(directionalSpeedAi, 0.0f);
    const float ownSpeedWorld = directionalSpeedAi *
        (kGameFramesPerSecond / kAiSpeedScale);
    // Ordinary speed control starts earlier using the conservative handling
    // estimate. Emergency braking represents the shorter, full-brake reserve.
    const float deceleration = std::clamp(
        EstimatedBrakeDeceleration(vehicle) * kEmergencyBrakeReserve,
        kMinimumBrakeDeceleration,
        kEmergencyMaximumDeceleration);
    const float margin = kEmergencyBaseMargin +
        (pedestrian ? kEmergencyPedMargin : 0.0f);
    const float stoppingDistance =
        ownSpeedWorld * kEmergencyReactionSeconds +
        (ownSpeedWorld * ownSpeedWorld) / (2.0f * deceleration) + margin;
    const float hardStopDistance = margin + std::max(
        kEmergencyHardStopMinDistance,
        ownSpeedWorld * kEmergencyHardStopSeconds);
    const bool insideHardStopZone = freeDistance <= hardStopDistance;
    const bool hardStop = directionalSpeedAi > 0.05f && insideHardStopZone;

    const float activationDistance = hardStopOnly
        ? hardStopDistance
        : std::max(
            hardStopDistance,
            stoppingDistance * kEmergencyStoppingDistanceFraction);
    if (!active && freeDistance <= activationDistance) active = true;
    if (active && freeDistance > activationDistance + kEmergencyReleaseMargin)
        active = false;
    if (insideHardStopZone) active = true;
    if (!active) return;

    decision.targetSpeed = 0.0f;
    decision.fullBrake = true;
    const bool velocityIntervention =
        cfg.emergencyVelocityIntervention == 1 ||
        (cfg.emergencyVelocityIntervention == 2 &&
         platform::IsNormalDrivingStyle(vehicle));
    decision.dampVelocity = decision.dampVelocity ||
        (hardStop && velocityIntervention);
}

bool UpdateHurriedImpactLimiter(
    CVehicle *vehicle,
    float freeDistance,
    float obstacleSpeedAi,
    float directionalSpeedAi,
    ControlDecision &decision) {
    if (!vehicle || !std::isfinite(freeDistance)) return false;

    const float contactSpeedAi = std::max(
        kHurriedMinimumContactSpeedAi,
        std::max(obstacleSpeedAi, 0.0f) + kHurriedPassingSpeedDeltaAi);
    if (directionalSpeedAi <=
        contactSpeedAi + kHurriedImpactReleaseToleranceAi) return false;

    const float ownSpeedWorld = directionalSpeedAi *
        (kGameFramesPerSecond / kAiSpeedScale);
    const float contactSpeedWorld = contactSpeedAi *
        (kGameFramesPerSecond / kAiSpeedScale);
    const float deceleration = std::clamp(
        EstimatedBrakeDeceleration(vehicle) * kEmergencyBrakeReserve,
        kMinimumBrakeDeceleration,
        kEmergencyMaximumDeceleration);
    const float brakingDistance = std::max(
        (ownSpeedWorld * ownSpeedWorld -
         contactSpeedWorld * contactSpeedWorld) / (2.0f * deceleration),
        0.0f);
    const float activationDistance =
        ownSpeedWorld * kEmergencyReactionSeconds + brakingDistance +
        kEmergencyBaseMargin;
    if (freeDistance > activationDistance) return false;

    decision.controlledBrake = true;
    const float hardStopDistance = kEmergencyBaseMargin + std::max(
        kEmergencyHardStopMinDistance,
        ownSpeedWorld * kEmergencyHardStopSeconds);
    const bool velocityIntervention =
        cfg.emergencyVelocityIntervention == 1;
    if (velocityIntervention && freeDistance <= hardStopDistance) {
        decision.dampVelocity = true;
        decision.velocityFloorAi = std::max(
            decision.velocityFloorAi, contactSpeedAi);
    }
    return true;
}

void ApplyControlDecision(
    CVehicle *vehicle,
    VehicleState &state,
    const CruiseProfile &profile,
    const ControlDecision &decision) {
    ApplyDesiredCruiseSpeed(vehicle, state, profile, decision.targetSpeed);
    if (decision.fullBrake || decision.controlledBrake) {
        vehicle->m_fGasPedal = 0.0f;
        platform::ApplyFullBrake(vehicle);
    }
    if (!decision.dampVelocity) return;

    // The regular brakes get the first chance to stop the vehicle. Reaching
    // this branch with a full emergency stop means the remaining clearance is
    // already critical, so velocity is the final collision safeguard.
    if (decision.fullBrake && decision.velocityFloorAi <= 0.0f) {
        vehicle->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        vehicle->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
        return;
    }

    const float frameScale = std::clamp(
        CTimer::ms_fTimeStep / kTimeStepMagic, 0.0f, 3.0f);
    float moveRetention = std::pow(0.82f, frameScale);
    const float currentForwardSpeedAi = ForwardSpeedAi(vehicle);
    if (decision.velocityFloorAi > 0.0f && currentForwardSpeedAi > 0.001f) {
        moveRetention = std::max(
            moveRetention,
            std::min(decision.velocityFloorAi / currentForwardSpeedAi, 1.0f));
    }
    const float turnRetention = std::pow(0.72f, frameScale);
    vehicle->m_vecMoveSpeed *= moveRetention;
    vehicle->m_vecTurnSpeed *= turnRetention;
    if (vehicle->m_vecMoveSpeed.Magnitude() < 0.002f)
        vehicle->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    if (vehicle->m_vecTurnSpeed.Magnitude() < 0.001f)
        vehicle->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
}

void UpdateReverseObstacleProtection(
    CVehicle *vehicle,
    VehicleState &state,
    const CVector &modelMin,
    const CVector &modelMax,
    ControlDecision &decision) {
    if (!HasReverseIntent(vehicle)) {
        state.reverseProbeActive = false;
        state.reverseBlocked = false;
        state.reverseEmergencyBrakeActive = false;
        std::fill(std::begin(state.reverseGroundFailureFrames),
                  std::end(state.reverseGroundFailureFrames), 0);
        return;
    }

    const float reverseWorldSpeed = std::max(-SignedForwardSpeed(vehicle), 0.0f);
    const float distance = std::max(
        reverseWorldSpeed * cfg.frontMultDist,
        kRearProbeMinDistance);
    const float height = std::clamp(modelMin.z * 0.4f, -1.0f, 1.0f) + 0.15f;
    constexpr int capacity = 3;
    const int count = IsTwoWheeler(vehicle) ? 1 : capacity;
    CVector start[capacity] = {
        WorldCoordWithOffset(vehicle, {0.0f, modelMin.y, height}),
        WorldCoordWithOffset(vehicle, {modelMin.x, modelMin.y, height}),
        WorldCoordWithOffset(vehicle, {modelMax.x, modelMin.y, height})
    };
    CVector end[capacity] = {
        WorldCoordWithOffset(vehicle, {0.0f, modelMin.y - distance, height}),
        WorldCoordWithOffset(vehicle, {modelMin.x, modelMin.y - distance, height}),
        WorldCoordWithOffset(vehicle, {modelMax.x, modelMin.y - distance, height})
    };

    bool blocked = false;
    float nearestDistance = std::numeric_limits<float>::max();
    for (int index = 0; index < count; ++index) {
        const ProbeEndStatus status = PrepareProbeEnd(end[index], cfg.heightDiffLimit);
        if (status == ProbeEndStatus::MissingGround) {
            state.reverseGroundFailureFrames[index] = std::min<unsigned char>(
                static_cast<unsigned char>(state.reverseGroundFailureFrames[index] + 1),
                kGroundFailureConfirmationFrames);
        } else {
            state.reverseGroundFailureFrames[index] = 0;
        }
        const bool forced = status == ProbeEndStatus::UnsafeHeight ||
            (status == ProbeEndStatus::MissingGround &&
             state.reverseGroundFailureFrames[index] >= kGroundFailureConfirmationFrames);

        CColPoint point = {};
        CEntity *entity = nullptr;
        bool hit = false;
        if (!forced) {
            hit = ProbeLine(vehicle, start[index], end[index], point, entity);
        }
        blocked = blocked || forced || hit;
        if (forced) {
            nearestDistance = std::min(nearestDistance, distance);
        } else if (hit) {
            nearestDistance = std::min(
                nearestDistance,
                (point.m_vecPoint - start[index]).Magnitude());
        }
        QueueDebugLine(start[index], end[index], forced || hit);
    }
    for (int index = count; index < capacity; ++index)
        state.reverseGroundFailureFrames[index] = 0;

    state.reverseProbeActive = true;
    state.reverseBlocked = blocked;
    const float reverseSpeedAi = std::max(
        -SignedForwardSpeed(vehicle) * kAiSpeedScale,
        0.0f);
    UpdateEmergencyBrake(
        vehicle,
        state.reverseEmergencyBrakeActive,
        blocked,
        nearestDistance,
        false,
        false,
        reverseSpeedAi,
        decision);
}

void UpdateNativeStuckRecovery(
    CVehicle *vehicle,
    VehicleState &state) {
    constexpr unsigned int kNativeStuckThresholdMs = 2000;

    const unsigned int now = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
    const bool staticObstacle = state.forwardBlocked && state.forwardStaticObstacle;
    const bool aiControlled = !vehicle->m_pDriver->IsPlayer() ||
        state.playerMode != PlayerProcessingMode::None;

    if (!staticObstacle || !aiControlled || !platform::HasMovementIntent(vehicle)) {
        CancelNativeStuckRecovery(vehicle, state);
        return;
    }
    if (!IsVehicleStopped(vehicle)) {
        state.forwardBlockedSince = now;
        state.nativeStuckCheckArmed = false;
        return;
    }
    if (state.forwardBlockedSince == 0) {
        state.forwardBlockedSince = now;
    }
    if (state.reverseBlocked) {
        if (!state.nativeStuckCheckArmed)
            Log("recovery blocked by rear vehicle=%p", vehicle);
        state.nativeStuckCheckArmed = true;
        return;
    }
    if (platform::HasTemporaryAction(vehicle)) {
        if (state.nativeStuckCheckArmed)
            Log("recovery started vehicle=%p", vehicle);
        state.nativeStuckCheckArmed = false;
        return;
    }
    const bool firstRequest = !state.nativeStuckCheckArmed;
    state.nativeStuckCheckArmed =
        platform::TriggerStuckRecovery(vehicle, now, kNativeStuckThresholdMs);
    if (firstRequest && state.nativeStuckCheckArmed)
        Log("recovery requested vehicle=%p mode=%s", vehicle,
            platform::IsHurriedDrivingStyle(vehicle) ? "hurried" : "normal");
}

void ProcessTrafficVehicle(CVehicle *vehicle) {
    if (!IsSupportedTrafficVehicle(vehicle) || !vehicle->m_pDriver) {
        return;
    }

    VehicleState &state = vehicleState.Get(vehicle);
    state.debugProcessed = false;
    if (!cfg.enabled || IsMissionVehicleExcluded(vehicle)) {
        ReleaseVehicleControl(vehicle, state, true);
        return;
    }

    if (!CanProcessVehicle(vehicle)) {
        ReleaseVehicleControl(vehicle, state, true);
        return;
    }
    if (cfg.onlyInNormalDrivingStyle &&
        !platform::IsNormalDrivingStyle(vehicle)) {
        ReleaseVehicleControl(vehicle, state, true);
        return;
    }

    CVector modelMin, modelMax;
    if (!GetEntityDimensions(vehicle, modelMin, modelMax)) {
        ReleaseVehicleControl(vehicle, state, true);
        return;
    }

    CruiseProfile profile;
    if (!ResolveCruiseProfile(vehicle, state, profile)) {
        state.debugProcessed = cfg.debugMode;
        state.debugSourceSpeed = -1.0f;
        ReleaseVehicleControl(vehicle, state, false);
        return;
    }

    if (cfg.useRealPhysics && !vehicle->m_pDriver->IsPlayer() &&
        CanSwitchToRealPhysics(vehicle) && platform::VehicleStatus(vehicle) == STATUS_SIMPLE) {
        platform::SwitchToRealPhysics(vehicle);
        Log("real physics vehicle=%p", vehicle);
    }

    if (cfg.fixGroundStuck && state.runGroundStuckFix &&
        vehicle->m_vecMoveSpeed.Magnitude() *
            (CTimer::ms_fTimeStep * kTimeStepMagic) < 0.005f) {
        FixGroundStuck(vehicle, modelMin, modelMax);
    }

    const float distanceToCamera =
        (TheCamera.GetPosition() - vehicle->GetPosition()).Magnitude();
    if (SpeedKmh(vehicle) > 10.0f && distanceToCamera < 30.0f) {
        state.runGroundStuckFix = false;
    }

    state.debugProcessed = cfg.debugMode;
    state.debugCurrentSpeed = SignedForwardSpeed(vehicle) * kAiSpeedScale;
    state.debugSourceSpeed = profile.baseSpeed;
    state.debugCruiseSpeed = profile.cruiseSpeed;
    state.debugRoadMultiplier = profile.roadSpeedMultiplier;

    if (platform::IsPloughDrivingStyle(vehicle)) {
        CancelNativeStuckRecovery(vehicle, state);
        ClearProbeState(state);
        state.emergencyBrakeActive = false;
        state.hurriedImpactLimitActive = false;
        state.rayPriorityYieldActive = false;
        ApplyDesiredCruiseSpeed(vehicle, state, profile, profile.cruiseSpeed);
        return;
    }

    ControlDecision decision;
    decision.targetSpeed = profile.cruiseSpeed;
    state.debugCurveLimit = profile.cruiseSpeed;
    state.debugSteerLimit = profile.cruiseSpeed;

    UpdateReverseObstacleProtection(vehicle, state, modelMin, modelMax, decision);
    if (HasReverseIntent(vehicle)) {
        CancelNativeStuckRecovery(vehicle, state);
        ClearForwardProbeState(state);
        ApplyControlDecision(vehicle, state, profile, decision);
        return;
    }

    float upcomingTurnAngle = -1.0f;
    state.debugCurveLimit = UpcomingCurveSpeedLimit(
        vehicle, profile.cruiseSpeed, upcomingTurnAngle);
    state.debugSteerLimit = SteeringSpeedLimit(vehicle, profile.cruiseSpeed);
    if (upcomingTurnAngle >= 0.0f &&
        upcomingTurnAngle < kStraightLaneAngle) {
        platform::KeepCurrentLaneOnStraight(vehicle);
    }
    decision.targetSpeed = std::min(
        decision.targetSpeed,
        std::min(state.debugCurveLimit, state.debugSteerLimit));

    const float speedKmh = SpeedKmh(vehicle);
    const float forwardSpeedAi = ForwardSpeedAi(vehicle);
    const float probeDistance = std::max(
        vehicle->m_vecMoveSpeed.Magnitude() * cfg.frontMultDist,
        kForwardProbeMinDistance);
    constexpr int capacity = 3;
    CVector start[capacity] = {};
    CVector end[capacity] = {};
    int probeCount = BuildFrontProbeRays(
        vehicle, modelMin, modelMax, probeDistance, speedKmh, start, end);

    bool forcedHit[capacity] = {};
    bool hit[capacity] = {};
    CColPoint points[capacity] = {};
    CEntity *entities[capacity] = {};
    int missingGroundCount = 0;
    ProbeEndStatus endStatus[capacity] = {};

    for (int index = 0; index < probeCount; ++index) {
        endStatus[index] = PrepareProbeEnd(end[index], cfg.heightDiffLimit);
        if (endStatus[index] == ProbeEndStatus::MissingGround) {
            ++missingGroundCount;
            state.forwardGroundFailureFrames[index] = std::min<unsigned char>(
                static_cast<unsigned char>(state.forwardGroundFailureFrames[index] + 1),
                kGroundFailureConfirmationFrames);
        } else {
            state.forwardGroundFailureFrames[index] = 0;
        }
    }
    for (int index = probeCount; index < capacity; ++index) {
        state.forwardGroundFailureFrames[index] = 0;
    }

    bool forcedObstacle = false;
    for (int index = 0; index < probeCount; ++index) {
        forcedHit[index] = endStatus[index] == ProbeEndStatus::UnsafeHeight ||
            (endStatus[index] == ProbeEndStatus::MissingGround &&
             (missingGroundCount >= 2 ||
              state.forwardGroundFailureFrames[index] >= kGroundFailureConfirmationFrames));
        forcedObstacle = forcedObstacle || forcedHit[index];
        if (!forcedHit[index]) {
            hit[index] = ProbeLine(vehicle, start[index], end[index], points[index], entities[index]);
        }
    }

    bool sensedObstacle = forcedObstacle;
    for (int index = 0; index < probeCount; ++index) {
        sensedObstacle = sensedObstacle || hit[index];
    }

    const int nearestIndex = NearestHitIndex(start, points, hit, probeCount);
    CEntity *nearestEntity = nearestIndex >= 0 ? entities[nearestIndex] : nullptr;
    float nearestDistance = nearestIndex >= 0
        ? (points[nearestIndex].m_vecPoint - start[nearestIndex]).Magnitude()
        : (forcedObstacle ? probeDistance : std::numeric_limits<float>::max());
    bool staticObstacle = nearestEntity ? IsStaticObstacle(nearestEntity) : false;
    bool dynamicObstacle = IsDynamicObstacle(nearestEntity);
    bool pedestrianObstacle = nearestEntity && nearestEntity->m_nType == ENTITY_TYPE_PED;

    const unsigned int now = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
    if (staticObstacle) {
        if (state.staticBlocker != nearestEntity ||
            (vehicle->GetPosition() - state.staticBlockAnchor).Magnitude() >
                kStaticProgressDistance) {
            state.staticBlocker = nearestEntity;
            state.staticBlockSince = now;
            state.staticBlockAnchor = vehicle->GetPosition();
        }
    } else {
        state.staticBlocker = nullptr;
        state.staticBlockSince = 0;
    }
    const bool staticNoProgress = staticObstacle &&
        platform::HasMovementIntent(vehicle) && IsVehicleStopped(vehicle) &&
        now - state.staticBlockSince >= kStaticBlockConfirmationMs;
    bool hardStaticObstacle = staticObstacle &&
        ((nearestIndex == 0) || staticNoProgress);

    bool obstacle = sensedObstacle;
    if (!obstacle && state.forwardBlocked) {
        if (state.forwardClearSince == 0) {
            state.forwardClearSince = now;
            state.forwardClearFrames = 1;
        } else {
            ++state.forwardClearFrames;
        }
        if (now - state.forwardClearSince < kObstacleClearConfirmationMs ||
            state.forwardClearFrames < 3) {
            obstacle = true;
            staticObstacle = state.forwardStaticObstacle;
            hardStaticObstacle = state.forwardStaticObstacle;
            dynamicObstacle = state.forwardDynamicObstacle;
            pedestrianObstacle = state.forwardPedObstacle;
            nearestDistance = state.forwardNearestHitDistance;
        } else {
            state.forwardClearSince = 0;
            state.forwardClearFrames = 0;
            state.forwardBlockedSince = 0;
        }
    } else if (obstacle) {
        state.forwardClearSince = 0;
        state.forwardClearFrames = 0;
        if (state.forwardBlockedSince == 0) {
            state.forwardBlockedSince = now;
        }
    } else {
        state.forwardBlockedSince = 0;
    }

    const bool wasForwardBlocked = state.forwardBlocked;
    const bool wasForwardStatic = state.forwardStaticObstacle;
    const bool wasEmergencyBrakeActive = state.emergencyBrakeActive;
    const bool wasHurriedImpactLimitActive = state.hurriedImpactLimitActive;
    state.forwardProbeActive = true;
    state.forwardBlocked = obstacle;
    state.forwardStaticObstacle = hardStaticObstacle;
    state.forwardDynamicObstacle = dynamicObstacle;
    state.forwardPedObstacle = pedestrianObstacle;
    state.forwardNearestHitDistance = nearestDistance;
    if (sensedObstacle) {
        state.forwardObstacleSpeed = ObstacleForwardSpeedAi(nearestEntity, vehicle);
    }
    if (obstacle && (!wasForwardBlocked || wasForwardStatic != hardStaticObstacle)) {
        Log("obstacle vehicle=%p type=%s distance=%.2f ray=%d",
            vehicle,
            pedestrianObstacle ? "ped" :
                (dynamicObstacle ? "dynamic" : "static"),
            nearestDistance,
            nearestIndex);
    }

    if (obstacle) {
        const bool vehicleObstacle = dynamicObstacle && !pedestrianObstacle &&
            (!nearestEntity || nearestEntity->m_nType == ENTITY_TYPE_VEHICLE);
        const bool hurriedMode = platform::IsHurriedDrivingStyle(vehicle);

        // Normal traffic follows a detected leader smoothly. Hurried traffic
        // leaves overtaking to the game's AVOID_CARS logic and receives only
        // the final collision guard below.
        if (!vehicleObstacle || !hurriedMode) {
            if (vehicleObstacle) {
                const float leadingSpeed = std::clamp(
                    state.forwardObstacleSpeed, 0.0f, profile.cruiseSpeed);
                const float safeGap = std::clamp(
                    1.5f + forwardSpeedAi * 0.25f, 2.5f, 7.0f);
                const float gapRange = std::max(probeDistance - safeGap, 1.0f);
                float gapBlend = std::clamp(
                    (nearestDistance - safeGap) / gapRange, 0.0f, 1.0f);
                gapBlend = gapBlend * gapBlend * (3.0f - 2.0f * gapBlend);
                const float leadSpeed = leadingSpeed +
                    (profile.cruiseSpeed - leadingSpeed) * gapBlend;
                decision.targetSpeed = std::min(decision.targetSpeed, leadSpeed);
            }
            const float comfortableSpeed = ComfortableObstacleCruiseSpeedAi(
                vehicle, nearestDistance,
                dynamicObstacle ? state.forwardObstacleSpeed : 0.0f,
                pedestrianObstacle);
            decision.targetSpeed = std::min(decision.targetSpeed, comfortableSpeed);
        }

        state.hurriedImpactLimitActive = hurriedMode && vehicleObstacle &&
            nearestIndex == 0 && UpdateHurriedImpactLimiter(
                vehicle,
                nearestDistance,
                state.forwardObstacleSpeed,
                forwardSpeedAi,
                decision);
        const bool vehicleEmergencyObstacle = vehicleObstacle && !hurriedMode;
        const bool emergencyObstacle = forcedObstacle || pedestrianObstacle ||
            vehicleEmergencyObstacle || hardStaticObstacle;
        UpdateEmergencyBrake(
            vehicle,
            state.emergencyBrakeActive,
            emergencyObstacle,
            nearestDistance,
            pedestrianObstacle,
            vehicleObstacle,
            forwardSpeedAi,
            decision);
    } else {
        state.emergencyBrakeActive = false;
        state.hurriedImpactLimitActive = false;
    }

    if (state.hurriedImpactLimitActive != wasHurriedImpactLimitActive) {
        Log("impact limit vehicle=%p active=%d speed=%.2f lead=%.2f",
            vehicle,
            state.hurriedImpactLimitActive ? 1 : 0,
            forwardSpeedAi,
            state.forwardObstacleSpeed);
    }
    if (state.emergencyBrakeActive != wasEmergencyBrakeActive) {
        Log("emergency brake vehicle=%p active=%d distance=%.2f",
            vehicle,
            state.emergencyBrakeActive ? 1 : 0,
            nearestDistance);
    }

    for (int index = 0; index < probeCount; ++index) {
        QueueDebugLine(start[index], end[index], forcedHit[index] || hit[index]);
    }

    if (state.rayPriorityYieldActive && platform::IsNormalDrivingStyle(vehicle)) {
        decision.targetSpeed = 0.0f;
        decision.fullBrake = true;
    }

    ApplyControlDecision(vehicle, state, profile, decision);
    UpdateNativeStuckRecovery(vehicle, state);
}

void ProcessVehicleEvent(CVehicle *vehicle) {
    if (!vehicle) {
        return;
    }
    VehicleState &state = vehicleState.Get(vehicle);
    const unsigned int frame = static_cast<unsigned int>(CTimer::m_FrameCounter);
    if (state.lastProcessedFrame == frame) {
        return;
    }
    state.lastProcessedFrame = frame;
    UpdateTaxiRoofLight(vehicle);
    ProcessTrafficVehicle(vehicle);
}

void ProcessAllTrafficVehicles() {
    std::vector<CVehicle *> vehicles;
    platform::CollectTrafficVehicles(vehicles);
    for (CVehicle *vehicle : vehicles) ProcessVehicleEvent(vehicle);
}

void RenderVehicleDebug(CVehicle *vehicle) {
    if (!cfg.debugMode || !vehicle) return;
    VehicleState &state = vehicleState.Get(vehicle);
    if (!state.debugProcessed) return;
    CVector modelMin, modelMax;
    if (!GetEntityDimensions(vehicle, modelMin, modelMax)) return;

    float offset = 0.0f;
    if (state.debugSourceSpeed < 0.0f) {
        DrawDebugText(vehicle, modelMax, offset, 1.0f,
            "SOURCE SPEED NOT DETECTED");
        return;
    }
    DrawDebugText(vehicle, modelMax, offset, 1.0f,
        "speed %.1f game", state.debugCurrentSpeed);
    DrawDebugText(vehicle, modelMax, offset, 1.0f,
        "base %.1f cruise %.1f road %.2f",
        state.debugSourceSpeed, state.debugCruiseSpeed,
        state.debugRoadMultiplier);
    DrawDebugText(vehicle, modelMax, offset, 1.0f,
        "target %.1f applied %u", state.desiredCruiseSpeed,
        state.lastAppliedCruiseSpeed);
    DrawDebugText(vehicle, modelMax, offset, 1.0f,
        "curve %.1f steer %.1f",
        state.debugCurveLimit, state.debugSteerLimit);
    if (state.forwardBlocked)
        DrawDebugText(vehicle, modelMax, offset, 1.0f, "FORWARD BLOCKED");
    if (state.reverseBlocked)
        DrawDebugText(vehicle, modelMax, offset, 1.0f, "REVERSE BLOCKED");
    if (state.emergencyBrakeActive || state.reverseEmergencyBrakeActive)
        DrawDebugText(vehicle, modelMax, offset, 1.0f, "EMERGENCY BRAKE");
    if (state.hurriedImpactLimitActive)
        DrawDebugText(vehicle, modelMax, offset, 1.0f, "IMPACT LIMIT");
    if (state.rayPriorityYieldActive)
        DrawDebugText(vehicle, modelMax, offset, 1.0f,
            "TRAFFIC YIELD %.1f", state.rayPriorityConflictDistance);
    if (state.nativeStuckCheckArmed)
        DrawDebugText(vehicle, modelMax, offset, 1.0f, "RECOVERY ARMED");
}

} // namespace

bool ShouldSkipNativeTrafficSlowdown(CEntity *entity, CVehicle *vehicle) {
    return cfg.enabled && CanProcessVehicle(vehicle) &&
           IsSafelySeparatedTrafficVehicle(entity, vehicle);
}

void Install() {
    randomState ^= static_cast<std::uint32_t>(GetTickCount());
    randomState ^= static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Install));
    LoadConfig(true);

    Events::initGameEvent += [] {
        platform::InstallNativeTrafficSlowdownHook();
    };

    Events::vehicleRenderEvent.before += [](CVehicle *vehicle) {
        RenderVehicleDebug(vehicle);
    };
    Events::processScriptsEvent.after += [] {
        ReloadConfigIfDue();
        platform::ProcessRandomVehicleSpawnProtection();
        platform::ProcessPlayerBikeSirenYield();
        UpdateCompactRayPriorityArbiter();
        ProcessAllTrafficVehicles();
    };
    Events::restartGameEvent += [] {
        LoadConfig(false);
    };
    Events::drawingEvent.Add([] {
        DrawDebugLines();
    });
}

bool SetPlayerAutoDriveEnabled(bool enabled) {
    if (playerAutoDrive && !enabled) {
        if (CVehicle *vehicle = FindPlayerVehicle()) {
            VehicleState &state = vehicleState.Get(vehicle);
            ReleaseVehicleControl(vehicle, state, true);
        }
    }
    playerAutoDrive = enabled && cfg.enabled;
    Log("Ext_TogglePlayerAutoDrive requested=%d accepted=%d",
        enabled ? 1 : 0,
        playerAutoDrive ? 1 : 0);
    return enabled ? playerAutoDrive : cfg.enabled;
}

bool PlayerAutoDriveEnabled() {
    return playerAutoDrive;
}

} // namespace rtf

// Keep the bootstrap in this translation unit and after every shared runtime
// object above. C++ guarantees initialization order within one translation
// unit, so Install() cannot run before cfg, vehicleState, logFile and
// debugLines have been constructed.
namespace {
class RealTrafficFixPlugin {
public:
    RealTrafficFixPlugin() {
        rtf::Install();
    }
};

RealTrafficFixPlugin gRealTrafficFixPlugin;
} // namespace
