#!/usr/bin/env python3
"""Reproduce the reported TestEqual overload error and check the actual fix.

Optional C++ compiler check, not Unreal/UHT/UBT or a material-rendering test.
The numeric overload signatures and integer types come from Log(9).txt.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile


def main():
    project = Path(__file__).resolve().parents[2]
    source = project / 'Plugins/BackyardMMO/Source/BackyardMMOEditor/Private/Tests/BYMWorkerMaterialTests.cpp'
    text = source.read_text(encoding='utf-8')
    match = re.search(r'const int64 SelectedDirection=.*?;\s*TestEqual\(TEXT\("Facing selects one source cell"\).*?;', text, re.S)
    if not match:
        raise RuntimeError('Could not locate the actual corrected worker-cell assertion')
    fixed = match.group(0)
    broken = 'TestEqual(TEXT("Facing selects one source cell"),FMath::RoundToInt((UV.R*810.-.5)/27.),Direction);'
    adapters = r'''
#include <cmath>
#include <iostream>
#include <type_traits>
using int32 = int;
using int64 = long long;
using SIZE_T = unsigned long long;
static_assert(sizeof(int32)==4 && sizeof(int64)==8);
#define TEXT(value) L##value
struct FMath { static int64 RoundToInt(double value) { return std::llround(value); } };
struct FLinearColor { float R; };
static int Assertions=0, Failures=0;
bool TestEqual(const wchar_t*, int32 a, int32 b) { ++Assertions; Failures+=(a!=b); return a==b; }
bool TestEqual(const wchar_t*, int64 a, int64 b) { ++Assertions; Failures+=(a!=b); return a==b; }
bool TestEqual(const wchar_t*, SIZE_T a, SIZE_T b) { ++Assertions; Failures+=(a!=b); return a==b; }
bool TestEqual(const wchar_t*, float a, float b, float tolerance=.0001f) { ++Assertions; const bool ok=std::abs(a-b)<=tolerance; Failures+=!ok; return ok; }
bool TestEqual(const wchar_t*, double a, double b, double tolerance=.0001) { ++Assertions; const bool ok=std::abs(a-b)<=tolerance; Failures+=!ok; return ok; }
int main() {
    for(int32 Direction=0; Direction<30; ++Direction) {
        const FLinearColor UV{static_cast<float>((Direction*27.+.5)/810.)};
'''
    suffix = r'''
    }
    std::cout << Assertions << " worker-cell assertions, " << Failures << " failures\n";
    return Assertions==30 && Failures==0 ? 0 : 1;
}
'''
    with tempfile.TemporaryDirectory(prefix='bym-test-overloads-') as directory:
        root = Path(directory)
        compiler = os.environ.get('CXX', 'g++')
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Wshadow', '-Werror', '-pedantic']
        before = root / 'before.cpp'
        before.write_text(adapters + broken + suffix)
        result = subprocess.run([compiler, *flags, '-fsyntax-only', str(before)], capture_output=True, text=True)
        if result.returncode == 0 or 'ambiguous' not in result.stderr.lower():
            raise RuntimeError('The original expression did not reproduce the expected ambiguous overload')
        after = root / 'after.cpp'
        after.write_text(adapters + fixed + suffix)
        executable = root / ('after.exe' if os.name == 'nt' else 'after')
        subprocess.run([compiler, *flags, str(after), '-o', str(executable)], check=True)
        checked = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
        print('Original expression: ambiguous overload reproduced')
        print(checked.stdout.strip())
        print(json.dumps({'passed': True, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                          'unreal_execution': False}))


if __name__ == '__main__':
    main()
