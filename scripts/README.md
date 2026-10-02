# CloudPlay — Automatic GitHub Sync

This directory contains the tooling that keeps the local working tree in sync
with the GitHub remote (`origin`). Whenever code changes, they are detected,
committed and pushed automatically.

## Components

| File | Purpose |
| --- | --- |
| `autosync.py` | Dependency-free daemon (Python stdlib) that watches the working tree, debounces changes, then commits + pushes. |
| `autosync.sh` | Control script: `start`, `stop`, `restart`, `status`, `logs`, `once`, `pause`, `resume`. |
| `hooks/post-commit` | Git hook that pushes immediately after every manual commit. |
| `install-hooks.sh` | Copies `hooks/*` into `.git/hooks/` and makes them executable. |

## One-time setup

```bash
# from the repository root
git remote add origin https://github.com/dahalujwal1000/CloudPlay.git   # if not already added
./scripts/install-hooks.sh                                              # install the post-commit hook
```

## Everyday use

```bash
./scripts/autosync.sh start        # start watching (runs in the background)
./scripts/autosync.sh status       # is it running?
./scripts/autosync.sh logs 50      # last 50 log lines
./scripts/autosync.sh stop         # stop watching
```

Tuning (passed straight through to `autosync.py`):

```bash
./scripts/autosync.sh start --interval 3 --debounce 10
./scripts/autosync.sh start --message "chore: autosave"
```

Pausing while you do something delicate (a rebase, a large refactor, etc.):

```bash
./scripts/autosync.sh pause
# ... work ...
./scripts/autosync.sh resume
```

A single manual sync without starting the daemon:

```bash
./scripts/autosync.sh once
```

## How it works

1. `autosync.py` polls `git status --porcelain` (so `.gitignore` is respected)
   every `--interval` seconds.
2. When a change is seen it waits `--debounce` seconds for the tree to go quiet,
   so partially-written files are never committed.
3. It then runs `git add -A`, `git commit -m "<message> <UTC timestamp>"` and
   `git push -u origin <branch>` (with retries on failure).
4. Manually created commits with a clean tree are also pushed.
5. Sync is skipped automatically during a merge / rebase / cherry-pick / revert /
   bisect, or while the pause sentinel exists.

## Notes & caveats

* Hooks live in `.git/hooks/` and are **not** tracked by git; run
  `./scripts/install-hooks.sh` after every fresh clone.
* The daemon commits **everything that is not git-ignored**. Keep secrets out of
  the tree or add them to `.gitignore` (see `AGENTS.md` → Security).
* Logs and the PID file live under `.git/` (`autosync.log`, `autosync.pid`) and
  are therefore never committed.
* The VS Code workspace settings (`.vscode/settings.json`) additionally enable
  smart-commit and auto-push after commits made from the editor UI.
