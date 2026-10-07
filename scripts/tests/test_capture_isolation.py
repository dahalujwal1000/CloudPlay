import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "capture_isolation", Path(__file__).parents[1] / "capture_isolation.py"
)
isolation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolation)

node_spec = importlib.util.spec_from_file_location(
    "pipewire_node_snapshot",
    Path(__file__).parents[1] / "pipewire_node_snapshot.py",
)
nodes = importlib.util.module_from_spec(node_spec)
node_spec.loader.exec_module(nodes)


class IsolationTests(unittest.TestCase):
    def test_node_snapshot_cli_private_output_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "node.json"
            objects = [
                {"id": 70, "type": "PipeWire:Interface:Node", "info": {}}
            ]
            with (
                patch.object(
                    sys,
                    "argv",
                    ["snapshot", "--node", "70", "--output", str(output)],
                ),
                patch.object(
                    nodes.subprocess,
                    "run",
                    return_value=subprocess.CompletedProcess(
                        ["pw-dump"], 0, stdout=json.dumps(objects)
                    ),
                ),
                patch("builtins.print"),
            ):
                self.assertEqual(nodes.main(), 0)
                self.assertEqual(output.stat().st_mode & 0o777, 0o600)
                saved = output.read_text()
                self.assertEqual(nodes.main(), 1)
                self.assertEqual(output.read_text(), saved)
            for info in ([1], {"props": [1]}, {"params": [1]}):
                with self.assertRaises(ValueError):
                    nodes.select_node(
                        [
                            {
                                "id": 70,
                                "type": "PipeWire:Interface:Node",
                                "info": info,
                            }
                        ],
                        70,
                    )

    def test_node_snapshot_limits_parameters_and_excludes_private_properties(
        self,
    ):
        objects = [
            {
                "id": 70,
                "type": "PipeWire:Interface:Node",
                "info": {
                    "state": "running",
                    "props": {
                        "node.rate": "1/60",
                        "token": "secret",
                        "application.name": "private application",
                    },
                    "params": {
                        "Format": [{"format": "BGRx"}],
                        "Buffers": [1] * 40,
                        "Unrelated": ["private"],
                    },
                },
            }
        ]
        result = nodes.select_node(objects, 70)
        self.assertEqual(result["properties"], {"node.rate": "1/60"})
        self.assertEqual(len(result["parameters"]["Buffers"]), 32)
        self.assertEqual(result["truncatedParameters"], ["Buffers"])
        self.assertNotIn("Unrelated", result["parameters"])
        self.assertFalse(result["clockQuantumVerified"])
        for data, node_id in (
            ({}, 70),
            ([], 70),
            (objects, 71),
            ([{"id": 70, "type": "PipeWire:Interface:Client"}], 70),
        ):
            with self.assertRaises(ValueError):
                nodes.select_node(data, node_id)

    def test_nvidia_launcher_scopes_environment_and_preserves_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vendor = root / "nvidia.json"
            vendor.write_text("{}")
            player = root / "ffplay"
            player.write_text(
                f"#!{sys.executable}\nimport os, json, sys\n"
                "print(json.dumps({'env':dict(os.environ), 'args':sys.argv[1:]}))\n"
            )
            player.chmod(0o700)
            environment = dict(
                os.environ,
                PATH=f"{root}:{os.defpath}",
                CLOUDPLAY_NVIDIA_EGL_VENDOR=str(vendor),
                DRI_PRIME="1",
            )
            launcher = Path(__file__).parents[1] / "nvidia-motion-reference.sh"
            result = subprocess.run(
                ["sh", str(launcher), "-t", "2"],
                env=environment,
                text=True,
                capture_output=True,
                check=True,
            )
            data = json.loads(result.stdout)
            self.assertEqual(data["env"]["__NV_PRIME_RENDER_OFFLOAD"], "1")
            self.assertEqual(
                data["env"]["__EGL_VENDOR_LIBRARY_FILENAMES"], str(vendor)
            )
            self.assertEqual(data["env"]["SDL_VIDEODRIVER"], "wayland")
            self.assertEqual(data["env"]["SDL_RENDER_DRIVER"], "opengl")
            self.assertNotIn("DRI_PRIME", data["env"])
            self.assertEqual(data["args"][-2:], ["-t", "2"])
            self.assertEqual(environment["DRI_PRIME"], "1")
            vendor.unlink()
            failure = subprocess.run(
                ["sh", str(launcher)],
                env=environment,
                text=True,
                capture_output=True,
            )
            self.assertEqual(failure.returncode, 1)
            self.assertIn("vendor file unavailable", failure.stderr)

    def test_report_retains_negotiated_layout_and_zero_frame_intervals(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "capture.jsonl"
            events = [
                {"event": "capture.requested_rate", "maximumFps": 60},
                {"event": "capture.format", "width": 1920, "height": 1080},
                {
                    "event": "capture.buffer",
                    "spaFormatName": "BGRx",
                    "planes": [
                        {
                            "memoryTypeName": "MemFd",
                            "stride": 7680,
                            "effectiveOffset": 0,
                        }
                    ],
                },
                {"event": "capture.interval", "fps": 0},
                {"event": "capture.interval", "fps": 34},
            ]
            log.write_text("\n".join(json.dumps(event) for event in events))
            report = isolation.read_capture(log)
            self.assertEqual(report["requestedRate"], events[0])
            self.assertEqual(report["format"], events[1])
            self.assertEqual(report["buffer"], events[2])
            self.assertEqual(report["intervals"], events[3:])

    def test_cpu_only_requests_one_cpu_probe_and_never_changes_pool(self):
        self.check_single_control("cpu", False)

    def test_gpu_only_fixed_rate_requests_one_probe(self):
        self.check_single_control("gpu", True)

    def test_gpu_only_preserves_range_baseline(self):
        self.check_single_control("gpu", False)

    def check_single_control(self, control, fixed):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe, log = root / "probe", root / "source.log"
            probe.touch()
            log.touch()
            output = root / "results"
            arguments = [
                "capture_isolation.py",
                "--probe",
                str(probe),
                "--source-pid",
                "123",
                "--source-log",
                str(log),
                "--output",
                str(output),
                "--control",
                control,
            ]
            if fixed:
                arguments.append("--fixed-rate")
            summary = {
                "elapsedSeconds": 30,
                "width": 1920,
                "height": 1080,
                "cpuCapture": control == "cpu",
                "discarded": 0,
                "sequenceGaps": 0,
                "outstandingBuffers": 0,
                "received": 1800,
                "released": 1800,
                "delivered": 1800,
                "fps": 60,
            }
            with (
                patch.object(sys, "argv", arguments),
                patch.object(
                    isolation, "process_identity", return_value="100"
                ),
                patch.object(
                    isolation,
                    "source_evidence",
                    return_value={"alive": True, "ffplayDroppedFrames": 0},
                ),
                patch.object(
                    isolation, "run_monitored", return_value=(3, None)
                ) as monitor,
                patch.object(
                    isolation,
                    "read_capture",
                    return_value={
                        "summary": summary,
                        "timings": {},
                        "failure": None,
                    },
                ),
                patch("builtins.print"),
            ):
                self.assertEqual(isolation.main(), 0)
            self.assertEqual(monitor.call_count, 1)
            self.assertEqual(
                "--cpu-capture" in monitor.call_args.args[0], control == "cpu"
            )
            self.assertEqual(
                "--fixed-rate" in monitor.call_args.args[0], fixed
            )
            self.assertIn("monitor", monitor.call_args.args[0])
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(report["requestedControl"], control)
            self.assertEqual(report["fixedRateRequested"], fixed)
            self.assertEqual(report["sourceTarget"], "monitor")
            self.assertEqual(list(report["runs"]), [f"baseline-{control}"])
            self.assertIsNone(report["change"])
            self.assertFalse(report["nvencReady"])

    def test_source_target_window_and_snapshot_node(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe, log = root / "probe", root / "source.log"
            probe.touch()
            log.touch()
            output = root / "results"
            arguments = [
                "capture_isolation.py",
                "--probe",
                str(probe),
                "--source-pid",
                "123",
                "--source-log",
                str(log),
                "--output",
                str(output),
                "--source-target",
                "window",
                "--control",
                "cpu",
                "--snapshot-node",
            ]
            summary = {
                "elapsedSeconds": 30,
                "width": 1920,
                "height": 1080,
                "cpuCapture": True,
                "discarded": 0,
                "sequenceGaps": 0,
                "outstandingBuffers": 0,
                "received": 1800,
                "released": 1800,
                "delivered": 1800,
                "fps": 60,
            }

            def fake_monitor(
                command, stdout, stderr, source_alive, on_poll=None
            ):
                if on_poll:
                    (output / "baseline-cpu.jsonl").write_text(
                        '{"event":"capture.source","nodeId":77}\n'
                    )
                    on_poll(None)
                    on_poll(None)
                    with patch.object(
                        isolation.time, "monotonic", return_value=110
                    ):
                        on_poll(None)
                        on_poll(None)
                    with patch.object(
                        isolation.time, "monotonic", return_value=120
                    ):
                        on_poll(None)
                        on_poll(None)
                return 0, None

            def fake_snapshot(command, **kwargs):
                self.assertEqual(command[0], sys.executable)
                self.assertEqual(command[-3], "77")
                Path(command[-1]).write_text(
                    json.dumps({"nodeId": 77, "state": "running"})
                )
                return subprocess.CompletedProcess(command, 0)

            with (
                patch.object(sys, "argv", arguments),
                patch.object(isolation.time, "monotonic", return_value=100),
                patch.object(
                    isolation, "process_identity", return_value="100"
                ),
                patch.object(
                    isolation,
                    "source_evidence",
                    return_value={"alive": True, "ffplayDroppedFrames": 0},
                ),
                patch.object(
                    isolation, "run_monitored", side_effect=fake_monitor
                ) as monitor,
                patch.object(
                    isolation.subprocess, "run", side_effect=fake_snapshot
                ) as snapshot,
                patch.object(
                    isolation,
                    "read_capture",
                    return_value={
                        "summary": summary,
                        "timings": {},
                        "failure": None,
                    },
                ),
                patch("builtins.print"),
            ):
                self.assertEqual(isolation.main(), 0)
            self.assertEqual(monitor.call_count, 1)
            self.assertEqual(snapshot.call_count, 3)
            self.assertIn("window", monitor.call_args.args[0])
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(report["sourceTarget"], "window")
            self.assertEqual(
                report["runs"]["baseline-cpu"]["nodeSnapshot"]["nodeId"], 77
            )
            self.assertEqual(
                report["runs"]["baseline-cpu"]["nodeSnapshotStatus"]["status"],
                "saved",
            )
            observations = report["runs"]["baseline-cpu"]["nodeSnapshots"]
            self.assertEqual(
                [item["scheduledSeconds"] for item in observations],
                [0, 10, 20],
            )
            self.assertEqual(
                [item["observedSeconds"] for item in observations], [0, 10, 20]
            )
            self.assertEqual(
                len({call.args[0][-1] for call in snapshot.call_args_list}), 3
            )

    def test_snapshot_failures_are_reported_without_accepting_stale_output(
        self,
    ):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "node.json"
            path.write_text('{"nodeId":77}')
            for failure in (
                subprocess.TimeoutExpired("snapshot", 7),
                OSError("missing"),
            ):
                with patch.object(
                    isolation.subprocess, "run", side_effect=failure
                ):
                    status, snapshot = isolation.capture_node_snapshot(
                        77, path
                    )
                    self.assertEqual(status["status"], "failed")
                    self.assertIsNone(snapshot)
            with patch.object(
                isolation.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 1),
            ):
                status, snapshot = isolation.capture_node_snapshot(77, path)
                self.assertEqual(status["exitCode"], 1)
                self.assertIsNone(snapshot)
            for contents in ("not json", "[]", '{"nodeId":78}'):
                path.write_text(contents)
                with patch.object(
                    isolation.subprocess,
                    "run",
                    return_value=subprocess.CompletedProcess([], 0),
                ):
                    status, snapshot = isolation.capture_node_snapshot(
                        77, path
                    )
                    self.assertEqual(status["status"], "failed")
                    self.assertIsNone(snapshot)

    def test_failed_baseline_is_saved_and_skips_next_portal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe, log = root / "probe", root / "source.log"
            probe.touch()
            log.touch()
            output = root / "results"
            evidence = {"alive": True, "ffplayDroppedFrames": 0}
            arguments = [
                "capture_isolation.py",
                "--probe",
                str(probe),
                "--source-pid",
                "123",
                "--source-log",
                str(log),
                "--output",
                str(output),
            ]
            with (
                patch.object(sys, "argv", arguments),
                patch.object(
                    isolation, "process_identity", return_value="100"
                ),
                patch.object(
                    isolation, "source_evidence", return_value=evidence
                ),
                patch.object(
                    isolation, "run_monitored", side_effect=KeyboardInterrupt
                ) as monitor,
                patch("builtins.print"),
            ):
                self.assertEqual(isolation.main(), 1)
            self.assertEqual(monitor.call_count, 1)
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(list(report["runs"]), ["baseline-gpu"])
            run = report["runs"]["baseline-gpu"]
            self.assertEqual(run["exitCode"], 130)
            self.assertEqual(run["exclusionReason"], "interrupted")
            self.assertFalse(run["validControl"])
            self.assertFalse(report["nvencReady"])
            self.assertFalse((output / "report.json.tmp").exists())

    def test_process_identity_rejects_zombies_and_pid_reuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            process = root / "123"
            process.mkdir()
            stat = process / "stat"

            def write_stat(state, start):
                stat.write_text(
                    "123 (name with ) spaces) "
                    + " ".join([state] + ["0"] * 18 + [str(start)])
                )

            write_stat("S", 100)
            self.assertEqual(isolation.process_identity(123, root), "100")
            write_stat("S", 101)
            self.assertEqual(isolation.process_identity(123, root), "101")
            write_stat("Z", 101)
            self.assertIsNone(isolation.process_identity(123, root))
            stat.write_text("malformed")
            self.assertIsNone(isolation.process_identity(123, root))
            self.assertIsNone(isolation.process_identity(456, root))

    def test_monitor_reaps_probe_on_source_exit(self):
        with tempfile.TemporaryFile() as output:
            processes = []
            original = subprocess.Popen

            def launch(*args, **kwargs):
                process = original(*args, **kwargs)
                processes.append(process)
                return process

            with patch.object(
                isolation.subprocess, "Popen", side_effect=launch
            ):
                alive = iter([True, False])
                code, reason = isolation.run_monitored(
                    [sys.executable, "-c", "import time; time.sleep(60)"],
                    output,
                    output,
                    lambda: next(alive),
                )
            self.assertEqual(
                (code, reason), (125, "source_exited_or_replaced")
            )
            self.assertIsNotNone(processes[0].poll())

    def test_monitor_timeout_completion_and_missing_source(self):
        with tempfile.TemporaryFile() as output:
            command = [sys.executable, "-c", "raise SystemExit(3)"]
            self.assertEqual(
                isolation.run_monitored(command, output, output, lambda: True),
                (3, None),
            )
            self.assertEqual(
                isolation.run_monitored(
                    command, output, output, lambda: False
                ),
                (125, "source_unavailable"),
            )
            self.assertEqual(
                isolation.run_monitored(
                    [sys.executable, "-c", "import time; time.sleep(60)"],
                    output,
                    output,
                    lambda: True,
                    timeout=0,
                ),
                (124, "probe_timeout"),
            )

    def test_source_feedback_and_device_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fdinfo = root / "123" / "fdinfo"
            fdinfo.mkdir(parents=True)
            for fd in ("7", "8"):
                (fdinfo / fd).write_text(
                    "drm-driver: i915\ndrm-pdev: 0000:00:02.0\n"
                )
            log = root / "source.log"
            log.write_text(
                "Initialized opengl renderer.\n"
                "wp_presentation_feedback#1.presented(0, 10, 0, 16666667, 0, 1, 7)\n"
                "wp_presentation_feedback#2.presented(0, 40, 0, 16666667, 0, 2, 15)\n"
                "wp_presentation_feedback#2.presented(0, 40, 0, 16666667, 0, 2, 15)\n"
                "wp_presentation_feedback#3.discarded()\nfd= 3\nfd= 4\n"
            )
            evidence = isolation.source_evidence(123, log, proc_root=root)
            self.assertTrue(evidence["alive"])
            self.assertTrue(evidence["presentationVerified"])
            self.assertEqual(evidence["presentedFeedback"], 2)
            self.assertEqual(
                evidence["presentationFlagsHistogram"], {7: 1, 15: 1}
            )
            self.assertEqual(evidence["zeroCopyPresentedFeedback"], 1)
            self.assertEqual(evidence["presentationFps"], 1 / 30)
            self.assertEqual(evidence["ffplayDroppedFrames"], 4)
            self.assertEqual(evidence["discardedPresentationFeedback"], 1)
            self.assertEqual(len(evidence["openDrmDevices"]), 1)
            self.assertFalse(evidence["gpuIdentityVerified"])
            self.assertFalse(evidence["distinctFrameContentVerified"])
            self.assertEqual(evidence["rendererBackend"], "opengl")

    def test_missing_process_and_malformed_feedback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "source.log"
            log.write_text(
                "wp_presentation_feedback@1.presented(bad)\n"
                "wp_presentation_feedback@2.presented(0, 10, 1000000000, 1, 0, 1, 0)\n"
            )
            evidence = isolation.source_evidence(987, log, proc_root=root)
            self.assertFalse(evidence["alive"])
            self.assertFalse(evidence["presentationVerified"])
            self.assertIsNone(evidence["presentationFps"])
            self.assertEqual(evidence["invalidPresentationFeedback"], 2)

    def test_source_rate_without_presentation_feedback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "source.log"
            log.write_text("rate: 60/1\nfd= 0\n")
            evidence = isolation.source_evidence(987, log, proc_root=root)
            self.assertFalse(evidence["presentationVerified"])
            self.assertIsNone(evidence["presentationFps"])
            self.assertEqual(evidence["ffplayDroppedFrames"], 0)

    def test_partial_capture_retains_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "capture.jsonl"
            log.write_text("banner\n[]\n42\n")
            error = Path(directory) / "capture.stderr"
            error.write_text(
                json.dumps(
                    {
                        "event": "capture.failed",
                        "operation": "portal.session_closed",
                    }
                )
                + "\n"
            )
            data = isolation.read_capture(log, error)
            self.assertIsNone(data["summary"])
            self.assertEqual(
                data["failure"]["operation"], "portal.session_closed"
            )
            self.assertFalse(isolation.completed_control(data, False))

    def test_pool_experiment_requires_completed_controls_and_source(self):
        def control(cpu, fps):
            return {
                "exitCode": 3,
                "validControl": True,
                "source": {
                    "presentationVerified": True,
                    "presentationFps": 60,
                },
                "summary": {
                    "elapsedSeconds": 30,
                    "width": 1920,
                    "height": 1080,
                    "cpuCapture": cpu,
                    "fps": fps,
                    "maxBufferPoolSize": 4,
                    "discarded": 0,
                    "sequenceGaps": 0,
                    "outstandingBuffers": 0,
                    "received": 1200,
                    "released": 1200,
                    "delivered": 1200,
                },
                "timings": {
                    "fenceWait": {"p99": 17},
                    "bufferHeld": {"p99": 18},
                },
            }

        gpu, cpu = control(False, 40), control(True, 60)
        self.assertTrue(isolation.supports_pool_experiment(gpu, cpu))
        self.assertFalse(
            isolation.supports_pool_experiment({"summary": None}, {})
        )
        unverified_cpu = copy.deepcopy(cpu)
        unverified_cpu["source"]["presentationVerified"] = False
        self.assertFalse(
            isolation.supports_pool_experiment(gpu, unverified_cpu)
        )
        discarded_source = copy.deepcopy(gpu)
        discarded_source["source"]["discardedPresentationFeedback"] = 1
        self.assertFalse(
            isolation.supports_pool_experiment(discarded_source, cpu)
        )
        slow_fence = copy.deepcopy(gpu)
        slow_fence["timings"]["fenceWait"]["p99"] = 5
        self.assertFalse(isolation.supports_pool_experiment(slow_fence, cpu))
        for key, value in (
            ("elapsedSeconds", 15),
            ("released", 1199),
            ("sequenceGaps", 1),
            ("maxBufferPoolSize", 8),
        ):
            bad = copy.deepcopy(gpu)
            bad["summary"][key] = value
            self.assertFalse(isolation.supports_pool_experiment(bad, cpu))
        cpu["summary"]["fps"] = 40
        self.assertFalse(isolation.supports_pool_experiment(gpu, cpu))
        cpu["summary"]["fps"] = 60
        gpu["source"]["presentationVerified"] = False
        self.assertFalse(isolation.supports_pool_experiment(gpu, cpu))


if __name__ == "__main__":
    unittest.main()
