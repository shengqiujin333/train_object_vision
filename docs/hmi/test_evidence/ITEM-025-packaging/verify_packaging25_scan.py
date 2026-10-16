# -*- coding: utf-8 -*-
"""Verifier-owned packaging/deployment-contract scan (ITEM-025 software_verification).

Subject: implementer round stamp 20261016-ITEM025-r1 (commit 6d1787b).
This scan goes beyond the delivered check_packaging.py token checks: it asserts
the deployment CONTRACT between the systemd unit, install.sh, UPGRADE.md and
what the build system/installer actually produce.

Layouts: repo root (packaging/ + app/) or board upload (packaging/ + src/).
Prints KEY=True/False lines only; the fixtures driver asserts the exact map.

Positive keys (must be True on a healthy tree):
  P1_unit_restart_always            systemd Restart=always present
  P2_unit_display                   Environment=DISPLAY= present
  P3_no_cpu_performance             unit/install.sh never reference cpu-performance
  P4_install_runtime_dirs           install.sh creates bin/config/models/resources/data/logs
  P5_install_enables_unit           install.sh installs + enables railhmi.service
  P6_version_upgrade_resources      VERSION/UPGRADE(升级/回滚)/RESOURCES(许可/来源) complete
  P7_fault_catalog_six_kinds        fault_catalog.cpp covers all 6 FI kinds
  P8_p6_selfcheck_card              P6Maintenance.qml self-check card + per-fault rows
  P9_main_injects_faults            main.cpp injects railhmiFaults
  P13_unit_named_railhmi            unit is railhmi.service (SC-BOOT restarts railhmi)
  P14_execstart_binary_installed    the ExecStart binary is actually installed by install.sh
                                    or built by a CMake target
  P15_sim_inject_cli_built          SC inject commands reference a real built CLI
  P16_fault_probe_built             railhmi-fault-probe is a real built tool

Defect keys (D-01/D-02 findings):
  D01_execstartpre_provenance       every ExecStartPre/ExecStart binary under the
                                    prefix is installed by install.sh OR built by a
                                    CMake target. True = no such provenance defect.
                                    (On stamp 20261016-ITEM025-r1 this was False:
                                    railhmi-version-check was neither; fixed in
                                    fix-r2 by removing the ExecStartPre line.)
  D01b_upgrade_doc_bin_tools        every bin/ tool documented in UPGRADE.md is
                                    installed or built (True also when no tools
                                    are documented; fix-r2 removed the doc entry).
  D02_config_arg_consumed           the unit passes --config; the binary must
                                    actually consume that argument at runtime:
                                    main.cpp parses --config (parse_config_dir,
                                    fix-r2) or the compile-time default points at
                                    the deployment prefix (/opt/railhmi/config).
"""
from __future__ import annotations

import argparse
import os
import re
import sys


