#include "BYMMenuGameMode.h"
#include "BYMCameraPawn.h"
#include "BYMHUD.h"
#include "BYMPlayerController.h"

ABYMMenuGameMode::ABYMMenuGameMode()
{
    PlayerControllerClass = ABYMPlayerController::StaticClass();
    DefaultPawnClass = ABYMCameraPawn::StaticClass();
    HUDClass = ABYMHUD::StaticClass();
    // Deliberately no server subsystem initialization: the menu is not a game host.
}
