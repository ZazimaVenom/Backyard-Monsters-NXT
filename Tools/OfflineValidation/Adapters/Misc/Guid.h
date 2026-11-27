#pragma once
#include "CoreMinimal.h"
enum class EGuidFormats{Digits};struct FGuid{uint32 Value;static FGuid NewGuid(){static uint32 next=0;return {++next};}FString ToString(EGuidFormats)const{return FString::Printf("test-guid-%u",Value);}};
