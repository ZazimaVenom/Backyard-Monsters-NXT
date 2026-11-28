#pragma once
#include "CoreMinimal.h"
// The portable harness uses UTF-8 char strings, not engine UTF-16 TCHARs.
// This adapter preserves real UTF-8 bytes; the production codec itself validates
// byte sequences. Native automation additionally exercises Unreal conversion.
struct FTCHARToUTF8 {
 std::string Bytes;
 explicit FTCHARToUTF8(const TCHAR* text):Bytes(text){}
 FTCHARToUTF8(const TCHAR* text,int32 length):Bytes(text,static_cast<size_t>(length)){}
 const ANSICHAR* Get()const{return Bytes.data();}
 int32 Length()const{return static_cast<int32>(Bytes.size());}
};
struct FUTF8ToTCHAR {
 std::string Text;
 FUTF8ToTCHAR(const ANSICHAR* text,int32 length):Text(text,static_cast<size_t>(length)){}
 const TCHAR* Get()const{return Text.data();}
 int32 Length()const{return static_cast<int32>(Text.size());}
};
