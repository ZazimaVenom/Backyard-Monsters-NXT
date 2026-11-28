#pragma once
#pragma GCC system_header
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <type_traits>
#include <cctype>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <limits>
#include <stdexcept>
using TCHAR=char;
using ANSICHAR=char;
using int32=int32_t;
using int64=int64_t;
using uint32=uint32_t;
using uint64=uint64_t;
using uint8=uint8_t;
constexpr int32 INDEX_NONE=-1;
constexpr int32 MAX_int32=2147483647;
constexpr int32 MIN_int32=(-2147483647-1);
enum class ESearchCase {IgnoreCase,CaseSensitive};
#define check(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(0)
constexpr double PI=3.14159265358979323846;
constexpr double UE_SMALL_NUMBER=1.e-8;
#define BACKYARDMMO_API
#define WITH_DEV_AUTOMATION_TESTS 1
#define TEXT(x) x
#define UE_ARRAY_COUNT(x) static_cast<int32>(sizeof(x)/sizeof((x)[0]))
#define MoveTemp(x) std::move(x)
class FString:public std::string {
public:
 using std::string::string;
 FString(const std::string&s):std::string(s){}
 FString(int32 count,const TCHAR*text):std::string(text,static_cast<size_t>(count)){}
 bool IsEmpty()const{return empty();}
 int32 Len()const{return static_cast<int32>(size());}
 const char* operator*()const{return c_str();}
 FString Mid(int32 start)const{return substr(start);}
 bool Contains(const FString& fragment)const{return find(fragment)!=npos;}
 bool StartsWith(const FString& prefix)const{return rfind(prefix,0)==0;}
 bool IsNumeric()const{return !empty()&&std::all_of(begin(),end(),[](unsigned char c){return std::isdigit(c);});}
 int32 Compare(const FString&other,ESearchCase kind=ESearchCase::CaseSensitive)const{FString a=*this,b=other;if(kind==ESearchCase::IgnoreCase){for(char&c:a)c=static_cast<char>(std::tolower(c));for(char&c:b)c=static_cast<char>(std::tolower(c));}return a.compare(b);}
 template<class List>static FString Join(const List&items,const FString&separator){FString result;for(const auto&value:items){if(!result.empty())result+=separator;result+=value;}return result;}
 static FString Printf(const char*format,...){va_list args;va_start(args,format);va_list copied;va_copy(copied,args);int length=vsnprintf(nullptr,0,format,copied);va_end(copied);std::vector<char>buf(static_cast<size_t>(length)+1);vsnprintf(buf.data(),buf.size(),format,args);va_end(args);return FString(buf.data());}
 void Reset(){clear();}
 void Empty(){clear();}
 static FString FromInt(int32 n){return std::to_string(n);}
 static FString ChrN(int32 n,TCHAR c){return FString(n,c);}
};
template<class T>class TArray:public std::vector<T>{
public:
 using std::vector<T>::vector;
 int32 Num()const{return int32(this->size());}
 bool IsEmpty()const{return this->empty();}
 bool IsValidIndex(int32 i)const{return i>=0&&i<Num();}
 void Reset(){this->clear();}
 void RemoveAt(int32 index,int32 count=1){this->erase(this->begin()+index,this->begin()+index+count);}
 T& Last(){return this->back();}
 const T&Last()const{return this->back();}
 void Empty(){this->clear();}
 template<class P>void RemoveAll(P predicate){this->erase(std::remove_if(this->begin(),this->end(),predicate),this->end());}
 void Reserve(int32 n){this->reserve(n);}
 void SetNum(int32 n){this->resize(n);}
 void SetNumUninitialized(int32 n){this->resize(n);}
 T* GetData(){return this->data();}
 const T* GetData()const{return this->data();}
 void Append(const T*values,int32 count){if(count>0)this->insert(this->end(),values,values+count);}
 void Append(const TArray<T>&values){this->insert(this->end(),values.begin(),values.end());}
 void Init(const T&v,int32 n){this->assign(n,v);}
 void Add(const T&v){this->push_back(v);}
 void Add(T&&v){this->push_back(std::move(v));}
 void Sort(){std::sort(this->begin(),this->end());}
 template<class P>void Sort(P predicate){if constexpr(std::is_pointer<T>::value)std::sort(this->begin(),this->end(),[&](const T&a,const T&b){return predicate(*a,*b);});else std::sort(this->begin(),this->end(),predicate);}
 template<class P>void StableSort(P predicate){std::stable_sort(this->begin(),this->end(),predicate);}
 template<class P>T*FindByPredicate(P predicate){for(auto&value:*this)if(predicate(value))return &value;return nullptr;}
 template<class P>const T*FindByPredicate(P predicate)const{for(const auto&value:*this)if(predicate(value))return &value;return nullptr;}
 template<class P>bool ContainsByPredicate(P predicate)const{return FindByPredicate(predicate)!=nullptr;}
 template<class P>int32 IndexOfByPredicate(P predicate)const{for(int32 index=0;index<Num();++index)if(predicate((*this)[index]))return index;return INDEX_NONE;}
 int32 Find(const T&value)const{return IndexOfByPredicate([&](const T&entry){return entry==value;});}
 bool Contains(const T&value)const{return Find(value)!=INDEX_NONE;}
 void Insert(const T&value,int32 index){this->insert(this->begin()+index,value);}
 int32 Remove(const T&value){int32 old=Num();RemoveAll([&](const T&entry){return entry==value;});return old-Num();}
};
template<class K,class V>struct TPair{K Key;V Value;};
template<class K>struct TOfflineMapHash {
 size_t operator()(const K&key)const{if constexpr(std::is_same<K,FString>::value)return std::hash<std::string>{}(key);else return std::hash<K>{}(key);}
};
template<class K,class V>class TMap:public TArray<TPair<K,V>>{
 // Unreal's TMap uses hashed lookup. Keep a vector for Key/Value range
 // iteration and an index for lookup; a linear stand-in makes the real
 // 100,000-structure world catalog pathologically unlike the engine.
 std::unordered_map<K,size_t,TOfflineMapHash<K>> Index;
public:
 void Add(const K&k,const V&v){FindOrAdd(k)=v;}
 void Add(const K&k,V&&v){FindOrAdd(k)=std::move(v);}
 V&FindOrAdd(const K&k){auto found=Index.find(k);if(found!=Index.end())return (*this)[found->second].Value;Index.emplace(k,this->size());this->push_back({k,{}});return this->back().Value;}
 V*Find(const K&k){auto found=Index.find(k);return found==Index.end()?nullptr:&(*this)[found->second].Value;}
 const V*Find(const K&k)const{auto found=Index.find(k);return found==Index.end()?nullptr:&(*this)[found->second].Value;}
 V&FindChecked(const K&k){auto*p=Find(k);if(!p)throw std::runtime_error("missing map key");return *p;}
 const V&FindChecked(const K&k)const{auto*p=Find(k);if(!p)throw std::runtime_error("missing map key");return *p;}
 int32 Remove(const K&k){auto found=Index.find(k);if(found==Index.end())return 0;const size_t at=found->second,last=this->size()-1;Index.erase(found);if(at!=last){(*this)[at]=std::move((*this)[last]);Index[(*this)[at].Key]=at;}this->pop_back();return 1;}
 void GetKeys(TArray<K>&keys)const{keys.Reset();for(const auto&e:*this)keys.Add(e.Key);}
 V FindRef(const K&k)const{const V*found=Find(k);return found?*found:V();}
 bool Contains(const K&k)const{return Index.find(k)!=Index.end();}
 void Empty(){this->clear();Index.clear();}
 void Reset(){Empty();}
 void Reserve(int32 n){TArray<TPair<K,V>>::Reserve(n);Index.reserve(static_cast<size_t>(n));}
};
template<class T>class TSet:public TArray<T>{
public:bool Contains(const T&v)const{return std::find(this->begin(),this->end(),v)!=this->end();}
 void Add(const T&v){if(!Contains(v))this->push_back(v);}
};
struct FMath{
 template<class T>static T Clamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
 template<class T>static T Min(T a,T b){return std::min(a,b);}
 template<class T>static T Max(T a,T b){return std::max(a,b);}
 template<class T>static auto Abs(T v){return std::abs(v);}
 template<class T>static bool IsFinite(T v){return std::isfinite(v);}
 template<class T>static int32 FloorToInt(T v){return int32(std::floor(v));}
 template<class T>static bool IsNearlyEqual(T a,T b){return std::abs(a-b)<1.e-8;}
 template<class T>static bool IsNearlyEqual(T a,T b,T e){return std::abs(a-b)<=e;}
 template<class T>static T Square(T value){return value*value;}
 static double Sqrt(double v){return std::sqrt(v);}
 static double FloorToDouble(double v){return std::floor(v);}
 static double Fmod(double a,double b){return std::fmod(a,b);}
 template<class T>static T Min3(T a,T b,T c){return std::min(a,std::min(b,c));}
 template<class T>static T DivideAndRoundUp(T a,T b){return (a+b-1)/b;}
 template<class T>static int32 CeilToInt(T v){return int32(std::ceil(v));}
 static double Cos(double v){return std::cos(v);}
 static double Sin(double v){return std::sin(v);}
};
struct FVector2D{
 double X=0,Y=0;
 FVector2D()=default;FVector2D(double x,double y):X(x),Y(y){}
 static const FVector2D ZeroVector;
 FVector2D operator+(const FVector2D&r)const{return {X+r.X,Y+r.Y};}
 FVector2D operator-(const FVector2D&r)const{return {X-r.X,Y-r.Y};}
 FVector2D operator/(double d)const{return {X/d,Y/d};}
 FVector2D operator*(double d)const{return {X*d,Y*d};}
 FVector2D&operator+=(const FVector2D&r){X+=r.X;Y+=r.Y;return *this;}
 FVector2D GetSafeNormal()const{double l=Size();return l>1e-9?(*this)/l:FVector2D();}
 double SizeSquared()const{return X*X+Y*Y;}
 double Size()const{return std::sqrt(X*X+Y*Y);}
 bool IsNearlyZero(double e=1.e-4)const{return std::abs(X)<=e&&std::abs(Y)<=e;}
 bool Equals(const FVector2D&r,double e)const{return std::abs(X-r.X)<=e&&std::abs(Y-r.Y)<=e;}
};
inline const FVector2D FVector2D::ZeroVector={0,0};

