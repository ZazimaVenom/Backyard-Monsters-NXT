#pragma once
#include "CoreMinimal.h"
// Deterministic substitute RNG; this harness validates helper algorithms,
// not engine RNG output or Unreal API compatibility.
struct FRandomStream{
 std::mt19937 State{0};
 void Initialize(int32 seed){State.seed(uint32(seed));}
 int32 RandRange(int32 lo,int32 hi){return std::uniform_int_distribution<int32>(lo,hi)(State);}
 float FRandRange(float lo,float hi){return std::uniform_real_distribution<float>(lo,hi)(State);}
};
