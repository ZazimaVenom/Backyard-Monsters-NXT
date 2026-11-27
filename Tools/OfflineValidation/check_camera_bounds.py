#!/usr/bin/env python3
"""Check the actual rectangular-yard camera constraint with math-only adapters.

This compiles one extracted production function, not Unreal or the camera class.
Optional developer validation; not required to open the .uproject.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile


def main():
    project = Path(__file__).resolve().parents[2]
    source = project / 'Plugins/BackyardMMO/Source/BackyardMMO/Private/BYMCameraPawn.cpp'
    text = source.read_text()
    start = text.index('void ABYMCameraPawn::ConstrainViewToGroundBounds(')
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    function = text[start:end]
    adapters = r'''
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
struct FVector { double X=0,Y=0,Z=0; };
struct FVector2D {
 double X,Y;
 FVector2D(double x,double y):X(x),Y(y){}
 FVector2D operator+(const FVector2D& p)const{return {X+p.X,Y+p.Y};}
 FVector2D operator-(const FVector2D& p)const{return {X-p.X,Y-p.Y};}
};
struct FMath {
 template<class T>static bool IsFinite(T v){return std::isfinite(v);}
 template<class T>static T Min(T a,T b){return std::min(a,b);}
 template<class T>static T Max(T a,T b){return std::max(a,b);}
 template<class T>static T Clamp(T v,T a,T b){return std::clamp(v,a,b);}
};
struct ABYMCameraPawn {
 static void ConstrainViewToGroundBounds(FVector&,float&,float,const FVector2D&,const FVector2D&,float,float);
};
'''
    tests = r'''
int main(){
 unsigned long long cases=0;
 const double aspects[]={.025,.1,.25,.5,9./16.,1.,4./3.,16./9.,21./9.,4.,20.};
 const float widths[]={1000,2400,6200,15000,20000};
 const float units[]={20,100,1000};
 const int sizes[][2]={{50,40},{55,44},{61,49},{67,54},{74,59},{81,65},{89,71},{125,100},{222,178}};
 for(const auto& size:sizes)for(float unit:units)for(double aspect:aspects)for(float width:widths)
 for(int point=0;point<9;++point){
  // Project each rectangle's corners using the source's 2:1 isometric transform.
  const double minX=std::floor((50-size[0])*.5),minY=std::floor((40-size[1])*.5);
  const double maxX=minX+size[0],maxY=minY+size[1];
  const FVector2D yardMin((minX-maxY+20)*unit,(minX+minY)*unit*.5);
  const FVector2D yardMax((maxX-minY+20)*unit,(maxX+maxY)*unit*.5);
  const float margin=std::max(8192.f*unit/20.f,20000.f);
  FVector position{(point%3-1)*10000000.,(point/3-1)*10000000.,100.};
  float actualWidth=width;
  ABYMCameraPawn::ConstrainViewToGroundBounds(position,actualWidth,static_cast<float>(aspect),yardMin,yardMax,margin,unit*20);
  const double halfW=actualWidth*.5,halfH=halfW/static_cast<float>(aspect);
  // Float orthographic width has finite rounding; allow 0.1 world units.
  const double tolerance=.1;
  if(!std::isfinite(actualWidth)||actualWidth<=0||actualWidth>width||position.Z!=0
   ||position.X-halfW<yardMin.X-margin+1-tolerance||position.X+halfW>yardMax.X+margin-1+tolerance
   ||position.Y-halfH<yardMin.Y-margin+1-tolerance||position.Y+halfH>yardMax.Y+margin-1+tolerance){
    std::cerr<<"Viewport escaped terrain: "<<size[0]<<'x'<<size[1]<<" unit "<<unit<<" aspect "<<aspect<<" width "<<width<<'\n';return 1;
  }
  ++cases;
 }
 // Invalid requested width/aspect must recover to finite defaults.
 FVector position{};float width=std::numeric_limits<float>::quiet_NaN();
 ABYMCameraPawn::ConstrainViewToGroundBounds(position,width,std::numeric_limits<float>::quiet_NaN(),{-1000,-1000},{1000,1000},20000,2000);
 if(!std::isfinite(width)||width!=1000)return 1;
 std::cout<<cases+1<<" camera-bound cases passed (extracted function, math adapters only)\n";
}
'''
    with tempfile.TemporaryDirectory(prefix='bym-camera-bounds-') as directory:
        root = Path(directory)
        cpp = root / 'check.cpp'
        cpp.write_text(adapters + '\n' + function + '\n' + tests)
        executable = root / ('check.exe' if os.name == 'nt' else 'check')
        compiler = os.environ.get('CXX', 'g++')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Wshadow', '-Werror', str(cpp), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
        print(result.stdout.strip())
        print(json.dumps({'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                          'function_sha256': hashlib.sha256(function.encode()).hexdigest(),
                          'unreal_execution': False}))


if __name__ == '__main__':
    main()
