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
    elif($type == "czechlight-roadm-common:hardware-alarm") then
        "\ud83d\udee0" # 🛠 (hammer and wrench)
    else
        $type + " " + $qualifier
    end;

def formatIsCleared($isCleared):
    if $isCleared == true then
        "cleared"
    elif $isCleared == false then
        "active"
    else
        ""
    end;

def filterCleared($config):
  if $config | has("cleared") then
    . | map(select(.["is-cleared"] == $config.cleared))
  else
    .
  end;

# Fetch the history count from the config, or return the provided default value if not present
def historyCount($config; $default):
  if $config | has("history") then
    $config.history
  else
    $default
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
    . | sort_by(.["is-cleared"] != (false, true), .["perceived-severity"] != ("critical", "major", "minor", "warning", "indeterminate", "cleared"));

# Format interesting status-change details fields along with passed params into a single array
def formatline($alarmTypeId; $alarmTypeQualifier; $resource; $cleared):
    . | [
          formatAlarmType($alarmTypeId; $alarmTypeQualifier),
          ($resource | formatResource),
          .["perceived-severity"],
          .["alarm-text"],
          .["time"],
          formatIsCleared($cleared)
      ];

def pop:
    .
    | length as $len
    | .[1:$len];

def main:
  (["", "Resource", "Severity", "Detail", "Timestamp", "Status"]),
  ( .
    | if . == null then halt else . end # guard against null object (no alarms)
    | filterCleared($config)
    | filterTimestamp($config)
    | sortBySeverity
    | .[]

    # Extract fields common to the alarm and its current state
    | .["alarm-type-id"] as $alarmTypeId
    | .["alarm-type-qualifier"] as $alarmTypeQualifier
    | .["resource"] as $resource
    | .["is-cleared"] as $cleared
    | .["perceived-severity"] as $severity
    | .["status-change"]
    | (
        # Print first status-change entry in the alarm, along with the alarm details and colorize it
        (. | first | formatline($alarmTypeId; $alarmTypeQualifier; $resource; $cleared) | colorize($severity; $cleared)),
        # Print other status-change entries (number is limited by historyCount) in the alarm but do not output the alarm details, print only emptystrings
        (limit(historyCount($config; . | length) - 1; (. | pop | sort_by(.time) | reverse | .[]? | formatline(""; ""; ""; ""))))
    )
  ) | @tsv;

.["ietf-alarms:alarms"]?["alarm-list"]?["alarm"]? | main
