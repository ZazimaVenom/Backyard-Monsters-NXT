#pragma once
#pragma GCC system_header
#include "CoreMinimal.h"
enum class EJson {None,Null,String,Number,Boolean,Array,Object};
struct FJsonObject;
struct FJsonValue {
 EJson Type=EJson::Null;double Number=0;bool Boolean=false;FString String;
 TSharedPtr<FJsonObject> Object;TArray<TSharedPtr<FJsonValue>> Array;
 virtual ~FJsonValue()=default;
 TSharedPtr<FJsonObject> AsObject()const{return Type==EJson::Object?Object:TSharedPtr<FJsonObject>();}
 const TArray<TSharedPtr<FJsonValue>>&AsArray()const{return Array;}
 double AsNumber()const{return Number;}
 FString AsString()const{return String;}
};
struct FJsonValueNumber:FJsonValue {explicit FJsonValueNumber(double value){Type=EJson::Number;Number=value;}};
struct FJsonValueString:FJsonValue {explicit FJsonValueString(const FString&value){Type=EJson::String;String=value;}};
struct FJsonValueBoolean:FJsonValue {explicit FJsonValueBoolean(bool value){Type=EJson::Boolean;Boolean=value;}};
struct FJsonValueObject:FJsonValue {explicit FJsonValueObject(const TSharedPtr<FJsonObject>&value){Type=EJson::Object;Object=value;}};
struct FJsonValueArray:FJsonValue {explicit FJsonValueArray(const TArray<TSharedPtr<FJsonValue>>&value){Type=EJson::Array;Array=value;}};

// UE 5.8 JSON iteration exposes a shared-string key, not an FString.
// Deliberately provide no implicit FString or TCHAR* conversion: a JSON
// entry's key must be copied with FString(*Entry.Key) before passing it to
// a gameplay TMap<FString,...>. The previous FString-key stand-in hid the
// exact Contains/Add/equality errors reported by the real UE compiler.
class FOfflineJsonObjectKey {
 FString Text;
public:
 FOfflineJsonObjectKey()=default;
 explicit FOfflineJsonObjectKey(const FString&text):Text(text){}
 const TCHAR* operator*()const{return *Text;}
 bool operator==(const FOfflineJsonObjectKey&other)const{return Text==other.Text;}
 bool operator!=(const FOfflineJsonObjectKey&other)const{return !(*this==other);}
 bool IsEmpty()const{return Text.IsEmpty();}
 int32 Len()const{return Text.Len();}
};
template<>struct TOfflineMapHash<FOfflineJsonObjectKey> {
 size_t operator()(const FOfflineJsonObjectKey&key)const{return std::hash<std::string>{}(*key);}
};
// Existing, explicitly typed TPair<FString,...> JSON iteration is supported
// by UE's JSON iterator. Preserve that pair-copy form without allowing an
// automatically deduced entry.Key to convert implicitly on its own.
template<class V>struct TPair<FOfflineJsonObjectKey,V> {
 FOfflineJsonObjectKey Key;V Value;
 operator TPair<FString,V>()const{return {FString(*Key),Value};}
};
class FOfflineJsonObjectValues:public TMap<FOfflineJsonObjectKey,TSharedPtr<FJsonValue>> {
 using FBase=TMap<FOfflineJsonObjectKey,TSharedPtr<FJsonValue>>;
public:
 using FBase::Add;using FBase::Contains;using FBase::Find;using FBase::Remove;
 void Add(const FString&key,const TSharedPtr<FJsonValue>&value){FBase::Add(FOfflineJsonObjectKey(key),value);}
 void Add(const FString&key,TSharedPtr<FJsonValue>&&value){FBase::Add(FOfflineJsonObjectKey(key),MoveTemp(value));}
 bool Contains(const FString&key)const{return FBase::Contains(FOfflineJsonObjectKey(key));}
 TSharedPtr<FJsonValue>*Find(const FString&key){return FBase::Find(FOfflineJsonObjectKey(key));}
 const TSharedPtr<FJsonValue>*Find(const FString&key)const{return FBase::Find(FOfflineJsonObjectKey(key));}
 int32 Remove(const FString&key){return FBase::Remove(FOfflineJsonObjectKey(key));}
};
struct FJsonObject {
 FOfflineJsonObjectValues Values;
 bool HasField(const FString&key)const{return Values.Contains(key);}
 void RemoveField(const FString&key){Values.Remove(key);}
 void SetNumberField(const FString&key,double value){Values.Add(key,MakeShared<FJsonValueNumber>(value));}
 void SetStringField(const FString&key,const FString&value){Values.Add(key,MakeShared<FJsonValueString>(value));}
 void SetBoolField(const FString&key,bool value){Values.Add(key,MakeShared<FJsonValueBoolean>(value));}
 void SetObjectField(const FString&key,const TSharedPtr<FJsonObject>&value){Values.Add(key,MakeShared<FJsonValueObject>(value));}
 void SetArrayField(const FString&key,const TArray<TSharedPtr<FJsonValue>>&value){Values.Add(key,MakeShared<FJsonValueArray>(value));}
 bool TryGetNumberField(const FString&key,double&value)const{auto*p=Values.Find(key);if(!p||!p->IsValid()||(*p)->Type!=EJson::Number)return false;value=(*p)->Number;return true;}
 bool TryGetStringField(const FString&key,FString&value)const{auto*p=Values.Find(key);if(!p||!p->IsValid()||(*p)->Type!=EJson::String)return false;value=(*p)->String;return true;}
 bool TryGetBoolField(const FString&key,bool&value)const{auto*p=Values.Find(key);if(!p||!p->IsValid()||(*p)->Type!=EJson::Boolean)return false;value=(*p)->Boolean;return true;}
 bool TryGetObjectField(const FString&key,const TSharedPtr<FJsonObject>*&value)const{auto*p=Values.Find(key);if(!p||!p->IsValid()||(*p)->Type!=EJson::Object)return false;value=&(*p)->Object;return true;}
 bool TryGetArrayField(const FString&key,const TArray<TSharedPtr<FJsonValue>>*&value)const{auto*p=Values.Find(key);if(!p||!p->IsValid()||(*p)->Type!=EJson::Array)return false;value=&(*p)->Array;return true;}
};
