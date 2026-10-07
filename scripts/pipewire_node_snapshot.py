#!/usr/bin/env python3
"""Save bounded, non-sensitive PipeWire capture-node diagnostics."""

import argparse
import json
import os
from pathlib import Path
import subprocess


PROPERTIES = {
    "node.name",
    "node.rate",
    "node.latency",
    "node.max-latency",
    "node.driver",
    "node.want-driver",
    "media.class",
    "object.serial",
    "video.format",
    "video.size",
    "video.framerate",
    "target.object",
}
PARAMETERS = {"Format", "EnumFormat", "Buffers", "Latency", "ProcessLatency"}


def select_node(objects, node_id):
    if not isinstance(objects, list):
        raise ValueError("pw-dump did not return an object array")
    for item in objects:
        if not isinstance(item, dict) or item.get("id") != node_id:
            continue
        if item.get("type") != "PipeWire:Interface:Node":
            raise ValueError("Requested object is not a PipeWire node")
        info = item.get("info") or {}
        if not isinstance(info, dict):
            raise ValueError("Malformed node info")
        props = info.get("props") or {}
        params = info.get("params") or {}
        if not isinstance(props, dict) or not isinstance(params, dict):
            raise ValueError("Malformed node properties or parameters")
        return {
            "nodeId": node_id,
            "state": info.get("state"),
            "properties": {
                key: props[key] for key in sorted(PROPERTIES & props.keys())
            },
            "parameters": {
                key: params[key][:32]
                for key in sorted(PARAMETERS & params.keys())
                if isinstance(params[key], list)
            },
            "parameterLimit": 32,
            "truncatedParameters": [
                key
                for key in sorted(PARAMETERS & params.keys())
                if isinstance(params[key], list) and len(params[key]) > 32
            ],
            "clockQuantumVerified": False,
        }
    raise ValueError(
        "Node unavailable on the default remote; portal-scoped access may be required"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.node < 0:
        parser.error("Node ID must be nonnegative")
    try:
        result = subprocess.run(
            ["pw-dump", "-N", str(args.node)],
            capture_output=True,
            text=True,
            check=True,
            timeout=5,
        )
        if not result.stdout.strip():
            raise ValueError(
                "Node unavailable on the default remote; portal-scoped access may be required"
            )
        snapshot = select_node(json.loads(result.stdout), args.node)
        # Exclusive private output; do not overwrite prior measurements.
        descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w") as output:
            json.dump(snapshot, output, indent=2)
            output.write("\n")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(
            json.dumps(
                {
                    "event": "pipewire.snapshot_failed",
                    "nodeId": args.node,
                    "reason": str(error),
                }
            )
        )
        return 1
    print(
        json.dumps(
            {
                "event": "pipewire.snapshot_saved",
                "nodeId": args.node,
                "output": str(args.output),
            }
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
