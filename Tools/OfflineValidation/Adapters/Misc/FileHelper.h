#pragma once
#include "CoreMinimal.h"
struct FFileHelper{static bool LoadFileToString(FString&out,const char*path){std::ifstream file(path,std::ios::binary);if(!file)return false;std::ostringstream bytes;bytes<<file.rdbuf();out=bytes.str();return true;}};
