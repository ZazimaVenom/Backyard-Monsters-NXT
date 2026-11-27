#pragma once
#include "CoreMinimal.h"
// Test-only discovery: points at the unmodified packaged plugin Content folder.
struct IPlugin { FString GetContentDir() const {
 const char* Path = std::getenv("BYM_OFFLINE_CONTENT");
 if (!Path || !*Path) throw std::runtime_error("Missing offline catalog path");
 return Path;
} };
struct IPluginManager {
 static IPluginManager& Get(){static IPluginManager Value;return Value;}
 TSharedPtr<IPlugin> FindPlugin(const FString&)const{return MakeShared<IPlugin>();}
};
