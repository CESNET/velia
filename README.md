# YANG System management for embedded devices running Linux

Together with [sysrepo](https://www.sysrepo.org/), this software provides "general system management" of embedded devices.
The target platform is anything that runs Linux with [systemd](https://systemd.io/).
This runs in production on [CzechLight SDN DWDM devices](https://czechlight.cesnet.cz/en/open-line-system/sdn-roadm).

## Health tracking

This component tracks the overal health state of the system, including various sensors, or the state of `systemd` [units](https://www.freedesktop.org/software/systemd/man/systemd.unit.html).
As an operator-friendly LED at the front panel of the appliance shows the aggregated health state.

## System management

Firmware can be updated via [RAUC](https://rauc.io/), and various aspects of the system's configuration can be adjusted.

## Supported YANG models

For a full list, consult the [`yang/` directory](./yang/) in this repository.

- `ietf-hardware`
- `ietf-system`
- `czechlight-system`
