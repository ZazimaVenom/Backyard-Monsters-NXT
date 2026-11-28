#pragma once
#include "CoreMinimal.h"
#include <zlib.h>
inline constexpr const char* NAME_Zlib="Zlib";
enum ECompressionFlags { COMPRESS_NoFlags=0, COMPRESS_BiasSpeed=0x10 };
// Test-only binding to system zlib. Runtime game code uses Unreal's FCompression.
struct FCompression {
 static bool CompressMemoryBound(const char* format,int64& bound,int64 size,uintptr_t data){
  (void)data;if(std::strcmp(format,"Zlib")||size<0||size>INT32_MAX)return false;
  bound=static_cast<int64>(compressBound(static_cast<uLong>(size)));return true;
 }
 static bool CompressMemory(const char* format,void* compressed,int64& compressedSize,const void* raw,int64 rawSize,ECompressionFlags flags,uintptr_t data){
  (void)data;if(std::strcmp(format,"Zlib")||compressedSize<0||rawSize<0||rawSize>INT32_MAX)return false;
  uLongf output=static_cast<uLongf>(compressedSize);
  const int result=compress2(static_cast<Bytef*>(compressed),&output,static_cast<const Bytef*>(raw),static_cast<uLong>(rawSize),flags==COMPRESS_BiasSpeed?Z_BEST_SPEED:Z_DEFAULT_COMPRESSION);
  compressedSize=static_cast<int64>(output);return result==Z_OK;
 }
 static bool UncompressMemory(const char* format,void* raw,int64 rawSize,const void* compressed,int64 compressedSize,ECompressionFlags flags,uintptr_t data){
  (void)flags;(void)data;if(std::strcmp(format,"Zlib")||rawSize<0||compressedSize<0||rawSize>INT32_MAX||compressedSize>INT32_MAX)return false;
  uLongf output=static_cast<uLongf>(rawSize);
  const int result=uncompress(static_cast<Bytef*>(raw),&output,static_cast<const Bytef*>(compressed),static_cast<uLong>(compressedSize));
  return result==Z_OK&&output==static_cast<uLongf>(rawSize);
 }
};
