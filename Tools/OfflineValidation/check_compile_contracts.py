#!/usr/bin/env python3
"""Narrow regressions for concrete UE 5.8 compile failures; not an engine build."""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'Plugins' / 'BackyardMMO' / 'Source' / 'BackyardMMO'
ADAPTERS = Path(__file__).resolve().parent / 'Adapters'
MATH_TEST = RUNTIME / 'Private' / 'Tests' / 'BYMMapAuthorityTests.cpp'
SLATE_SOURCE = RUNTIME / 'Private' / 'SBYMWorldMap.cpp'


def input_paths() -> set[Path]:
    return {Path(__file__).resolve(), MATH_TEST, SLATE_SOURCE}


def check_own_math_header(source: str) -> None:
    if re.search(r'\bstd::(?:floor|ceil|sin|cos|atan2|trunc|sqrt|isfinite)\s*\(', source):
        if not re.search(r'^\s*#\s*include\s*<cmath>', source, re.MULTILINE):
            raise RuntimeError('BYMMapAuthorityTests.cpp must include <cmath> directly; the portable CoreMinimal adapter cannot establish UE/PCH transitive includes.')


def run_checks(cxx: str = 'g++') -> dict[str, object]:
    source = MATH_TEST.read_text(encoding='utf-8')
    check_own_math_header(source)
    # Prove the guard catches the actual reported missing-include version.
    try:
        check_own_math_header(re.sub(r'^\s*#\s*include\s*<cmath>\s*$', '', source, flags=re.MULTILINE))
    except RuntimeError:
        pass
    else:
        raise RuntimeError('Missing-cmath negative control was not rejected.')

    slate = SLATE_SOURCE.read_text(encoding='utf-8')
    marker = 'double LabelWidth=Label.Len()*7.;'
    begin = slate.find(marker)
    if begin < 0:
        raise RuntimeError('Cannot locate the actual map-label renderer guard for the compile contract.')
    begin += len(marker)
    end = slate.find('{', begin)
    renderer_guard = slate[begin:end] + '{ (void)0; }'
    mouse_start = slate.find('FReply SBYMWorldMap::OnMouseButtonDown(')
    mouse_end = slate.find('FReply SBYMWorldMap::OnMouseMove(', mouse_start)
    focus = re.search(r'return\s+FReply::Handled\(\)[^;]+;', slate[mouse_start:mouse_end])
    if not focus:
        raise RuntimeError('Cannot locate the actual map mouse-focus return expression.')

    json_prelude = r'''
#include "Dom/JsonObject.h"
#include <type_traits>
static_assert(!std::is_convertible<FOfflineJsonObjectKey, FString>::value,
              "JSON iteration keys must not silently become FString");
static_assert(!std::is_convertible<FOfflineJsonObjectKey, const TCHAR*>::value,
              "Dereference JSON keys explicitly");
'''
    json_setup = 'FJsonObject Object; Object.SetNumberField(TEXT("C1"),1); TMap<FString,int32> Gameplay; for(const auto& Pair : Object.Values)'
    slate_prelude = r'''
struct FSlateRenderer {};
struct FSlateApplication {
    static bool IsInitialized();
    static FSlateApplication& Get();
    FSlateRenderer* GetRenderer();
};
enum class EFocusCause { Mouse };
struct FReply {
    static FReply Handled();
    FReply& CaptureMouse(int);
    FReply& SetUserFocus(int, EFocusCause, bool);
};
template<class T> int SharedThis(T*) { return 0; }
'''
    cases = [
        ('explicit_json_key', json_prelude + 'void Probe(){' + json_setup + '{const FString Key(*Pair.Key); Gameplay.Add(Key,1); (void)Gameplay.Contains(Key); (void)(Key==FString(TEXT("C1")));} for(const TPair<FString,TSharedPtr<FJsonValue>>& Typed : Object.Values){(void)Gameplay.Contains(Typed.Key);}}', True),
        ('actual_renderer_pointer_guard', slate_prelude + 'void Probe(){' + renderer_guard + '}', True),
        ('actual_reply_user_focus', slate_prelude + 'struct SBYMWorldMap { FReply Probe(){' + focus.group(0) + '} };', True),
        ('reject_implicit_json_contains', json_prelude + 'void Probe(){' + json_setup + '{(void)Gameplay.Contains(Pair.Key);}}', False),
        ('reject_implicit_json_add', json_prelude + 'void Probe(){' + json_setup + '{Gameplay.Add(Pair.Key,1);}}', False),
        ('reject_implicit_json_equality', json_prelude + 'void Probe(){' + json_setup + '{(void)(FString(TEXT("C1"))==Pair.Key);}}', False),
        ('reject_renderer_shared_pointer_api', slate_prelude + 'void Probe(){(void)FSlateApplication::Get().GetRenderer().IsValid();}', False),
        ('reject_reply_keyboard_focus_api', slate_prelude + 'struct SBYMWorldMap { FReply Probe(){return FReply::Handled().CaptureMouse(SharedThis(this)).SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);} };', False),
    ]
    with tempfile.TemporaryDirectory(prefix='bym-compile-contracts-') as temporary:
        for name, text, should_pass in cases:
            path = Path(temporary) / (name + '.cpp')
            path.write_text(text, encoding='utf-8')
            result = subprocess.run([cxx, '-std=c++17', '-fsyntax-only', '-I' + str(ADAPTERS), str(path)], capture_output=True, text=True)
            if (result.returncode == 0) != should_pass:
                raise RuntimeError(name + (' unexpectedly compiled.' if not should_pass else ' failed:\n' + result.stderr[-5000:]))
    return {
        'result': 'PASS',
        'positive_compile_checks': 3,
        'required_compile_failures': 5,
        'direct_cmath_include_checks': 1,
        'missing_cmath_negative_controls': 1,
        'scope': 'Concrete JSON-key and extracted Slate-expression contracts against strict stubs, plus direct math include ownership. Not an Unreal/UBT/UHT compile or Slate runtime test.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    print(run_checks(args.cxx))
