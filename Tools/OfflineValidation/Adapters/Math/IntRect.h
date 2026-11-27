#pragma once
#include "CoreMinimal.h"
struct FIntRect{
 FIntPoint Min,Max;
 FIntRect():Min{0,0},Max{0,0}{}
 FIntRect(int32 x0,int32 y0,int32 x1,int32 y1):Min{x0,y0},Max{x1,y1}{}
 int32 Width()const{return Max.X-Min.X;}
 int32 Height()const{return Max.Y-Min.Y;}
};
