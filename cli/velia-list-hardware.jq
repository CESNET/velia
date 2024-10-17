def filter_interesting:
    . | map(select(
        .name == "ne" # always show the root NE item
        # ...or any component which shows "something useful" to the user
        or has("serial-num")
        or has("model-name")
        or has("hardware-rev")
        or has("software-rev")
        or has("firmware-rev")
        ))
    ;


def main:
  (["HW", "Manufacturer", "Model", "Version", "S/N"]),
  ( .
    | if . == null then halt else . end # guard against "no hardware"
    | filter_interesting
    | sort_by(.name)
    | .[]
    | [
        .["name"],
        .["mfg-name"],
        .["model-name"],
        ([.["hardware-rev"], .["software-rev"], .["firmware-rev"] | select(.)] | join(" ")),
        .["serial-num"]
      ]
  ) | @tsv;

.["ietf-hardware:hardware"]?["component"]? | main
