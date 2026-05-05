#!/usr/bin/env python3
"""
Collect OpenJ9 footprint artifacts for a live JVM and run offline analysis.

Workflow:
1. Copy /proc/<pid>/smaps first.
2. Snapshot existing /tmp dump files for this PID.
3. Send SIGQUIT (kill -3 semantics) to the JVM.
4. Wait for new javacore and core files for that PID to appear in /tmp.
5. Resolve jdmpview from the same Java installation as the target JVM.
6. Run jdmpview !printallcallsites on the new core.
7. Run footprintAnalysis.linux with smaps, javacore, and callsites.
8. Store all outputs in a PID-tagged session directory.

Assumptions from user requirements:
- OpenJ9 is already configured so SIGQUIT generates both javacore and core.
  Use: -Dcom.ibm.dbgmalloc=true -Xdump:none -Xdump:system:events=user,file=/tmp/core.%pid.%seq.dmp -Xdump:java:events=user,file=/tmp/javacore.%pid.%seq.txt
- Dumps appear under /tmp.
- Dump filenames contain the JVM PID.
- jdmpview should be located from the same installation as the target JVM's java.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple


DEFAULT_DUMP_DIR = Path("/tmp")
DEFAULT_WAIT_TIMEOUT = 300 # Max value to wait for javacore/coredump to be generated (in seconds)
DEFAULT_STABLE_SECONDS = 5
DEFAULT_POLL_INTERVAL = 1.0


@dataclass
class CommandResult:
    command: List[str]
    returncode: int
    stdout_path: Optional[str] = None
    stderr_path: Optional[str] = None


@dataclass
class Manifest:
    pid: int
    timestamp_utc: str
    hostname: str
    java_exe: str
    jdmpview_exe: str
    dump_dir: str
    session_dir: str
    smaps_file: str
    maps_file: str
    status_file: str
    cmdline_file: str
    javacore_file: str
    core_file: str
    callsites_file: str
    footprint_output_file: str
    commands: List[dict]


class CollectorError(Exception):
    pass


def utc_timestamp() -> str:
    # Use a compact UTC timestamp for deterministic session directory names.
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def read_proc_link(pid: int, name: str) -> Path:
    # Resolve a /proc symlink such as /proc/<pid>/exe to its real filesystem path.
    link = Path(f"/proc/{pid}/{name}")
    try:
        return link.resolve(strict=True)
    except Exception as exc:
        raise CollectorError(f"Cannot resolve {link}: {exc}") from exc


def read_proc_text(pid: int, name: str, binary: bool = False) -> bytes:
    # Read a /proc file and always return bytes so callers can write it back unchanged.
    path = Path(f"/proc/{pid}/{name}")
    try:
        return path.read_bytes() if binary else path.read_text().encode()
    except Exception as exc:
        raise CollectorError(f"Cannot read {path}: {exc}") from exc


def ensure_pid_exists(pid: int) -> None:
    # Validate early that the target process still exists.
    if not Path(f"/proc/{pid}").exists():
        raise CollectorError(f"PID {pid} does not exist")


def find_java_for_pid(pid: int) -> Path:
    # Identify the exact Java executable backing the target JVM.
    exe = read_proc_link(pid, "exe")
    if exe.name != "java":
        raise CollectorError(f"PID {pid} exe is not java: {exe}")
    return exe


def find_jdmpview(java_exe: Path) -> Path:
    # Resolve jdmpview from the same Java installation as the target JVM.
    java_bin = java_exe.parent
    candidates = [
        java_bin / "jdmpview",
        java_bin / "jdmpview.exe",
        java_bin.parent / "bin" / "jdmpview",
        java_bin.parent / "bin" / "jdmpview.exe",
    ]
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    raise CollectorError(
        f"Cannot locate jdmpview relative to java executable {java_exe}"
    )


def safe_write_bytes(path: Path, data: bytes) -> None:
    # Small wrapper kept so file writing behavior is centralized.
    path.write_bytes(data)


def copy_proc_artifacts(pid: int, session_dir: Path) -> Tuple[Path, Path, Path, Path]:
    # Capture the main /proc inputs before SIGQUIT so RSS data is as close as possible
    # to the pre-dump live JVM state.
    smaps_dst = session_dir / f"smaps.pid{pid}.txt"
    maps_dst = session_dir / f"maps.pid{pid}.txt"
    status_dst = session_dir / f"status.pid{pid}.txt"
    cmdline_dst = session_dir / f"cmdline.pid{pid}.txt"

    safe_write_bytes(smaps_dst, read_proc_text(pid, "smaps"))
    safe_write_bytes(maps_dst, read_proc_text(pid, "maps"))
    safe_write_bytes(status_dst, read_proc_text(pid, "status"))
    cmdline_raw = read_proc_text(pid, "cmdline", binary=True).replace(b"\x00", b" ").strip()
    safe_write_bytes(cmdline_dst, cmdline_raw + b"\n")

    return smaps_dst, maps_dst, status_dst, cmdline_dst


def list_candidate_dumps(dump_dir: Path, pid: int) -> Tuple[set[str], set[str]]:
    # Discover dump files in /tmp using the agreed convention that names contain the PID.
    pid_token = str(pid)
    javacores = {
        str(p)
        for p in dump_dir.glob(f"*{pid_token}*")
        if p.is_file() and "javacore" in p.name.lower()
    }
    cores = {
        str(p)
        for p in dump_dir.glob(f"*{pid_token}*")
        if p.is_file()
        and any(token in p.name.lower() for token in ("core", "coredump"))
        and "javacore" not in p.name.lower()
    }
    return javacores, cores


def newest_file(paths: Iterable[str]) -> Path:
    # Choose the newest file when multiple dumps match the same PID.
    candidates = [Path(p) for p in paths]
    if not candidates:
        raise CollectorError("No candidate files found")
    return max(candidates, key=lambda p: p.stat().st_mtime)


def wait_for_file_stable(path: Path, stable_seconds: int, timeout_seconds: int) -> None:
    # Wait until a dump file stops growing so downstream tools do not read a partial file.
    deadline = time.time() + timeout_seconds
    last_size = -1
    stable_since = None

    while time.time() < deadline:
        if not path.exists():
            time.sleep(DEFAULT_POLL_INTERVAL)
            continue
        try:
            size = path.stat().st_size
        except FileNotFoundError:
            time.sleep(DEFAULT_POLL_INTERVAL)
            continue

        if size == last_size:
            if stable_since is None:
                stable_since = time.time()
            elif time.time() - stable_since >= stable_seconds:
                return
        else:
            last_size = size
            stable_since = None
        time.sleep(DEFAULT_POLL_INTERVAL)

    raise CollectorError(f"Timed out waiting for file to stabilize: {path}")


def wait_for_new_dumps(
    dump_dir: Path,
    pid: int,
    before_javacores: set[str],
    before_cores: set[str],
    timeout_seconds: int,
    stable_seconds: int,
) -> Tuple[Path, Path]:
    # Compare the dump directory before and after SIGQUIT to identify the new artifacts.
    deadline = time.time() + timeout_seconds
    new_javacore: Optional[Path] = None
    new_core: Optional[Path] = None

    while time.time() < deadline:
        javacores, cores = list_candidate_dumps(dump_dir, pid)
        javacore_delta = javacores - before_javacores
        core_delta = cores - before_cores

        if javacore_delta and new_javacore is None:
            new_javacore = newest_file(javacore_delta)
        if core_delta and new_core is None:
            new_core = newest_file(core_delta)

        if new_javacore and new_core:
            wait_for_file_stable(new_javacore, stable_seconds, timeout_seconds)
            wait_for_file_stable(new_core, stable_seconds, timeout_seconds)
            return new_javacore, new_core

        time.sleep(DEFAULT_POLL_INTERVAL)

    raise CollectorError(
        f"Timed out waiting for new javacore/core files in {dump_dir} for PID {pid}"
    )


def run_command(
    command: Sequence[str],
    stdout_path: Path,
    stderr_path: Path,
    timeout_seconds: int,
) -> CommandResult:
    # Execute an external tool and persist stdout/stderr for later troubleshooting.
    with stdout_path.open("wb") as out_f, stderr_path.open("wb") as err_f:
        proc = subprocess.run(
            list(command),
            stdout=out_f,
            stderr=err_f,
            timeout=timeout_seconds,
            check=False,
        )
    return CommandResult(
        command=list(command),
        returncode=proc.returncode,
        stdout_path=str(stdout_path),
        stderr_path=str(stderr_path),
    )


def create_session_dir(base_dir: Path, pid: int) -> Path:
    # Keep each collection in its own directory to make artifacts reproducible and easy to inspect.
    session_dir = base_dir / f"openj9-footprint-{utc_timestamp()}-pid{pid}"
    session_dir.mkdir(parents=True, exist_ok=False)
    return session_dir


def send_sigquit(pid: int) -> None:
    # Trigger the OpenJ9 diagnostic event configured on SIGQUIT.
    try:
        os.kill(pid, signal.SIGQUIT)
    except Exception as exc:
        raise CollectorError(f"Failed to send SIGQUIT to PID {pid}: {exc}") from exc


def copy_dump_to_session(src: Path, dst_dir: Path, prefix: str, pid: int) -> Path:
    # Copy dumps into the session directory so later analysis is independent of /tmp cleanup.
    dst = dst_dir / f"{prefix}.pid{pid}{src.suffix or '.txt'}"
    shutil.copy2(src, dst)
    return dst


def resolve_footprint_binary(path_arg: Optional[str]) -> Path:
    # Resolve the existing native analysis tool, defaulting to the workspace copy.
    if path_arg:
        binary = Path(path_arg)
    else:
        binary = Path.cwd() / "footprintAnalysis.linux"
    if not binary.exists():
        raise CollectorError(f"footprintAnalysis binary not found: {binary}")
    if not os.access(binary, os.X_OK):
        raise CollectorError(f"footprintAnalysis binary is not executable: {binary}")
    return binary.resolve()


def parse_args() -> argparse.Namespace:
    # Define a small CLI surface focused on dump location, output location, and timeouts.
    parser = argparse.ArgumentParser(
        description="Collect OpenJ9 javacore/core/smaps artifacts and run footprint analysis."
    )
    parser.add_argument("pid", type=int, help="Target JVM PID")
    parser.add_argument(
        "--dump-dir",
        default=str(DEFAULT_DUMP_DIR),
        help="Directory where OpenJ9 writes javacore/core files (default: /tmp)",
    )
    parser.add_argument(
        "--output-dir",
        default=".",
        help="Base directory where the session directory will be created",
    )
    parser.add_argument(
        "--footprint-binary",
        default=None,
        help="Path to footprintAnalysis.linux (default: ./footprintAnalysis.linux)",
    )
    parser.add_argument(
        "--wait-timeout",
        type=int,
        default=DEFAULT_WAIT_TIMEOUT,
        help="Seconds to wait for javacore/core and external commands (default: 300)",
    )
    parser.add_argument(
        "--stable-seconds",
        type=int,
        default=DEFAULT_STABLE_SECONDS,
        help="How long a dump file size must remain unchanged before it is considered complete",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Reduce console output to warnings and errors; full trace still goes to collector.log",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase tracing verbosity; specify twice for polling-level detail",
    )
    return parser.parse_args()


def main() -> int:
    # Orchestrate the full capture flow: proc snapshot, SIGQUIT, dump discovery,
    # callsite extraction, and native footprint analysis.
    args = parse_args()
    pid = args.pid
    dump_dir = Path(args.dump_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    console_level = 0 if args.quiet else args.verbose + 1

    ensure_pid_exists(pid)
    java_exe = find_java_for_pid(pid)
    jdmpview_exe = find_jdmpview(java_exe)
    footprint_binary = resolve_footprint_binary(args.footprint_binary)

    session_dir = create_session_dir(output_dir, pid)
    collector_log = session_dir / "collector.log"

    def log(msg: str, level: int = 1) -> None:
        # Always write the full trace to the log file, but print to console based on verbosity.
        line = f"[{datetime.now(timezone.utc).isoformat()}] {msg}"
        if console_level >= level:
            print(line)
        with collector_log.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    commands: List[dict] = []

    try:
        log(f"Target PID: {pid}")
        log(f"Resolved java executable: {java_exe}")
        log(f"Resolved jdmpview executable: {jdmpview_exe}")
        log(f"Using dump directory: {dump_dir}")
        log(f"Session directory: {session_dir}")

        smaps_file, maps_file, status_file, cmdline_file = copy_proc_artifacts(pid, session_dir)
        log(f"Copied proc artifacts, including {smaps_file.name}")

        before_javacores, before_cores = list_candidate_dumps(dump_dir, pid)
        log(
            f"Existing dump inventory for PID {pid}: "
            f"{len(before_javacores)} javacore files, {len(before_cores)} core files"
        )

        send_sigquit(pid)
        log("Sent SIGQUIT to target JVM")
        log("Waiting for new javacore and core files to appear", level=1)

        javacore_src, core_src = wait_for_new_dumps(
            dump_dir=dump_dir,
            pid=pid,
            before_javacores=before_javacores,
            before_cores=before_cores,
            timeout_seconds=args.wait_timeout,
            stable_seconds=args.stable_seconds,
        )
        log(f"Detected new javacore: {javacore_src}")
        log(f"Detected new core: {core_src}")

        javacore_file = copy_dump_to_session(javacore_src, session_dir, "javacore", pid)
        core_file = copy_dump_to_session(core_src, session_dir, "core", pid)
        log("Copied dumps into session directory")

        callsites_file = session_dir / f"callsites.pid{pid}.txt"
        jdmp_stderr = session_dir / "jdmpview.stderr.txt"
        jdmp_cmd = [str(jdmpview_exe), "-core", str(core_file), "!printallcallsites"]
        log(f"Running jdmpview command: {' '.join(jdmp_cmd)}", level=2)
        jdmp_result = run_command(jdmp_cmd, callsites_file, jdmp_stderr, args.wait_timeout)
        commands.append(asdict(jdmp_result))
        log(f"jdmpview return code: {jdmp_result.returncode}")
        if jdmp_result.returncode != 0:
            raise CollectorError(
                f"jdmpview failed with exit code {jdmp_result.returncode}; see {jdmp_stderr}"
            )

        footprint_output = session_dir / f"footprintAnalysis.pid{pid}.txt"
        footprint_stderr = session_dir / "footprintAnalysis.stderr.txt"
        footprint_cmd = [
            str(footprint_binary),
            "-s",
            str(smaps_file),
            "-j",
            str(javacore_file),
            "-c",
            str(callsites_file),
        ]
        log(f"Running footprintAnalysis command: {' '.join(footprint_cmd)}", level=2)
        footprint_result = run_command(
            footprint_cmd, footprint_output, footprint_stderr, args.wait_timeout
        )
        commands.append(asdict(footprint_result))
        log(f"footprintAnalysis return code: {footprint_result.returncode}")
        if footprint_result.returncode != 0:
            raise CollectorError(
                f"footprintAnalysis failed with exit code {footprint_result.returncode}; "
                f"see {footprint_stderr}"
            )

        manifest = Manifest(
            pid=pid,
            timestamp_utc=datetime.now(timezone.utc).isoformat(),
            hostname=socket.gethostname(),
            java_exe=str(java_exe),
            jdmpview_exe=str(jdmpview_exe),
            dump_dir=str(dump_dir),
            session_dir=str(session_dir),
            smaps_file=str(smaps_file),
            maps_file=str(maps_file),
            status_file=str(status_file),
            cmdline_file=str(cmdline_file),
            javacore_file=str(javacore_file),
            core_file=str(core_file),
            callsites_file=str(callsites_file),
            footprint_output_file=str(footprint_output),
            commands=commands,
        )
        manifest_path = session_dir / "manifest.json"
        manifest_path.write_text(json.dumps(asdict(manifest), indent=2), encoding="utf-8")
        log(f"Wrote manifest: {manifest_path}")
        log(f"Analysis output: {footprint_output}")
        return 0

    except subprocess.TimeoutExpired as exc:
        log(f"Timeout while executing command: {exc}", level=0)
        return 2
    except CollectorError as exc:
        log(f"ERROR: {exc}", level=0)
        return 1
    except Exception as exc:
        log(f"UNEXPECTED ERROR: {exc}", level=0)
        return 3


if __name__ == "__main__":
    sys.exit(main())

# Made with Bob
