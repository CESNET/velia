#!/usr/bin/env bash

PIDFILE1="./test-merge1.pid"
PIDFILE2="./test-merge2.pid"

set -x

stop() {
  for pidfile in "$PIDFILE1" "$PIDFILE2"; do
    [[ ! -r "$pidfile" ]] && continue  # no pidfile

    PID="$(cat "$pidfile")"

    if ps --pid "$PID" >/dev/null; then
      kill -SIGTERM "$PID" 2>/dev/null  # please terminate
      sleep 0.5
      if ps --pid "$PID" >/dev/null; then
        kill -SIGKILL "$PID" 2>/dev/null # shots fired
      fi
    fi
    echo "" > "$pidfile"
  done
}

start() {
  rm -f "$PID1.sysrepo" "$PID2.sysrepo" # in case these files already exist

  ./test-sysrepo_test_merge-daemon --subscribe 2>/dev/null 1>/dev/null &  # ctest waits here if those file descriptors are open
  PID1="$!"
  echo "$PID1" > $PIDFILE1

  ./test-sysrepo_test_merge-daemon --set-item  2>/dev/null 1>/dev/null &
  PID2="$!"
  echo "$PID2" > $PIDFILE2

  echo "Started both daemons ("$PID1", "$PID2")" >&2
  echo "Waiting for sysrepo initialization" >&2
  while [ ! -e "$PID1.sysrepo" ] && [ ! -e "$PID2.sysrepo" ]; do
    sleep 0.1
  done
  echo "Done" >&2

}

if [ $# -ne 1 ]; then
  echo "Usage: $0 start|stop" >&2
  exit 1
elif [ "$1" == "start" ]; then
  stop
  start
elif [ "$1" == "stop" ]; then
  stop
fi

set +x
exit 0
