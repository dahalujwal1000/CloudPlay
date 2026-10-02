#!/usr/bin/env python3
"""CloudPlay auto-sync daemon.

Watches the repository working tree and, whenever code changes are detected,
automatically commits them and pushes to the configured GitHub remote.

Only the Python standard library is used (no external dependencies), so it
runs anywhere the project already builds.

Behaviour
---------
* Change detection uses ``git status --porcelain`` so ``.gitignore`` is
  honoured and only genuinely committable changes are considered.
* Changes are debounced: a commit+push only happens once the working tree has
  been quiet for ``--debounce`` seconds, so half-written files are never
  captured.
* Sync is skipped while a merge / rebase / cherry-pick / revert / bisect is in
  progress.
* Sync can be paused at runtime by creating the sentinel file
  ``<git-dir>/autosync.pause`` (see ``scripts/autosync.sh pause``).
* Local commits made by hand are pushed even when the working tree is clean.

Usage
-----
    python3 scripts/autosync.py [--interval 5] [--debounce 15]
                                [--remote origin] [--branch main]
                                [--message "chore(autosync): update"]
                                [--once] [--path DIR]
"""
from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

DEFAULT_MESSAGE = "chore(autosync): update"


def _run(args, cwd):
    """Run a command, returning (returncode, stdout, stderr) as text."""
    try:
        proc = subprocess.run(
            args,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as exc:  # pragma: no cover - environment issue
        raise RuntimeError(f"executable not found: {args[0]}") from exc
    return proc.returncode, proc.stdout, proc.stderr


def log(message: str) -> None:
    """Timestamped log line (stdout is redirected to a log file by the launcher)."""
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    print(f"[{stamp}] {message}", flush=True)


class Repo:
    """Thin wrapper around the git commands the daemon needs."""

    def __init__(self, path: str) -> None:
        self.path = os.path.abspath(path)
        rc, out, err = _run(["git", "rev-parse", "--show-toplevel"], self.path)
        if rc != 0:
            raise RuntimeError(f"not a git repository: {self.path}: {err.strip()}")
        self.root = out.strip()

    def git(self, *args: str):
        return _run(["git", *args], self.root)

    def git_dir(self) -> str:
        _, out, _ = self.git("rev-parse", "--git-dir")
        path = out.strip()
        return path if os.path.isabs(path) else os.path.join(self.root, path)

    # -- state helpers -------------------------------------------------
    def paused(self) -> bool:
        return os.path.exists(os.path.join(self.git_dir(), "autosync.pause"))

    def operation_in_progress(self):
        git_dir = self.git_dir()
        markers = (
            "rebase-merge",
            "rebase-apply",
            "MERGE_HEAD",
            "CHERRY_PICK_HEAD",
            "REVERT_HEAD",
            "BISECT_LOG",
        )
        for marker in markers:
            if os.path.exists(os.path.join(git_dir, marker)):
                return marker
        return None

    def branch(self) -> str:
        rc, out, _ = self.git("symbolic-ref", "--short", "-q", "HEAD")
        return out.strip() if rc == 0 else ""

    def has_commits(self) -> bool:
        rc, _, _ = self.git("rev-parse", "--verify", "-q", "HEAD")
        return rc == 0

    def status(self) -> str:
        _, out, _ = self.git("status", "--porcelain")
        return out

    def changed_files(self):
        files = []
        for line in self.status().splitlines():
            if len(line) > 3:
                files.append(line[3:].strip())
        return files

    def unpushed_count(self):
        """Number of local commits not on the upstream, or None if no upstream."""
        rc, out, _ = self.git("rev-list", "--count", "@{upstream}..HEAD")
        if rc != 0:
            return None
        try:
            return int(out.strip())
        except ValueError:
            return None

    def fingerprint(self) -> str:
        _, head, _ = self.git("rev-parse", "HEAD")
        return f"{head.strip()}|{self.unpushed_count()}|{self.status()}"

    # -- actions -------------------------------------------------------
    def commit_all(self, message: str):
        self.git("add", "-A")
        rc, out, err = self.git("commit", "-m", message)
        return rc == 0, (out + err).strip()

    def push(self, remote: str, branch: str):
        rc, out, err = self.git("push", "-u", remote, branch)
        return rc == 0, (out + err).strip()


def sync_once(repo: Repo, remote: str, branch: str, message: str, retries: int = 3) -> str:
    """Commit (if needed) and push once. Returns a short status string."""
    if repo.paused():
        log("autosync paused (sentinel present); skipping")
        return "paused"

    marker = repo.operation_in_progress()
    if marker:
        log(f"git operation in progress ({marker}); skipping")
        return "in-progress"

    target = branch or repo.branch()
    if not target:
        log("detached HEAD; skipping")
        return "detached"

    committed = False
    if repo.status().strip():
        files = repo.changed_files()
        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        summary = f"{message} {stamp}"
        log(f"detected {len(files)} changed path(s); committing")
        ok, detail = repo.commit_all(summary)
        if ok:
            committed = True
            log(f"committed: {summary}")
        else:
            tail = detail.splitlines()[-1] if detail else "no changes"
            log(f"commit skipped: {tail}")
    elif not repo.has_commits():
        log("empty repository with no changes; nothing to do")
        return "no-op"

    unpushed = repo.unpushed_count()
    needs_push = committed or unpushed is None or (unpushed or 0) > 0
    if not needs_push:
        log("nothing to push (working tree clean, upstream up to date)")
        return "clean"

    for attempt in range(1, retries + 1):
        ok, detail = repo.push(remote, target)
        if ok:
            log(f"pushed {target} -> {remote}")
            return "pushed"
        tail = detail.splitlines()[-1] if detail else ""
        log(f"push attempt {attempt}/{retries} failed: {tail}")
        if attempt < retries:
            time.sleep(min(2 ** attempt, 15))
    log("push failed after retries; will retry on next change")
    return "push-failed"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="CloudPlay auto-sync daemon")
    parser.add_argument("--path", default=".", help="path inside the git repository")
    parser.add_argument("--interval", type=float, default=5.0, help="poll interval (seconds)")
    parser.add_argument("--debounce", type=float, default=15.0, help="quiet period before committing (seconds)")
    parser.add_argument("--remote", default="origin", help="git remote name")
    parser.add_argument("--branch", default="", help="branch to push (default: current branch)")
    parser.add_argument("--message", default=DEFAULT_MESSAGE, help="base commit message")
    parser.add_argument("--once", action="store_true", help="sync once and exit")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        repo = Repo(args.path)
    except RuntimeError as exc:
        log(f"error: {exc}")
        return 1

    log(f"repository: {repo.root}")
    log(
        f"remote={args.remote} branch={args.branch or repo.branch() or '(current)'} "
        f"interval={args.interval}s debounce={args.debounce}s"
    )

    if args.once:
        sync_once(repo, args.remote, args.branch, args.message)
        return 0

    stopping = {"flag": False}

    def _handle(signum, _frame):
        stopping["flag"] = True
        log(f"received signal {signum}; shutting down")

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    log("watching for changes (Ctrl-C to stop)")
    last_fp = None
    dirty_since = None

    while not stopping["flag"]:
        try:
            fp = repo.fingerprint()
            now = time.time()

            if fp != last_fp:
                last_fp = fp
                if repo.status().strip():
                    if dirty_since is None:
                        log("change detected; waiting for the tree to settle")
                    dirty_since = now
                else:
                    dirty_since = None

            if dirty_since is not None and (now - dirty_since) >= args.debounce:
                sync_once(repo, args.remote, args.branch, args.message)
                dirty_since = None
                last_fp = repo.fingerprint()
            elif dirty_since is None and repo.has_commits() and (repo.unpushed_count() or 0) > 0:
                # Hand-made commit(s) with a clean tree: push them promptly.
                sync_once(repo, args.remote, args.branch, args.message)
                last_fp = repo.fingerprint()
        except Exception as exc:  # keep the daemon alive no matter what
            log(f"unexpected error: {exc}")

        # Sleep in small slices so Ctrl-C / SIGTERM is responsive.
        for _ in range(int(max(args.interval, 0.2) * 10)):
            if stopping["flag"]:
                break
            time.sleep(0.1)

    log("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
