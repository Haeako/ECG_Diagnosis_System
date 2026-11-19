"""Check CMake profile registration without an ESP-IDF install or a compiler.

Run with ``python main/cmake/check_profiles.py`` from the project directory.
This checks configuration only; the generated QRS header is never compiled.
"""

import argparse
import itertools
import json
import re
import shutil
import subprocess
import uuid
from contextlib import contextmanager
from pathlib import Path


MAIN = Path(__file__).resolve().parents[1]


@contextmanager
def scratch_directory():
    # Inherit workspace permissions, including Windows sandbox access rules.
    scratch = (MAIN / f".profile-check-{uuid.uuid4().hex}").resolve()
    assert scratch.parent == MAIN and scratch.name.startswith(".profile-check-")
    scratch.mkdir()
    try:
        yield scratch
    finally:
        shutil.rmtree(scratch)


PROBE = r"""
cmake_minimum_required(VERSION 3.16)
set(COMPONENT_LIB profile_check)
function(idf_component_register)
    cmake_parse_arguments(component "" "" "SRCS;INCLUDE_DIRS;PRIV_REQUIRES;REQUIRES;EMBED_FILES" ${ARGN})
    get_property(count GLOBAL PROPERTY CHECK_REGISTER_COUNT)
    if(NOT count)
        set(count 0)
    endif()
    math(EXPR count "${count} + 1")
    set_property(GLOBAL PROPERTY CHECK_REGISTER_COUNT ${count})
    foreach(key SRCS INCLUDE_DIRS PRIV_REQUIRES REQUIRES EMBED_FILES)
        set_property(GLOBAL PROPERTY CHECK_${key} "${component_${key}}")
    endforeach()
endfunction()
function(target_compile_definitions target scope)
    set_property(GLOBAL APPEND PROPERTY CHECK_DEFINITIONS ${ARGN})
endfunction()
function(target_include_directories target scope)
    set_property(GLOBAL APPEND PROPERTY CHECK_INCLUDES ${ARGN})
endfunction()
include("${CHECK_MAIN}/CMakeLists.txt")
file(WRITE "${CHECK_OUTPUT}" "")
foreach(key SRCS INCLUDE_DIRS PRIV_REQUIRES REQUIRES EMBED_FILES DEFINITIONS INCLUDES REGISTER_COUNT)
    get_property(value GLOBAL PROPERTY CHECK_${key})
    file(APPEND "${CHECK_OUTPUT}" "${key}=${value}\n")
endforeach()
foreach(key ECG_BUILD_PROFILE ECG_ENABLE_TFLM _ecg_build_profiles _ecg_legacy_profile_options)
    file(APPEND "${CHECK_OUTPUT}" "${key}=${${key}}\n")
endforeach()
"""


