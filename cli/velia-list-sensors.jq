# constructs ansii color code escape sequence
def ansi(code): "\u001b[" + code + "m" + . + "\u001b[0m";

def colorize($faulty): if $faulty then . | map(ansi("31")) else . end;

# a string of $n zeros, "" when there are none (jq's repeat operator gives null)
def zeros($n):
    if $n > 0 then "0" * $n else "" end;

# Insert a decimal point $decimals digits from the right of a string of digits:
# "44171" with 3 decimals becomes "44.171", "7" with 6 becomes "0.000007".
# Pure magic.
def insertDecimalPoint($decimals):
    if length > $decimals then
        .[: -$decimals] + "." + .[-$decimals :]
    else
        "0." + zeros($decimals - length) + .
    end;

# 45.000 -> 45
def withoutTrailingZeros: sub("\\.?0+$"; "");

def scaleExponent:
    if . == null then
        0
    else
        {
            "yocto": -24,
            "zepto": -21,
            "atto": -18,
            "femto": -15,
            "pico": -12,
            "nano": -9,
            "micro": -6,
            "milli": -3,
            "units": 0,
            "kilo": 3,
            "mega": 6,
            "giga": 9,
            "tera": 12,
            "peta": 15,
            "exa": 18,
            "zetta": 21,
            "yotta": 24
        }[.]
    end;

# Print `. * 10^$exponent` as a plain decimal string:
#
#     44171 with -3  ->  "44.171"
#         7 with -6  ->  "0.000007"
#     45000 with -3  ->  "45"
#        -3 with  3  ->  "-3000"
#
# The digits are moved around by hand instead of the number being divided because jq does not have printf equivalent.
# Values switch to the scientific notation below 1e-4, so 7 mW would come out as "7e-06 W".
def asDecimalString($exponent):
    if . == 0 then "0"
    elif . < 0 then "-" + (- . | asDecimalString($exponent)) # print "-" sign and print abs(value)
    elif $exponent >= 0 then tostring + zeros($exponent) # decimal point moves right, past all the digits -> append zeroes
    else tostring | insertDecimalPoint(- $exponent) | withoutTrailingZeros # ...or left, in between them
    end;

def valueExponent:
    (.["value-scale"] | scaleExponent) as $scale
    | (.["value-precision"] // 0) as $precision
    | if $precision > 0 then
        $scale - $precision # a fixed-point number, the precision counts its fractional digits
      else
        $scale
      end;

def sensorUnits:
    if ((.["value-type"] // "other") | IN("other", "unknown")) then
       .["units-display"] // ""
    else
        {
            "volts-AC": "V AC",
            "volts-DC": "V DC",
            "amperes": "A",
            "watts": "W",
            "hertz": "Hz",
            "celsius": "°C",
            "percent-RH": "%RH",
            "rpm": "RPM",
            "cmm": "m3/min"
        }[.["value-type"]] // ""
    end;

# If the value-type is 'voltsAC', 'voltsDC', 'amperes', 'watts', 'hertz', 'celsius', or 'cmm' then
# both extremes of the sensor-value range are report that the sensor value underflowed or overflowed.
def outOfRange:
    (.["value-type"] | IN("volts-AC", "volts-DC", "amperes", "watts", "hertz", "celsius", "cmm")) as $overflowApply
    | if $overflowApply and .["value"] == -1000000000 then "underflow"
      elif $overflowApply and .["value"] == 1000000000 then "overflow"
      else null
      end;

def formatValue($overflow):
    if $overflow then
        $overflow # velia reports both the overflow and a nonoperational status, so this has to be checked first
    elif (.["oper-status"] // "ok") != "ok" or .["value"] == null then
        "N/A"
    elif (.["value-type"] // "other") == "truth-value" then
        (if .["value"] == 1 then "true" else "false" end)
    else
        valueExponent as $exponent
        | sensorUnits as $units
        | (.["value"] | asDecimalString($exponent))
          + (if $units == "" then "" else " " + $units end)
    end;

def main:
  (["Sensor", "Value", "Status"]),
  ( .
    | if . == null then halt else . end # guard against "no hardware"
    | map(select(has("sensor-data")))
    | sort_by(.name)
    | .[]
    | .["name"] as $name
    | .["sensor-data"]
    | outOfRange as $overflow
    | (.["oper-status"] // "ok") as $operStatus
    | [
        $name,
        formatValue($overflow),
        (if $operStatus == "ok" then "" else $operStatus end)
      ]
    | colorize($operStatus != "ok" or $overflow)
  ) | @tsv;

.["ietf-hardware:hardware"]?["component"]? | main
