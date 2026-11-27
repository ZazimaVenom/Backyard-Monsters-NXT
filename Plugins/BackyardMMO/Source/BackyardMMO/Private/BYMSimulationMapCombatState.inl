// Included inside BYMSim before FBattle. GuardTower.as owns four independent
// emitters, rather than sharing BTOWER's ordinary target and frame countdown.
struct FMapTowerState
{
    FString Targets[4];
    double NextFireAt[4] = {};
};
