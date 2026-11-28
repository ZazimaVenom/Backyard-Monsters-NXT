#!/usr/bin/env python3
"""Exercise actual transport helpers and compare real simulation payload bytes.

Optional developer-only validation. Not Unreal, networking or Windows packaging.
Requires a C++17 compiler, Python 3.9+ and system zlib development files.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
RUNTIME = ROOT / 'Plugins' / 'BackyardMMO' / 'Source' / 'BackyardMMO'
ADAPTERS = HERE / 'Adapters'
PRODUCTION = [
    'BYMSimulation.cpp', 'BYMExperience.cpp', 'BYMEconomy.cpp', 'BYMCombatRules.cpp',
    'BYMBuildingLifecycle.cpp', 'BYMBunkerRules.cpp', 'BYMYardResidents.cpp',
    'BYMBuildingAnimation.cpp', 'BYMSpriteAtlas.cpp', 'BYMWorkerMovement.cpp',
    'BYMBotLayout.cpp', 'BYMMapRoomRules.cpp', 'BYMNetworkSnapshot.cpp', 'BYMNetworkStream.cpp',
]


def inputs() -> set[Path]:
    result = {Path(__file__).resolve(), HERE / 'NetworkBenchmark.cpp', RUNTIME / 'Private/Tests/BYMNetworkSnapshotTests.cpp', RUNTIME / 'Public/BYMSettings.h'}
    result.update(RUNTIME / 'Private' / name for name in PRODUCTION)
    result.update((RUNTIME / 'Private').glob('BYMSimulation*.inl'))
    result.add(RUNTIME / 'Private/BYMExperienceThresholds.inl')
    pending = list(result)
    while pending:
        source = pending.pop()
        for include in re.findall(r'^\s*#include\s+"([^"]+)"', source.read_text(encoding='utf-8'), re.MULTILINE):
            for parent in (source.parent, RUNTIME / 'Public', RUNTIME / 'Private/Tests', ADAPTERS):
                candidate = parent / include
                if candidate.is_file():
                    if candidate not in result:
                        result.add(candidate); pending.append(candidate)
                    break
    result.update(path for path in ADAPTERS.rglob('*') if path.is_file() and '__pycache__' not in path.parts)
    result.update(ROOT / 'Plugins/BackyardMMO/Content/Data' / name for name in ('catalog.json', 'experience.json', 'store-rules.json', 'maproom-rules.json', 'maproom-world.json'))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    files = inputs()
    def hashes():
        return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(files)}
    before = hashes()
    with tempfile.TemporaryDirectory(prefix='bym-network-benchmark-') as temporary:
        build = Path(temporary)
        fields = re.findall(r'^\s*((?:FString|int32|float|bool)\s+\w+\s*=\s*[^;]+;)', (RUNTIME / 'Public/BYMSettings.h').read_text(), re.MULTILINE)
        if not fields: raise RuntimeError('Could not extract the real project settings defaults')
        (build / 'BYMSettings.h').write_text('#pragma once\n#include "CoreMinimal.h"\nstruct UBYMSettings {\n' + '\n'.join(fields) + '\n};\ntemplate<class T>T* GetMutableDefault(){static T Value;return &Value;}\ntemplate<class T>const T* GetDefault(){return GetMutableDefault<T>();}\n')
        version = subprocess.run([args.cxx, '--version'], text=True, capture_output=True, check=True).stdout.splitlines()[0]
        flags = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Wshadow', '-Werror', '-Wno-unused-parameter', '-Wno-misleading-indentation']
        if 'clang' not in version.lower(): flags += ['-Wno-dangling-reference', '-Wno-error=range-loop-construct']
        executable = build / 'benchmark'
        command = [args.cxx, *flags, '-I' + str(build), '-I' + str(ADAPTERS), '-I' + str(RUNTIME / 'Public'), '-I' + str(RUNTIME / 'Private/Tests'), str(HERE / 'NetworkBenchmark.cpp')]
        command += [str(RUNTIME / 'Private' / name) for name in PRODUCTION] + ['-lz', '-o', str(executable)]
        print('Compiling actual codec, stream, native tests and host simulation with explicit offline adapters...', flush=True)
        compiled = subprocess.run(command)
        if compiled.returncode: return compiled.returncode
        frames = build / 'frames.jsonl'
        env = os.environ.copy(); env['BYM_OFFLINE_CONTENT'] = str(ROOT / 'Plugins/BackyardMMO/Content')
        executed = subprocess.run([str(executable), str(frames)], env=env, text=True, capture_output=True)
        print(executed.stdout, end='', flush=True)
        if executed.returncode:
            print(executed.stderr, end='', flush=True); return executed.returncode
        native = re.search(r'NETWORK_TESTS (\d+) (\d+) (\d+)', executed.stdout)
        if not native or int(native[3]): raise RuntimeError('Native network tests did not complete successfully')
        grouped = defaultdict(list)
        verified = 0
        for line in frames.read_text().splitlines():
            frame = json.loads(line)
            original, reconstructed = json.loads(frame.pop('before')), json.loads(frame.pop('after'))
            if original != reconstructed: raise RuntimeError('A reconstructed real gameplay snapshot differs: ' + frame['case'])
            verified += 1; grouped[frame['case']].append(frame)
        report_cases = []
        for name, samples in grouped.items():
            old = sum(row['old_json_utf8_bytes'] for row in samples)
            encoded = sum(row['encoded_bytes'] for row in samples)
            recurring_old = sum(row['old_json_utf8_bytes'] for row in samples[1:])
            recurring_new = sum(row['encoded_bytes'] for row in samples[1:])
            report_cases.append({
                'case': name, 'frames': len(samples), 'duration_seconds': 60,
                'old_full_json_utf8_bytes': old,
                'compressed_full_only_bytes': sum(row['compressed_full_bytes'] for row in samples),
                'compressed_delta_bytes': encoded,
                'reduction_percent': round((1 - encoded / old) * 100, 3),
                'first_frame_old_bytes': samples[0]['old_json_utf8_bytes'],
                'first_frame_new_bytes': samples[0]['encoded_bytes'],
                'recurring_old_bytes': recurring_old, 'recurring_new_bytes': recurring_new,
                'recurring_reduction_percent': round((1 - recurring_new / recurring_old) * 100, 3),
                'delta_frames': sum(row['delta'] for row in samples),
                'suppressed_frames': sum(not row['has_update'] for row in samples),
                'maximum_encoded_frame_bytes': max(row['encoded_bytes'] for row in samples),
            })
        changed = [name for name, digest in before.items() if hashes().get(name) != digest]
        if changed: raise RuntimeError('Validation inputs changed during run: ' + ', '.join(changed))
        report = {
            'result': 'PASS', 'completed_at_utc': datetime.now(timezone.utc).isoformat(), 'compiler': version,
            'native_network_test_bodies': int(native[1]), 'native_network_assertions': int(native[2]), 'failures': 0,
            'real_snapshots_independently_compared': verified, 'cases': report_cases,
            'scope': 'Actual production simulation JSON and actual encoding/reconstruction helpers using explicit offline Unreal adapters. UTF-8 application payload bytes only; excludes Unreal RPC/packet headers, ACKs, retransmission and other traffic. Not socket bandwidth, a native Unreal build, or a capacity test.',
            'compression': 'System zlib level 1 adapter for Unreal FCompression NAME_Zlib / COMPRESS_BiasSpeed; runtime has no extra zlib dependency beyond Unreal Core.',
            'unicode_scope': 'Portable TCHAR strings are UTF-8 bytes; native tests include Unicode but engine UTF-16 conversion is not executed here.',
            'fixture_scope': 'Real starter yards, actual production and hatchery commands, a real ten-monster invasion with retained army reserves, and source-data MR3 map responses. No socket-level host metadata or UI enrichment included.',
            'inputs_changed_during_run': [], 'sha256': before,
        }
        if args.record:
            (HERE / 'network-transport-results.json').write_text(json.dumps(report, indent=2) + '\n')
            (HERE / 'network-transport-run.txt').write_text('Offline adapters only; not Unreal/socket execution.\n' + executed.stdout)
        print(json.dumps({key: value for key, value in report.items() if key != 'sha256'}, indent=2), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
