#pragma once

class CEntity;
class CVehicle;

namespace rtf {

void Install();
bool SetPlayerAutoDriveEnabled(bool enabled);
bool PlayerAutoDriveEnabled();
bool ShouldSkipNativeTrafficSlowdown(CEntity *entity, CVehicle *vehicle);

} // namespace rtf
