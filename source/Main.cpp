#include "common/Core.h"

// This export is deliberately a permission switch only. It does not carry a
// speed, route, driving style or any other control input. RTF independently
// observes the speed assigned by the engine/task once the player's vehicle is
// allowed into the normal processing path.
extern "C" int __declspec(dllexport) Ext_TogglePlayerAutoDrive(int enabled) {
    return rtf::SetPlayerAutoDriveEnabled(enabled == 1) ? 1 : 0;
}