PROJECT_PROBE = r"""
cmake_minimum_required(VERSION 3.16)
set(CMAKE_BINARY_DIR "${CHECK_BINARY_DIR}")
file(WRITE "${CHECK_OUTPUT}" "")
set(step 0)
foreach(profile IN LISTS CHECK_PROFILES)
    set(ECG_BUILD_PROFILE "${profile}")
    if(step IN_LIST CHECK_CLEAR_STEPS)
        set(SDKCONFIG "" CACHE FILEPATH "Explicitly cleared configuration" FORCE)
    endif()
    include("${CHECK_MAIN}/cmake/EcgProjectOptions.cmake")
    file(APPEND "${CHECK_OUTPUT}" "${SDKCONFIG}|${ECG_AUTO_SDKCONFIG}\n")
    math(EXPR step "${step} + 1")
endforeach()
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake", help="CMake executable")
    args = parser.parse_args()
    checks = 0
    presets = json.loads((MAIN.parent / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    # ESP-IDF chooses the first listed preset when none is supplied.
    assert presets[0]["name"] == "bpm-runtime" and not presets[0].get("hidden", False)
    assert not any(p["name"] == "production" for p in presets)

    with scratch_directory() as scratch:
        script = scratch / "probe.cmake"
        script.write_text(PROBE, encoding="utf-8")
        output = scratch / "registration.txt"
        vectors = scratch / "vectors"
        vectors.mkdir()
        (vectors / "qrs_common_test_vectors.h").write_text(
            "/* Configuration fixture only; never compiled. */\n", encoding="utf-8"
        )
        (vectors / "bpm_candidates_vectors.h").write_text(
            "/* Configuration fixture only; never compiled. */\n", encoding="utf-8"
        )

        def probe(options=None, error=None):
            nonlocal checks
            options = options or {}
            options = {"QRS_COMMON_INPUT_DIR": vectors.as_posix(), **options}
            command = [
                args.cmake,
                f"-DCHECK_MAIN={MAIN.as_posix()}",
                f"-DCHECK_OUTPUT={output.as_posix()}",
                *[f"-D{key}={value}" for key, value in options.items()],
                "-P",
                str(script),
            ]
            run = subprocess.run(command, capture_output=True, text=True, check=False)
            checks += 1
            log = run.stdout + run.stderr
            if error is not None:
                assert run.returncode != 0, f"Configuration should fail: {options}"
                assert error in log, f"Wrong error for {options}:\n{log}"
                return None
            assert run.returncode == 0, f"Configuration failed: {options}\n{log}"
            return {
                key: value.split(";") if value else []
                for key, value in (
                    line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines()
                )
            }

        def assert_registration(result, profile):
            assert result["REGISTER_COUNT"] == ["1"], profile
            sources = result["SRCS"]
            assert sources and len(sources) == len(set(sources)), profile
            for source in sources + result["EMBED_FILES"]:
                assert (MAIN / source).is_file(), f"{profile}: missing {source}"
            entries = [
                source
                for source in sources
                if re.search(r"\bvoid\s+app_main\s*\(", (MAIN / source).read_text(encoding="utf-8"))
            ]
            assert len(entries) == 1, f"{profile}: app_main sources {entries}"
            assert not any(source.startswith(("drivers/", "task/", "utils/")) for source in sources), sources
            assert "main.c" not in sources, sources
            assert not {"bt", "fatfs", "sdmmc", "esp_adc", "esp_wifi", "mqtt"}.intersection(result["PRIV_REQUIRES"]), profile
            if profile == "bpm-runtime":
                assert not any("sqi_gate" in s or "af_model" in s for s in sources), sources
            if profile == "bpm-fast-qrs":
                assert len(sources)==2 and "components/qrs_detector/fast_qrs_port.c" in sources, sources
                assert result["ECG_ENABLE_TFLM"] != ["ON"]
            if profile == "bpm-candidates":
                assert not any("sqi_gate" in s or "af_model" in s for s in sources), sources
                assert result["ECG_ENABLE_TFLM"] != ["ON"]
                assert "BPM_CANDIDATES_BENCHMARK=1" in result["DEFINITIONS"]
            if profile == "af-runtime":
                assert not any("sqi_gate" in s or "components/bpm" in s for s in sources), sources
            if profile == "sqi-orphanidou":
                assert not any("af_model" in s or "components/bpm" in s for s in sources), sources
            wants_tflm = result["ECG_ENABLE_TFLM"] == ["ON"]
            assert ("espressif__esp-tflite-micro" in result["PRIV_REQUIRES"]) == wants_tflm, profile
            assert bool(result["EMBED_FILES"]) == wants_tflm, profile
            assert any(source.endswith(".cpp") for source in sources) == wants_tflm, profile

        baseline = probe()
        assert baseline["ECG_BUILD_PROFILE"] == ["bpm-runtime"]
        assert_registration(baseline, "bpm-runtime")
        profiles = baseline["_ecg_build_profiles"]
        assert profiles, "No advertised profiles"
        for profile in profiles:
            normal = probe({"ECG_BUILD_PROFILE": profile})
            assert_registration(normal, profile)
            early = probe({"ECG_BUILD_PROFILE": profile, "CMAKE_BUILD_EARLY_EXPANSION": "ON"})
            for key in ("SRCS", "PRIV_REQUIRES", "EMBED_FILES"):
                assert early[key] == normal[key], f"{profile}: early expansion differs for {key}"

        with_tflm = probe({"ECG_BUILD_PROFILE": "qrs-common", "ECG_ENABLE_TFLM": "ON"})
        assert_registration(with_tflm, "qrs-common")
        assert "QRS_WIM_XIANG_TFLM_BENCHMARK=1" in with_tflm["DEFINITIONS"]
        for mappings_key, canonical, error in (
            ("_ecg_legacy_profile_options", "ECG_BUILD_PROFILE", "Conflicting benchmark options"),
        ):
            mappings = [mapping.split("|", 1) for mapping in baseline[mappings_key]]
            assert mappings, f"No compatibility options for {canonical}"
            for flag, expected in mappings:
                result = probe({flag: "ON"})
                assert result[canonical] == [expected], (flag, expected, result[canonical])
                assert_registration(result, result["ECG_BUILD_PROFILE"][0])
            for (left, _), (right, _) in itertools.combinations(mappings, 2):
                probe({left: "ON", right: "ON"}, error=error)

        probe({"ECG_BUILD_PROFILE": "invalid"}, error="Unknown ECG_BUILD_PROFILE")
        probe({"ECG_BUILD_PROFILE": "qrs-han-cnn", "ECG_ENABLE_TFLM": "OFF"}, error="requires")
        probe({"ECG_ENABLE_TFLM": "ON"}, error="supported by")
        probe({"ECG_BUILD_PROFILE": "qrs-common", "QRS_COMMON_INPUT_DIR": str(scratch / "missing")}, error="requires generated vectors")
        probe({"ECG_BUILD_PROFILE": "bpm-candidates", "QRS_COMMON_INPUT_DIR": str(scratch / "missing")}, error="bpm_candidates_vectors.h")
        probe({"ECG_BUILD_PROFILE": "bpm-fast-qrs", "QRS_COMMON_INPUT_DIR": str(scratch / "missing")}, error="bpm_candidates_vectors.h")
        probe({"ECG_BUILD_PROFILE": "production"}, error="Unknown ECG_BUILD_PROFILE")
        probe({"ECG_ENABLE_SQI": "OFF"}, error="was removed")
        probe({"ECG_SQI_COST_PROFILE": "bsqi"}, error="was removed")
        probe({"ECG_BUILD_PROFILE": "bpm-runtime", "AF_MODEL_BENCHMARK_ENABLE": "ON"}, error="already set")

        project_script = scratch / "project_probe.cmake"
        project_script.write_text(PROJECT_PROBE, encoding="utf-8")
        binary_dir = scratch / "build"
        bpm_config = binary_dir / "sdkconfig.bpm-runtime"
        af_config = binary_dir / "sdkconfig.af-extra-trees"
        custom_config = scratch / "custom-sdkconfig"

        def assert_sdkconfig(profiles, expected, initial=None, clear_at=()):
            nonlocal checks
            options = {
                "CHECK_MAIN": MAIN.as_posix(),
                "CHECK_OUTPUT": output.as_posix(),
                "CHECK_BINARY_DIR": binary_dir.as_posix(),
                "CHECK_PROFILES": ";".join(profiles),
                "CHECK_CLEAR_STEPS": ";".join(str(index) for index in clear_at),
            }
            if initial is not None:
                options["SDKCONFIG"] = initial.as_posix()
            run = subprocess.run(
                [args.cmake, *[f"-D{key}={value}" for key, value in options.items()], "-P", str(project_script)],
                capture_output=True, text=True, check=False,
            )
            checks += 1
            assert run.returncode == 0, f"SDKCONFIG selection failed: {profiles}\n{run.stdout}{run.stderr}"
            actual = [
                tuple(Path(value) if value else None for value in line.split("|"))
                for line in output.read_text(encoding="utf-8").splitlines()
            ]
            assert actual == expected, f"SDKCONFIG transition {profiles}: {actual} != {expected}"

        assert_sdkconfig(["bpm-runtime"], [(bpm_config, bpm_config)])
        assert_sdkconfig(["af-extra-trees"], [(af_config, af_config)])
        assert_sdkconfig(
            ["bpm-runtime", "af-extra-trees", "bpm-runtime"],
            [(bpm_config, bpm_config), (af_config, af_config), (bpm_config, bpm_config)],
        )
        assert_sdkconfig(
            ["bpm-runtime", "af-extra-trees", "bpm-runtime"],
            [(custom_config, None)] * 3,
            initial=custom_config,
        )
        assert_sdkconfig(
            ["bpm-runtime", "af-extra-trees", "bpm-runtime"],
            [(custom_config, None), (af_config, af_config), (bpm_config, bpm_config)],
            initial=custom_config, clear_at=(1,),
        )
        print(f"Passed {checks} CMake configuration checks across {len(profiles)} profiles.")


if __name__ == "__main__":
    main()
