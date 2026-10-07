#!/usr/bin/env python3
"""Record source evidence and compare 30-second capture controls."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time


def process_identity(pid, proc_root=Path("/proc")):
    """Reject zombies and distinguish a reused PID using Linux starttime."""
    try:
        stat = (proc_root / str(pid) / "stat").read_text()
        fields = stat[stat.rindex(")") + 2 :].split()
        return fields[19] if fields[0] not in ("Z", "X") else None
    except (OSError, ValueError, IndexError):
        return None


def run_monitored(
    command, stdout, stderr, source_alive, timeout=170, on_poll=None
):
    if not source_alive():
        return 125, "source_unavailable"
    with subprocess.Popen(command, stdout=stdout, stderr=stderr) as process:
        deadline = time.monotonic() + timeout
        try:
            while True:
                if not source_alive():
                    reason, code = "source_exited_or_replaced", 125
                    break
                status = process.poll()
                if status is not None:
                    return status, None
                if on_poll:
                    on_poll(process)
                if time.monotonic() >= deadline:
                    reason, code = "probe_timeout", 124
                    break
                time.sleep(0.1)
        finally:
            # Always reap the probe, including interruption and source failure.
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        return code, reason


def source_evidence(pid, log, offset=0, proc_root=Path("/proc")):
    process = proc_root / str(pid)
    devices = []
    try:
        descriptors = list((process / "fdinfo").iterdir())
    except OSError:
        descriptors = []
    for fd in descriptors:
        try:
            fields = dict(
                line.split(":", 1)
                for line in fd.read_text().splitlines()
                if line.startswith(("drm-driver:", "drm-pdev:"))
            )
            if fields:
                device = {key: value.strip() for key, value in fields.items()}
                if device not in devices:
                    devices.append(device)
        except (OSError, ValueError):
            continue
    device_nodes = set()
    for fd in descriptors:
        try:
            target = str((process / "fd" / fd.name).readlink())
            if target.startswith(("/dev/dri/", "/dev/nvidia")):
                device_nodes.add(target)
        except OSError:
            continue
    presented = set()
    presentation_flags = {}
    discarded = 0
    decoder_drops = []
    renderer = None
    invalid_feedback = 0
    feedback = re.compile(
        r"wp_presentation_feedback[@#]\d+\.presented\(([^)]*)\)"
    )
    try:
        stream = log.open(errors="replace")
    except OSError:
        return {
            "pid": pid,
            "alive": process.exists(),
            "presentationVerified": False,
            "presentationFps": None,
            "ffplayDroppedFrames": None,
            "evidenceError": "source_log_unavailable",
        }
    with stream:
        stream.seek(offset)
        for line in stream:
            match = re.search(r"Initialized (.+) renderer\.", line)
            if match:
                renderer = match[1]
            for match in feedback.finditer(line):
                try:
                    values = [
                        int(value.strip(), 0) for value in match[1].split(",")
                    ]
                    if (
                        len(values) != 7
                        or any(
                            value < 0 or value > 0xFFFFFFFF for value in values
                        )
                        or values[2] >= 1_000_000_000
                    ):
                        raise ValueError("Invalid presentation feedback")
                except ValueError:
                    invalid_feedback += 1
                    continue
                key = (
                    ((values[0] << 32) | values[1]) * 1_000_000_000
                    + values[2],
                    (values[4] << 32) | values[5],
                )
                if key not in presented:
                    presentation_flags[values[6]] = (
                        presentation_flags.get(values[6], 0) + 1
                    )
                presented.add(key)
            discarded += len(
                re.findall(
                    r"wp_presentation_feedback[@#]\d+\.discarded\(", line
                )
            )
            decoder_drops.extend(
                int(value) for value in re.findall(r"\bfd=\s*(\d+)", line)
            )
    timestamps = sorted({timestamp for timestamp, _ in presented})
    span = (timestamps[-1] - timestamps[0]) / 1e9 if len(timestamps) > 1 else 0
    return {
        "pid": pid,
        "alive": process.exists(),
        "openDrmDevices": devices,
        "gpuDeviceNodes": sorted(device_nodes),
        "rendererBackend": renderer,
        "gpuIdentityVerified": False,
        "presentedFeedback": len(presented),
        "presentationFlagsHistogram": presentation_flags,
        "zeroCopyPresentedFeedback": sum(
            count for flags, count in presentation_flags.items() if flags & 8
        ),
        "distinctPresentationTimestamps": len(timestamps),
        "presentationSpanSeconds": span,
        "presentationFps": (len(timestamps) - 1) / span if span else None,
        "discardedPresentationFeedback": discarded,
        "ffplayDroppedFrames": max(decoder_drops) if decoder_drops else None,
        "invalidPresentationFeedback": invalid_feedback,
        "presentationVerified": span >= 29 and not invalid_feedback,
        "distinctFrameContentVerified": False,
    }


def read_capture(path, stderr_path=None):
    events = []
    for file in (path, stderr_path):
        if file is None:
            continue
        for line in file.read_text().splitlines():
            try:
                event = json.loads(line)
                if isinstance(event, dict):
                    events.append(event)
            except json.JSONDecodeError:
                continue
    summary = next(
        (event for event in events if event.get("event") == "capture.summary"),
        None,
    )
    timings = next(
        (
            event.get("stages", {})
            for event in events
            if event.get("event") == "capture.timings"
        ),
        {},
    )
    failure = next(
        (event for event in events if event.get("event") == "capture.failed"),
        None,
    )
    return {
        "summary": summary,
        "timings": timings,
        "failure": failure,
        "format": next(
            (
                event
                for event in events
                if event.get("event") == "capture.format"
            ),
            None,
        ),
        "buffer": next(
            (
                event
                for event in events
                if event.get("event") == "capture.buffer"
            ),
            None,
        ),
        "requestedRate": next(
            (
                event
                for event in events
                if event.get("event") == "capture.requested_rate"
            ),
            None,
        ),
        "intervals": [
            event
            for event in events
            if event.get("event") == "capture.interval"
        ],
    }


def completed_control(run, cpu):
    summary = run.get("summary") or {}
    return bool(
        run.get("exitCode") in (0, 3)
        and summary.get("elapsedSeconds", 0) >= 30
        and summary.get("width") == 1920
        and summary.get("height") == 1080
        and summary.get("cpuCapture") is cpu
        and summary.get("discarded") == 0
        and summary.get("sequenceGaps") == 0
        and summary.get("outstandingBuffers") == 0
        and summary.get("received")
        == summary.get("released")
        == summary.get("delivered")
        and summary.get("delivered", 0) > 0
    )


def supports_pool_experiment(gpu, cpu):
    summary = gpu.get("summary") or {}
    source = gpu.get("source", {})
    return bool(
        completed_control(gpu, False)
        and completed_control(cpu, True)
        and gpu.get("validControl") is not False
        and cpu.get("validControl") is not False
        and source.get("presentationVerified")
        and (source.get("presentationFps") or 0) >= 59
        and source.get("discardedPresentationFeedback", 0) == 0
        and cpu.get("source", {}).get("presentationVerified")
        and (cpu.get("source", {}).get("presentationFps") or 0) >= 59
        and cpu.get("source", {}).get("discardedPresentationFeedback", 0) == 0
        and cpu["summary"].get("fps", 0) >= 59
        and summary["fps"] < 59
        and 0 < summary.get("maxBufferPoolSize", 0) < 8
        and gpu["timings"].get("fenceWait", {}).get("p99", 0) >= 15
        and gpu["timings"].get("bufferHeld", {}).get("p99", 0) >= 15
    )


def capture_node_snapshot(node_id, output):
    script = Path(__file__).parent / "pipewire_node_snapshot.py"
    try:
        result = subprocess.run(
            [
                sys.executable,
                str(script),
                "--node",
                str(node_id),
                "--output",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=7,  # The child bounds pw-dump at five seconds.
        )
        if result.returncode:
            status = {
                "status": "failed",
                "nodeId": node_id,
                "reason": "snapshot_command_failed",
                "exitCode": result.returncode,
            }
            try:
                event = json.loads(result.stdout or "")
                if isinstance(event, dict) and isinstance(
                    event.get("reason"), str
                ):
                    status["detail"] = event["reason"][:512]
            except json.JSONDecodeError:
                pass
            return status, None
        snapshot = json.loads(output.read_text())
        if not isinstance(snapshot, dict) or snapshot.get("nodeId") != node_id:
            raise ValueError("Snapshot node does not match requested node")
        return {"status": "saved", "nodeId": node_id}, snapshot
    except subprocess.TimeoutExpired:
        return {
            "status": "failed",
            "nodeId": node_id,
            "reason": "snapshot_timeout",
        }, None
    except (OSError, ValueError) as error:
        return {
            "status": "failed",
            "nodeId": node_id,
            "reason": type(error).__name__,
        }, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--source-pid", type=int, required=True)
    parser.add_argument("--source-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--control",
        choices=("both", "cpu", "gpu"),
        default="both",
        help="Run both controls or repeat only one buffer path",
    )
    parser.add_argument(
        "--fixed-rate",
        action="store_true",
        help="Request exact 60/1 instead of the baseline rate range",
    )
    parser.add_argument(
        "--source-target",
        choices=("monitor", "window"),
        default="monitor",
        help="Target source type for ScreenCast dialog (monitor or window, default: monitor)",
    )
    parser.add_argument(
        "--snapshot-node",
        action="store_true",
        help="Capture live PipeWire node snapshot while each capture control is active",
    )
    args = parser.parse_args()
    if (
        args.source_pid <= 0
        or not args.probe.is_file()
        or not args.source_log.is_file()
    ):
        parser.error(
            "An existing probe/source log and positive source PID are required"
        )
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    identity = process_identity(args.source_pid)
    if identity is None:
        parser.error("The motion source must be a live, non-zombie process")
    report = {
        "sourceTarget": args.source_target,
        "sourceBefore": source_evidence(args.source_pid, args.source_log),
        "runs": {},
        "requestedControl": args.control,
        "fixedRateRequested": args.fixed_rate,
        "nvencReady": False,
        "webRtcStatus": "pending",
    }

    def save_report():
        temporary = args.output / "report.json.tmp"
        temporary.write_text(json.dumps(report, indent=2) + "\n")
        temporary.replace(args.output / "report.json")

    def source_alive():
        return process_identity(args.source_pid) == identity

    save_report()

    def run(name, pool, cpu):
        command = [
            str(args.probe.resolve()),
            "--seconds",
            "30",
            "--source",
            args.source_target,
            "--timing-diagnostics",
            "--buffer-pool",
            str(pool),
        ]
        if cpu:
            command.append("--cpu-capture")
        if args.fixed_rate:
            command.append("--fixed-rate")
        print(
            f"Running {name}: select the same 1080p {args.source_target}.",
            flush=True,
        )
        offset = args.source_log.stat().st_size
        before = source_evidence(args.source_pid, args.source_log)
        path = args.output / (name + ".jsonl")
        error_path = args.output / (name + ".stderr")
        snapshotted_node = None
        snapshot_status = {
            "status": (
                "source_not_observed" if args.snapshot_node else "disabled"
            )
        }
        node_snapshot = None
        node_observed_at = None
        node_observations = []

        def poll_snapshot(_proc):
            nonlocal snapshotted_node, snapshot_status, node_snapshot
            nonlocal node_observed_at
            if not args.snapshot_node or len(node_observations) == 3:
                return
            if not path.is_file():
                return
            try:
                for line in (
                    ()
                    if snapshotted_node is not None
                    else path.read_text().splitlines()
                ):
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue  # A flushed event may still be partially written.
                    if (
                        isinstance(event, dict)
                        and event.get("event") == "capture.source"
                    ):
                        node_id = event.get("nodeId")
                        if type(node_id) is int and 0 <= node_id < 0xFFFFFFFF:
                            snapshotted_node = node_id
                            node_observed_at = time.monotonic()
                            break
                if snapshotted_node is None:
                    return
                elapsed = time.monotonic() - node_observed_at
                scheduled = (0, 10, 20)[len(node_observations)]
                if elapsed < scheduled:
                    return
                suffix = "" if scheduled == 0 else f"-{scheduled}s"
                snapshot_out = args.output / f"{name}-node{suffix}.json"
                status, snapshot = capture_node_snapshot(
                    snapshotted_node, snapshot_out
                )
                observation = {
                    "scheduledSeconds": scheduled,
                    "observedSeconds": elapsed,
                    "status": status,
                }
                if snapshot is not None:
                    observation["snapshot"] = snapshot
                node_observations.append(observation)
                if scheduled == 0:
                    snapshot_status, node_snapshot = status, snapshot
            except OSError as error:
                snapshot_status = {
                    "status": "failed",
                    "reason": type(error).__name__,
                }

        with path.open("w") as stdout, error_path.open("w") as stderr:
            try:
                exit_code, exclusion = run_monitored(
                    command,
                    stdout,
                    stderr,
                    source_alive,
                    on_poll=poll_snapshot,
                )
            except KeyboardInterrupt:
                exit_code, exclusion = 130, "interrupted"
            except OSError as error:
                exit_code, exclusion = 126, f"probe_launch_failed: {error}"
        data = read_capture(path, error_path)
        data["exitCode"] = exit_code
        data["source"] = source_evidence(
            args.source_pid, args.source_log, offset
        )
        data["nodeSnapshotStatus"] = snapshot_status
        data["nodeSnapshots"] = node_observations
        if node_snapshot is not None:
            data["nodeSnapshot"] = node_snapshot
        data["validControl"] = bool(
            exclusion is None
            and source_alive()
            and completed_control(data, cpu)
        )
        data["exclusionReason"] = exclusion
        if not data["validControl"] and exclusion is None:
            data["exclusionReason"] = "incomplete_capture_control"
        after = data["source"]["ffplayDroppedFrames"]
        initial = before["ffplayDroppedFrames"]
        data["source"]["ffplayDropDelta"] = (
            after - initial
            if after is not None and initial is not None
            else None
        )
        report["runs"][name] = data
        save_report()
        return data

    gpu = (
        run("baseline-gpu", 4, False)
        if args.control in ("both", "gpu")
        else {}
    )
    cpu = (
        run("baseline-cpu", 4, True)
        if args.control == "cpu"
        or (args.control == "both" and gpu["validControl"])
        else {}
    )
    if supports_pool_experiment(gpu, cpu):
        report["change"] = {
            "requestedBufferPoolSize": 8,
            "evidence": "Completed controls; source feedback and CPU >=59 FPS; GPU <59 FPS, fence/hold p99 >=15 ms; observed pool <8",
            "hypothesis": "A larger pool may reduce recycling backpressure",
        }
        larger_gpu = run("pool8-gpu", 8, False)
        if larger_gpu["validControl"]:
            run("pool8-cpu", 8, True)
    else:
        report["change"] = None
        report["reason"] = (
            "Baseline does not support a pool-size experiment; inspect source/compositor evidence."
        )
    report["sourceAfter"] = source_evidence(args.source_pid, args.source_log)
    report["nvencReady"] = (
        False  # Pixel correctness requires separate verification.
    )
    report["webRtcStatus"] = "pending"
    save_report()
    for name, data in report["runs"].items():
        print(
            f"{name}: fps={(data['summary'] or {}).get('fps')}, "
            f"validControl={data['validControl']}, "
            f"sourceDropDelta={data['source']['ffplayDropDelta']}"
        )
    print(f"Report: {args.output / 'report.json'}")
    return (
        0 if all(run["validControl"] for run in report["runs"].values()) else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