def rd(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="verifier packaging/deployment scan (ITEM-025)")
    ap.add_argument("--app", default=None, help="app source root (default: 'app' if exists else '.')")
    ap.add_argument("--pkg", default="packaging")
    args = ap.parse_args()
    app = args.app or ("app" if os.path.isdir("app") else ".")
    pk = args.pkg
    if not os.path.isdir(app):
        sys.stderr.write("app source root not found: %s\n" % app)
        return 2
    # repo layout: <app>/src/{core,platform,ui}; board upload: <app>/src directly
    src_root = os.path.join(app, "src") if os.path.isdir(os.path.join(app, "src")) else app
    if not os.path.isdir(os.path.join(src_root, "ui", "src")):
        sys.stderr.write("ui sources not found under: %s\n" % src_root)
        return 2

    unit = rd(os.path.join(pk, "railhmi.service"))
    install = rd(os.path.join(pk, "install.sh"))
    upgrade = rd(os.path.join(pk, "UPGRADE.md"))
    version = rd(os.path.join(pk, "VERSION"))
    resources = rd(os.path.join(pk, "RESOURCES.md"))
    main_cpp = rd(os.path.join(src_root, "ui/src/main.cpp"))
    ui_cmake = rd(os.path.join(src_root, "ui/CMakeLists.txt"))
    fault_cpp = rd(os.path.join(src_root, "core/src/fault_catalog.cpp"))
    sim_inject_cpp = os.path.join(src_root, "core/tools/sim_inject.cpp")
    fault_probe_cpp = os.path.join(src_root, "core/tools/fault_probe.cpp")
    p6 = rd(os.path.join(src_root, "ui/qml/pages/P6Maintenance.qml"))

    def code(text: str) -> str:
        return "\n".join(l.split("#", 1)[0] for l in text.splitlines())

    unit_code = code(unit)
    install_code = install  # raw for substring checks like dir names

    # --- positive keys -----------------------------------------------------
    print("P1_unit_restart_always=%s" % ("Restart=always" in unit_code))
    print("P2_unit_display=%s" % ("Environment=DISPLAY=" in unit_code))
    # cpu-performance must never appear in effective unit/install statements;
    # comments documenting the boundary are allowed, as are bash `echo` lines
    # that merely print the boundary message.
    forbidden = []
    for i, line in enumerate(unit.splitlines()):
        stripped = code(line)
        if stripped.strip() and "cpu-performance" in stripped:
            forbidden.append(("unit", i + 1, stripped.strip()))
    for i, line in enumerate(install.splitlines()):
        stripped = code(line)
        if stripped.strip() and "cpu-performance" in stripped:
            forbidden.append(("install", i + 1, stripped.strip()))
    actionable = []
    for kind, ln, text in forbidden:
        if kind == "unit":
            actionable.append("unit line %d: %s" % (ln, text))
        elif not text.startswith("echo"):
            actionable.append("install line %d: %s" % (ln, text))
    print("P3_no_cpu_performance=%s" % (not actionable))
    for a in actionable:
        print("P3_forbidden:%s" % a)
    dirs_ok = all(re.search(r"\b%s\b" % d, install) for d in
                  ("bin", "config", "models", "resources", "data", "logs"))
    print("P4_install_runtime_dirs=%s" % dirs_ok)
    print("P5_install_enables_unit=%s"
          % ("railhmi.service" in install and "systemctl" in install_code and "enable" in install_code))
    print("P6_version_upgrade_resources=%s"
          % ("version:" in version and "升级" in upgrade and "回滚" in upgrade
             and "许可" in resources and "来源" in resources))
    print("P7_fault_catalog_six_kinds=%s"
          % all(k in fault_cpp for k in ("StreamCut", "Freeze", "AiExit", "DiskLow",
                                         "PtzOutOfTolerance", "RecordingInterruption")))
    print("P8_p6_selfcheck_card=%s" % ("maintSelfCheckCard" in p6 and "faultRow-" in p6))
    print("P9_main_injects_faults=%s" % ("railhmiFaults" in main_cpp))
    print("P13_unit_named_railhmi=%s" % os.path.isfile(os.path.join(pk, "railhmi.service")))

    # --- what the deployment actually produces ------------------------------
    installed_bin = set(re.findall(r"\$PREFIX/bin/([\w.-]+)", install))
    built_targets: set = set()
    for root, dirs, files in os.walk(src_root):
        dirs[:] = [d for d in dirs if d != "build" and not d.startswith("build-")]
        for fn in files:
            if fn == "CMakeLists.txt":
                cm = rd(os.path.join(root, fn))
                built_targets.update(re.findall(r"add_executable\(\s*([\w.-]+)", cm))
    provenance = installed_bin | built_targets

    exec_start_bins = []
    exec_pre_bins = []
    for m in re.finditer(r"^ExecStartPre=(\S+)", unit_code, re.M):
        exec_pre_bins.append(os.path.basename(m.group(1)))
    for m in re.finditer(r"^ExecStart=(\S+)", unit_code, re.M):
        exec_start_bins.append(os.path.basename(m.group(1)))
    exec_bins = exec_start_bins + exec_pre_bins
    print("P14_execstart_binary_installed=%s"
          % (bool(exec_start_bins) and all(b in provenance for b in exec_start_bins)))

    print("P15_sim_inject_cli_built=%s"
          % (os.path.isfile(sim_inject_cpp) and "railhmi-sim-inject" in built_targets
             and "railhmi-sim-inject" in fault_cpp))
    print("P16_fault_probe_built=%s"
          % (os.path.isfile(fault_probe_cpp) and "railhmi-fault-probe" in built_targets))

    # --- defect keys --------------------------------------------------------
    # D-01: every ExecStartPre/ExecStart binary must be covered by install.sh
    # or a CMake target (D-01 defect = False key). fix-r2 removed the
    # railhmi-version-check ExecStartPre line; True = no provenance defect.
    print("D01_execstartpre_provenance=%s" % all(b in provenance for b in exec_bins))

    # D-01b: UPGRADE.md documents bin/ tools that must exist. fix-r2 removed
    # the railhmi-version-check entry (changed to in-app config/version check).
    doc_tools = set(re.findall(r"`([\w-]+)`（`bin/` 内）", upgrade))
    print("D01b_upgrade_doc_bin_tools=%s"
          % ((not doc_tools) or all(t in provenance for t in doc_tools)))
    print("D01b_doc_tools=%s" % (",".join(sorted(doc_tools)) if doc_tools else "(none)"))

    # D-02: the unit passes --config /opt/railhmi/config; the binary must
    # actually consume it: parse_config_dir in main.cpp + load_config_dir(
    # configDir) — or the compile-time default must point at the deployment
    # prefix. Key False = the deployment contract is broken.
    cfg_defs = re.findall(r"RAILHMI_CONFIG_DIR[^\n]*", ui_cmake + main_cpp)
    parses_arg = 'parse_config_dir' in main_cpp and 'load_config_dir(configDir)' in main_cpp
    targets_prefix = any("/opt/railhmi" in d for d in cfg_defs)
    print("D02_config_arg_consumed=%s" % (parses_arg or targets_prefix))
    print("D02_parses_arg=%s targets_prefix=%s" % (parses_arg, targets_prefix))
    for d in cfg_defs:
        print("D02_cfg_def:%s" % d.strip())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
