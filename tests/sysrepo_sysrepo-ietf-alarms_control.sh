#!/usr/bin/env bash

PIDFILE="./test-alarms.pid"

set -x

stop() {
    [[ ! -r "$PIDFILE" ]] && return 0 # no pidfile

    PID="$(cat "$PIDFILE")"

    if ps --pid "$PID" >/dev/null; then
      kill -15 -- "$PID" 2>/dev/null  # please terminate
      sleep 0.5
      while ps --pid "$PID" >/dev/null; do
        echo "porad"
        kill -9 -- "$PID" 2>/dev/null # shots fired
      done
    fi
    echo "" > "$PIDFILE"
}

# $1 ... path to daemon
start() {
    rm -f "$PIDFILE" # in case these files already exist

    if [[ ! -x "$1" ]]; then
        echo "$1 is not executable" >&2
        return 1
    fi

    $1 --sysrepo-log-level=5 1>$2 2>&1 &
    PID="$!"
    echo "$PID" > $PIDFILE

    # wait for init
    sleep 2

    echo "Started $1 (pid=$PID)" >&2
}

help() {
    echo "Usage:" >&2
    echo "  $1 start PATH_TO_SYSREPO_IETF_ALARMSD LOGFILE" >&2
    echo "  $1 stop" >&2
}

if [[ "$1" == "start" && $# == 3 ]]; then
    stop
    start "$2" "$3"
elif [[ "$1" == "stop" && $# == 1 ]]; then
    stop
else
    help
    exit 1
fi
