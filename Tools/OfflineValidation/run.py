#!/usr/bin/env python3
"""Execute unchanged native runtime test bodies with explicit non-Unreal adapters.

Optional developer validation only. Never needed to open, build or play the game.
Python 3.9+ and a C++17 compiler (tested with GCC) are the only dependencies.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from check_compile_contracts import input_paths as compile_contract_inputs, run_checks as check_compile_contracts

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "Plugins" / "BackyardMMO" / "Source" / "BackyardMMO"
ADAPTERS = Path(__file__).resolve().parent / "Adapters"
EXCLUDED = {"BYMAccountSaveGameTests.cpp", "BYMPasswordTests.cpp"}
PRODUCTION = [
    "BYMSimulation.cpp", "BYMExperience.cpp", "BYMEconomy.cpp",
    "BYMCombatRules.cpp", "BYMBuildingLifecycle.cpp", "BYMBunkerRules.cpp",
    "BYMYardResidents.cpp", "BYMBuildingAnimation.cpp", "BYMSpriteAtlas.cpp", "BYMWorkerMovement.cpp", "BYMBotLayout.cpp", "BYMMapRoomRules.cpp",
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"), help="C++17 compiler executable")
    parser.add_argument("--filter", default="", help="Only execute tests whose names contain this substring")
    parser.add_argument("--record", action="store_true", help="Write this run's output and exact input hashes beside the runner")
    args = parser.parse_args()
    tests = sorted(p for p in (RUNTIME / "Private" / "Tests").glob("*.cpp") if p.name not in EXCLUDED)
    if not tests:
        parser.error("Plugin runtime tests not found beside this validation tool")
    inputs = set(tests)
    inputs.update(RUNTIME / "Private" / name for name in PRODUCTION)
    inputs.update((RUNTIME / "Private").glob("BYMSimulation*.inl"))
    inputs.add(RUNTIME / "Private" / "BYMExperienceThresholds.inl")
    inputs.add(RUNTIME / "Public" / "BYMSettings.h")
    # Record the local quoted-header dependency closure, not unrelated UI or
    # actor files that this non-Unreal executable cannot compile or validate.
    pending = list(inputs)
    while pending:
        source = pending.pop()
        for included in re.findall(r'^\s*#include\s+"([^"]+)"', source.read_text(encoding="utf-8"), re.MULTILINE):
            for directory in (source.parent, ADAPTERS, RUNTIME / "Public", RUNTIME / "Private"):
                candidate = directory / included
                if candidate.is_file():
                    if candidate not in inputs:
                        inputs.add(candidate)
                        pending.append(candidate)
                    break
    inputs.update(path for path in ADAPTERS.rglob("*") if path.is_file())
    inputs.update(ROOT / "Plugins" / "BackyardMMO" / "Content" / "Data" / name
                  for name in ("catalog.json", "experience.json", "store-rules.json", "maproom-rules.json", "maproom-world.json"))
    inputs.add(Path(__file__).resolve())
    inputs.update(compile_contract_inputs())
    def hashes() -> dict[str, str]:
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(inputs)}
    initial_hashes = hashes()
    try:
        compile_contracts = check_compile_contracts(args.cxx)
    except RuntimeError as error:
        print("Compile-contract regression failed: " + str(error), flush=True)
        return 1
    print("Compile contracts passed: 3 corrected snippets, 5 required compiler rejections, direct cmath include guard. Not an Unreal build.", flush=True)
    with tempfile.TemporaryDirectory(prefix="bym-offline-validation-") as temp:
        build = Path(temp)
        # Read current production defaults, never maintain a parallel settings table.
        settings = (RUNTIME / "Public" / "BYMSettings.h").read_text(encoding="utf-8")
        fields = re.findall(r"^\s*((?:FString|int32|float|bool)\s+\w+\s*=\s*[^;]+;)", settings, re.MULTILINE)
        if not fields:
            parser.error("Could not extract current host settings defaults")
        (build / "BYMSettings.h").write_text(
            '#pragma once\n#include "CoreMinimal.h"\nstruct UBYMSettings {\n'
            + "\n".join(fields) + '\n};\n'
            + 'template<class T>T* GetMutableDefault(){static T Value;return &Value;}\n'
            + 'template<class T>const T* GetDefault(){return GetMutableDefault<T>();}\n', encoding="utf-8")
        includes = '\n'.join('#include "' + p.as_posix() + '"' for p in tests)
        (build / "main.cpp").write_text('#include "Misc/AutomationTest.h"\n' + includes + r'''
int main(int argc,char** argv) {
    int Executed=0, FailedTests=0;
    const std::string Filter=argc>1?argv[1]:"";
    for(const auto& Test:OfflineTests()) {
        if(std::string(Test.Name).find(Filter)==std::string::npos) continue;
        ++Executed; const int Before=Failures;
        try {
            const bool Ok=Test.Run();
            if(!Ok&&Failures==Before) {++Failures;std::cerr<<"Returned false: "<<Test.Name<<'\n';}
        } catch(const std::exception& Error) {++Failures;std::cerr<<"Exception: "<<Error.what()<<'\n';}
        if(Failures!=Before) ++FailedTests;
        std::cout<<(Failures==Before?"PASS ":"FAIL ")<<Test.Name<<std::endl;
    }
    std::cout<<Executed<<" native test bodies, "<<Assertions<<" assertions, "<<Failures
        <<" assertion failures, "<<FailedTests<<" failing tests\n";
    return Failures||Executed==0?1:0;
}
''', encoding="utf-8")
        executable = build / ("validation.exe" if os.name == "nt" else "validation")
        version = subprocess.run([args.cxx, "--version"], check=True, text=True, capture_output=True).stdout
        flags = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Wshadow", "-Werror", "-Wno-unused-parameter", "-Wno-misleading-indentation"]
        # GCC diagnoses references through temporary shared-pointer handles even
        # when the original save object owns the referenced array for its lifetime.
        if "clang" not in version.lower():
            flags.append("-Wno-dangling-reference")
            # The real UE 5.8.4 log accepts explicitly typed JSON pair
            # iteration. Our shared-key pair-copy adapter makes GCC warn
            # about that conversion's temporary reference. Keep the warning
            # visible without treating this adapter-specific warning as an
            # engine error; every other enabled warning remains an error.
            flags.append("-Wno-error=range-loop-construct")
        command = [args.cxx, *flags, "-I" + str(build), "-I" + str(ADAPTERS), "-I" + str(RUNTIME / "Public"), str(build / "main.cpp")]
        command.extend(str(RUNTIME / "Private" / name) for name in PRODUCTION)
        command.extend(["-o", str(executable)])
        print("Compiling current production simulation and unchanged native test bodies with test-only adapters...", flush=True)
        compiled = subprocess.run(command, check=False)
        if compiled.returncode:
            return compiled.returncode
        environment = os.environ.copy()
        environment["BYM_OFFLINE_CONTENT"] = str(ROOT / "Plugins" / "BackyardMMO" / "Content")
        print("Executing offline tests. This is not an Unreal/UHT/UBT build or engine run.", flush=True)
        lines = []
        process = subprocess.Popen([str(executable), args.filter], env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            lines.append(line)
        result = process.wait()
        output = "".join(lines)
        final_hashes = hashes()
        changed = [name for name, digest in initial_hashes.items() if final_hashes.get(name) != digest]
        if changed:
            print("Validation inputs changed during this run; rerun after edits finish: " + ", ".join(changed), flush=True)
            result = result or 3
        if args.record:
            report = Path(__file__).resolve().parent
            report.joinpath("last-run.txt").write_text(
                "Actual production simulation and native test bodies with test-only adapters. Not an Unreal engine run.\n" + output,
                encoding="utf-8")
            summary = re.search(r"(\d+) native test bodies, (\d+) assertions, (\d+) assertion failures, (\d+) failing tests", output)
            editor = ROOT / "Plugins" / "BackyardMMO" / "Source" / "BackyardMMOEditor"
            count_tests = lambda paths: sum(len(re.findall(r"IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(", path.read_text(encoding="utf-8"))) for path in paths)
            editor_count = count_tests(editor.rglob("*Tests.cpp"))
            runtime_count = count_tests((RUNTIME / "Private" / "Tests").glob("*.cpp"))
            manifest = {
                "result": "PASS" if result == 0 and summary else "FAIL",
                "compiler": version.splitlines()[0],
                "native_runtime_test_bodies_executed": int(summary[1]) if summary else 0,
                "assertions": int(summary[2]) if summary else 0,
                "assertion_failures": int(summary[3]) if summary else 0,
                "failing_tests": int(summary[4]) if summary else 0,
                "total_native_automation_tests_in_source": runtime_count + editor_count,
                "scope": "Actual complete simulation and native runtime test bodies with explicitly substituted Unreal API adapters; not an Unreal UHT/UBT or engine run.",
                "filter": args.filter,
                "compile_contracts": compile_contracts,
                "excluded_runtime_tests": ["BackyardMMO.Accounts.PasswordVerifier", "BackyardMMO.Persistence.NativeAccountSaveGame"],
                "excluded_editor_tests": editor_count,
                "inputs_changed_during_run": changed,
                "sha256": initial_hashes,
            }
            report.joinpath("last-run-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return result


if __name__ == "__main__":
    raise SystemExit(main())
