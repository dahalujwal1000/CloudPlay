#!/usr/bin/env bash
# CloudPlay auto-sync control script.
#
# Manages the background daemon that watches the working tree and pushes code
# changes to GitHub automatically (see scripts/autosync.py).
#
# Usage:
#   ./scripts/autosync.sh start [extra autosync.py args...]
#   ./scripts/autosync.sh stop
#   ./scripts/autosync.sh restart [extra autosync.py args...]
#   ./scripts/autosync.sh status
#   ./scripts/autosync.sh logs [N]      # show last N log lines (default 40)
#   ./scripts/autosync.sh once          # sync once, then exit
#   ./scripts/autosync.sh pause         # pause auto-commit/push
#   ./scripts/autosync.sh resume        # resume auto-commit/push
set -euo pipefail

REPO_ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
GIT_DIR="$(git -C "$REPO_ROOT" rev-parse --git-dir)"
[[ "$GIT_DIR" != /* ]] && GIT_DIR="$REPO_ROOT/$GIT_DIR"

PY_SCRIPT="$REPO_ROOT/scripts/autosync.py"
PID_FILE="$GIT_DIR/autosync.pid"
LOG_FILE="$GIT_DIR/autosync.log"
PAUSE_FILE="$GIT_DIR/autosync.pause"
PYTHON="${PYTHON:-python3}"

pid_alive() {
    [[ -f "$PID_FILE" ]] || return 1
    local pid
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    [[ -n "$pid" ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

cmd_start() {
    if pid_alive; then
        echo "autosync already running (pid $(cat "$PID_FILE"))"
        return 0
    fi
    if [[ -e "$PAUSE_FILE" ]]; then
        echo "note: $PAUSE_FILE exists; the daemon will stay paused until 'resume'"
    fi
    : > "$LOG_FILE"
    nohup "$PYTHON" "$PY_SCRIPT" --path "$REPO_ROOT" "$@" >> "$LOG_FILE" 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"
    sleep 1
    if pid_alive; then
        echo "autosync started (pid $pid)"
        echo "log: $LOG_FILE"
    else
        echo "autosync failed to start; last log lines:" >&2
        tail -n 20 "$LOG_FILE" >&2 || true
        rm -f "$PID_FILE"
        return 1
    fi
}

cmd_stop() {
    if ! pid_alive; then
        echo "autosync is not running"
        rm -f "$PID_FILE"
        return 0
    fi
    local pid
    pid="$(cat "$PID_FILE")"
    kill "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.2
    done
    if kill -0 "$pid" 2>/dev/null; then
        echo "force killing pid $pid"
        kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
    echo "autosync stopped"
}

cmd_status() {
    if pid_alive; then
        echo "autosync: running (pid $(cat "$PID_FILE"))"
    else
        echo "autosync: stopped"
    fi
    if [[ -e "$PAUSE_FILE" ]]; then
        echo "state:   PAUSED ($PAUSE_FILE present)"
    else
        echo "state:   active"
    fi
    echo "log:     $LOG_FILE"
}

cmd_logs() {
    local n="${1:-40}"
    [[ -f "$LOG_FILE" ]] && tail -n "$n" "$LOG_FILE" || echo "no log file yet"
}

cmd_once() {
    "$PYTHON" "$PY_SCRIPT" --path "$REPO_ROOT" --once "$@"
}

cmd_pause() {
    : > "$PAUSE_FILE"
    echo "autosync paused ($PAUSE_FILE)"
}

cmd_resume() {
    rm -f "$PAUSE_FILE"
    echo "autosync resumed"
}

action="${1:-status}"
shift || true

case "$action" in
    start)   cmd_start "$@" ;;
    stop)    cmd_stop ;;
    restart) cmd_stop; cmd_start "$@" ;;
    status)  cmd_status ;;
    logs)    cmd_logs "$@" ;;
    once)    cmd_once "$@" ;;
    pause)   cmd_pause ;;
    resume)  cmd_resume ;;
    *)
        echo "unknown command: $action" >&2
        sed -n '2,18p' "${BASH_SOURCE[0]}" >&2
        exit 2
        ;;
esac