template<class T>struct TNumericLimits { static T Max(){return std::numeric_limits<T>::max();} };
struct FIntPoint { int32 X,Y; FIntPoint(int32 x,int32 y):X(x),Y(y){} };

template<class T>class TSharedPtr:public std::shared_ptr<T>{
public:
 using std::shared_ptr<T>::shared_ptr;
 TSharedPtr()=default;
 TSharedPtr(std::nullptr_t):std::shared_ptr<T>(nullptr){}
 TSharedPtr(const std::shared_ptr<T>&p):std::shared_ptr<T>(p){}
 template<class U>TSharedPtr(const TSharedPtr<U>&p):std::shared_ptr<T>(p){}
 bool IsValid()const{return this->get()!=nullptr;}
 TSharedPtr<T> ToSharedRef()const{if(!IsValid())throw std::runtime_error("null SharedRef");return *this;}
 T* Get()const{return this->get();}
 void Reset(){this->reset();}
};
template<class T>using TSharedRef=TSharedPtr<T>;
template<class T,class...A>TSharedPtr<T> MakeShared(A&&...a){return TSharedPtr<T>(std::make_shared<T>(std::forward<A>(a)...));}
template<class T>using TUniquePtr=std::unique_ptr<T>;
template<class T,class...A>TUniquePtr<T> MakeUnique(A&&...a){return std::make_unique<T>(std::forward<A>(a)...);}
struct FCString{static int32 Atoi(const TCHAR*s){return std::atoi(s);}};
inline uint32 GetTypeHash(const FString&s){return static_cast<uint32>(std::hash<std::string>{}(s));}

struct FMemory { static void* Memcpy(void* destination,const void* source,size_t size){return std::memcpy(destination,source,size);} };
