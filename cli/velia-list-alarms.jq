# constructs ansii color code escape sequence
def ansi(code): "\u001b[" + code + "m" + . + "\u001b[0m";

# Colorize every array element based on the severity and cleared status
def colorize(severity; cleared):
    if cleared == true then . # keep default color
    elif severity == "critical" then . | map(ansi("31;1")) # red bold
    elif severity == "major" then . | map(ansi("31")) # red
    elif severity == "minor" then . | map(ansi("33")) # yellow
    elif severity == "warning" then . | map(ansi("33;1")) # yellow bold
    elif severity == "indeterminate" then . | map(ansi("31")) # white
    else . # default color
    end;

# Extracts resource name from /ietf-hardware xpath, if possible. In other cases, returns the input.
def formatResource:
    if(test("/ietf-hardware:hardware/component*")) then
        capture("/ietf-hardware:hardware/component\\[name='(?<resource>.*)']") | .resource
    else
        .
    end;

# Formats the alarm type and qualifier
def formatAlarmType($type;$qualifier):
    if($type == "velia-alarms:sensor-low-value-alarm") then
        "\u23f7" # ⏷ (upwards triangle)
    elif($type == "velia-alarms:sensor-high-value-alarm") then
        "\u23f6" # ⏶ (downwards triangle)
    elif($type == "velia-alarms:systemd-unit-failure") then
        "\u23f8" # ⏸ (pause)
    elif($type == "velia-alarms:sensor-missing-alarm") then
        "\u2715" # ✕ (multiplication X)
    else
        $type + " " + $qualifier
    end;

def formatIsCleared($isCleared):
    if $isCleared then
        "cleared"
    else
        "active"
    end;

def filterCleared($config):
  if $config | has("cleared") then
    . | map(select(.["is-cleared"] == $config.cleared))
  else
    .
  end;

def parseTimestamp:
  . | strptime("%Y-%m-%dT%H:%M:%S%Z");

def filterTimestamp($config):
  if $config | has("until") then
    . | map(select((.["last-raised"] | parseTimestamp ) <= ($config.until | parseTimestamp)))
  else
    .
  end |
  if $config | has("since") then
    . | map(select((.["last-raised"] | parseTimestamp ) >= ($config.since | parseTimestamp)))
  else
    .
  end;

# https://stackoverflow.com/questions/76476166/jq-sorting-by-value
def sortBySeverity:
    . | sort_by(.["perceived-severity"] != ("critical", "major", "minor", "warning", "indeterminate", "cleared"));

def main:
  (["", "Resource", "Severity", "Detail", "Last raised", "Status"]),
  ( .
    | if . == null then halt else . end # guard against null object (no alarms)
    | filterCleared($config)
    | filterTimestamp($config)
    | sortBySeverity
    | .[]
    | .["perceived-severity"] as $severity
    | .["is-cleared"] as $cleared
    | [formatAlarmType(.["alarm-type-id"]; .["alarm-type-qualifier"]),
       (.["resource"] | formatResource),
       .["perceived-severity"],
       .["alarm-text"],
       .["last-raised"],
       formatIsCleared(.["is-cleared"])
      ]
    | colorize($severity; $cleared)
  ) | @tsv;

.["ietf-alarms:alarms"]?["alarm-list"]?["alarm"]? | main
