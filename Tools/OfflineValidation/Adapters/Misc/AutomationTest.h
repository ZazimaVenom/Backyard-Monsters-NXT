#pragma once
#include "CoreMinimal.h"
inline int Assertions=0;inline int Failures=0;
struct FAutomationTestBase{
 template<class A,class B>bool TestEqual(const FString&label,const A&a,const B&b){
  // Unreal has overloaded numeric assertions, not an unconstrained A/B numeric
  // comparison. Refuse mixed numeric types here rather than hiding ambiguities
  // such as the UE 5.8 int64/int32 worker-material regression.
  static_assert(!(std::is_arithmetic<A>::value&&std::is_arithmetic<B>::value)||std::is_same<A,B>::value,
   "Use matching numeric types in TestEqual; mixed types can be ambiguous in Unreal");
  ++Assertions;if(a==b)return true;++Failures;std::cerr<<"FAIL "<<label<<": actual="<<a<<" expected="<<b<<'\n';return false;}
 bool TestTrue(const FString&label,bool value){return TestEqual(label,value,true);}
 bool TestFalse(const FString&label,bool value){return TestEqual(label,value,false);}
 void AddError(const FString&error){++Failures;std::cerr<<"ERROR "<<error<<'\n';}
};
namespace EAutomationTestFlags{constexpr int EditorContext=1,EngineFilter=2;}
struct FOfflineTest{const char*Name;bool(*Run)();};
inline std::vector<FOfflineTest>& OfflineTests(){static std::vector<FOfflineTest>Tests;return Tests;}
inline bool RegisterOffline(const char*Name,bool(*Run)()){OfflineTests().push_back({Name,Run});return true;}
#define IMPLEMENT_SIMPLE_AUTOMATION_TEST(Name,Path,Flags) struct Name:FAutomationTestBase{bool RunTest(const FString&Parameters);}; inline const bool Name##Registered = RegisterOffline(Path, [](){return Name().RunTest("");});
