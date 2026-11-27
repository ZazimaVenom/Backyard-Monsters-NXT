#!/usr/bin/env python3
"""Check the reported sprite atlas editor/game API boundary, not an Unreal build."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
BOARD_SOURCE = ROOT / 'Plugins' / 'BackyardMMO' / 'Source' / 'BackyardMMO' / 'Private' / 'BYMBoard.cpp'


def extract_texture_readiness(source: str) -> str:
    """Use the real block, including its preprocessor guards, without rewriting it."""
    function_marker = 'bool ABYMBoard::TryConfigureAtlas('
    lookup_marker = 'UTexture2D* Texture=TextureCache.FindRef(Visual.TexturePath).Get();'
    end_marker = '// Exact original PNG dimensions'
    function_start = source.find(function_marker)
    function_end = source.find('\nvoid ABYMBoard::', function_start)
    if function_start < 0:
        raise RuntimeError('Cannot find the actual TryConfigureAtlas implementation.')
    if function_end < 0:
        function_end = len(source)
    start = source.find(lookup_marker, function_start, function_end)
    end = source.find(end_marker, start, function_end) if start >= 0 else -1
    if start < 0 or end < 0:
        raise RuntimeError('Cannot locate the actual sprite texture readiness block; update this narrow contract explicitly if the implementation moves.')
    block = source[start:end]
    if 'IsDefaultTexture' not in block:
        raise RuntimeError('The editor placeholder-texture retry guard must remain in the actual readiness block.')
    return block


# Deliberately independent of the broad offline adapters. The packaged-game
# texture type does not declare the editor-only method at all.
PRELUDE = r'''
#include <iostream>
struct UTexture2D {
    bool Placeholder = false;
    int DefaultQueries = 0;
#if WITH_EDITOR
    bool IsDefaultTexture() { ++DefaultQueries; return Placeholder; }
#endif
};
struct FMesh {
    bool Visible = true;
    int VisibilityChanges = 0;
    void SetVisibility(bool Value) { Visible = Value; ++VisibilityChanges; }
};
struct FMeshRef {
    FMesh* Value;
    bool IsValid() const { return Value != nullptr; }
    FMesh* operator->() const { return Value; }
};
struct FVisual { const char* TexturePath; FMeshRef Mesh; };
struct FTextureRef { UTexture2D* Value; UTexture2D* Get() const { return Value; } };
struct FTextureCache {
    UTexture2D* Value;
    FTextureRef FindRef(const char*) const { return {Value}; }
};
'''


BEHAVIOR_CHECKS = r'''
static int Assertions = 0;
static bool Check(bool Condition, const char* Message) {
    ++Assertions;
    if (!Condition) std::cerr << Message << '\n';
    return Condition;
}
int main() {
    FMesh Mesh;
    FVisual Visual{"atlas.png", {&Mesh}};
    FTextureCache Cache{nullptr};
    if (!Check(!Probe(Visual, Cache), "A missing texture must retry")) return 1;
    if (!Check(!Mesh.Visible && Mesh.VisibilityChanges == 1, "A missing texture must hide the mesh")) return 1;
    FVisual NoMesh{"atlas.png", {nullptr}};
    if (!Check(!Probe(NoMesh, Cache), "A missing mesh must be safe while retrying")) return 1;

    UTexture2D Texture;
    Cache.Value = &Texture;
    Mesh = FMesh{};
    if (!Check(Probe(Visual, Cache), "A ready texture must continue configuration")) return 1;
    if (!Check(Mesh.Visible && Mesh.VisibilityChanges == 0, "A ready texture must not be hidden")) return 1;
#if WITH_EDITOR
    if (!Check(Texture.DefaultQueries == 1, "The editor must check the placeholder resource")) return 1;
#else
    if (!Check(Texture.DefaultQueries == 0, "The game must not use the editor texture query")) return 1;
#endif

    Texture.Placeholder = true;
    Texture.DefaultQueries = 0;
    Mesh = FMesh{};
#if WITH_EDITOR
    if (!Check(!Probe(Visual, Cache), "An editor placeholder must retry")) return 1;
    if (!Check(!Mesh.Visible && Mesh.VisibilityChanges == 1, "An editor placeholder must hide the mesh")) return 1;
    if (!Check(Texture.DefaultQueries == 1, "The editor placeholder check must run")) return 1;
    if (!Check(!Probe(NoMesh, Cache), "An editor placeholder without a mesh must be safe")) return 1;
#else
    if (!Check(Probe(Visual, Cache), "A cooked texture must not depend on editor placeholder state")) return 1;
    if (!Check(Mesh.Visible && Mesh.VisibilityChanges == 0, "The game texture must remain available")) return 1;
    if (!Check(Texture.DefaultQueries == 0, "There is no game placeholder query")) return 1;
    if (!Check(Probe(NoMesh, Cache), "A valid game texture without a mesh must be safe")) return 1;
#endif

    Texture.Placeholder = false;
    Mesh = FMesh{};
    if (!Check(Probe(Visual, Cache), "A previously deferred texture must configure when ready")) return 1;
    if (!Check(Mesh.Visible && Mesh.VisibilityChanges == 0, "Retry completion must not hide the ready mesh")) return 1;
    std::cout << Assertions << " assertions passed\n";
    return 0;
}
'''


# This is the exact old API usage reported by the Windows game build. It should
# remain legal in editor mode, but must fail when the method is absent in game.
OLD_UNGUARDED_BLOCK = r'''
UTexture2D* Texture=TextureCache.FindRef(Visual.TexturePath).Get();
if(!Texture||Texture->IsDefaultTexture())
{
    if(Visual.Mesh.IsValid())Visual.Mesh->SetVisibility(false);
    return false;
}
'''


def unit(block: str) -> str:
    return PRELUDE + '\nbool Probe(FVisual& Visual, const FTextureCache& TextureCache) {\n' + block + '\nreturn true;\n}\n' + BEHAVIOR_CHECKS


def run_checks(cxx: str = 'g++') -> dict[str, object]:
    checker = Path(__file__).resolve()
    checker_bytes = checker.read_bytes()
    original = BOARD_SOURCE.read_bytes()
    block = extract_texture_readiness(original.decode('utf-8'))
    cases = [
        ('actual_game', block, 0, True),
        ('actual_editor', block, 1, True),
        ('old_editor_control', OLD_UNGUARDED_BLOCK, 1, True),
        ('reject_old_game_call', OLD_UNGUARDED_BLOCK, 0, False),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix='bym-packaging-contracts-') as directory:
        for name, source_block, editor, should_pass in cases:
            cpp = Path(directory) / (name + '.cpp')
            executable = Path(directory) / name
            cpp.write_text(unit(source_block), encoding='utf-8')
            compiled = subprocess.run(
                [cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                 '-DWITH_EDITOR=' + str(editor), str(cpp), '-o', str(executable)],
                capture_output=True, text=True,
            )
            if (compiled.returncode == 0) != should_pass:
                raise RuntimeError(name + (' unexpectedly compiled.' if not should_pass else ' failed:\n' + compiled.stderr[-5000:]))
            if not should_pass:
                if 'IsDefaultTexture' not in compiled.stderr:
                    raise RuntimeError('The negative control failed for an unrelated reason:\n' + compiled.stderr[-5000:])
                results.append({'case': name, 'with_editor': editor, 'result': 'EXPECTED_COMPILE_FAILURE'})
                continue
            executed = subprocess.run([str(executable)], capture_output=True, text=True)
            if executed.returncode != 0:
                raise RuntimeError(name + ' behavior failed:\n' + executed.stderr[-5000:])
            if executed.stdout.strip() != '12 assertions passed':
                raise RuntimeError(name + ' did not execute all expected behavior assertions: ' + executed.stdout)
            results.append({'case': name, 'with_editor': editor, 'result': 'PASS', 'behavior_assertions': 12})
    if BOARD_SOURCE.read_bytes() != original:
        raise RuntimeError('BYMBoard.cpp changed during packaging-contract validation; rerun after edits finish.')
    if checker.read_bytes() != checker_bytes:
        raise RuntimeError('The packaging checker changed during validation; rerun after edits finish.')
    version = subprocess.run([cxx, '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    return {
        'result': 'PASS',
        'completed_at_utc': datetime.now(timezone.utc).isoformat(),
        'compiler': version,
        'source': str(BOARD_SOURCE.relative_to(ROOT)),
        'source_sha256': hashlib.sha256(original).hexdigest(),
        'checker': str(checker.relative_to(ROOT)),
        'checker_sha256': hashlib.sha256(checker_bytes).hexdigest(),
        'extracted_block_sha256': hashlib.sha256(block.encode('utf-8')).hexdigest(),
        'positive_compile_checks': 3,
        'required_compile_failures': 1,
        'actual_source_behavior_assertions': 24,
        'historical_editor_behavior_assertions': 12,
        'cases': results,
        'scope': 'Actual sprite texture-readiness block with WITH_EDITOR=0/1 against strict minimal texture/mesh stubs. Not an Unreal/UBT/UHT build, cook, or render test.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--record', action='store_true', help='Record this focused check separately from the simulation manifest.')
    args = parser.parse_args()
    report = json.dumps(run_checks(args.cxx), indent=2) + '\n'
    if args.record:
        Path(__file__).with_name('packaging-contract-results.json').write_text(report, encoding='utf-8')
    print(report, end='')
