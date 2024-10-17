# YANG System management for embedded devices running Linux

Together with [sysrepo](https://www.sysrepo.org/), this software provides "general system management" of embedded devices.
The target platform is anything that runs Linux with [systemd](https://systemd.io/).
This runs in production on [CzechLight SDN DWDM devices](https://czechlight.cesnet.cz/en/open-line-system/sdn-roadm).

## Health tracking

This component tracks the overal health state of the system, including various sensors, or the state of `systemd` [units](https://www.freedesktop.org/software/systemd/man/systemd.unit.html).
As an operator-friendly LED at the front panel of the appliance shows the aggregated health state.

## System management

Firmware can be updated via [RAUC](https://rauc.io/), and various aspects of the system's configuration can be adjusted.
This includes a firewall, basic network settings, and authentication management.

## Supported YANG models

For a full list, consult the [`yang/` directory](./yang/) in this repository.

- [`ietf-access-control-list`, RFC 8519](https://tools.ietf.org/html/rfc8519) (with [deviations](./yang/czechlight-firewall@2021-01-25.yang))
- [`ietf-hardware`, RFC 8348](https://tools.ietf.org/html/rfc8348)
- [`ietf-system`, RFC 7317](https://tools.ietf.org/html/rfc7317) (partial support)
- [`ietf-interfaces`, RFC 8343](https://tools.ietf.org/html/rfc8343) (generating config for [`systemd-networkd`](https://www.freedesktop.org/software/systemd/man/systemd.network.html), with [extensions](./yang/czechlight-network@2021-02-22.yang))
- [`ietf-routing`, RFC 8349](https://tools.ietf.org/html/rfc8349) (see above)
- [`czechlight-system`](./yang/czechlight-system@2022-07-08.yang)

## Installation

For building, one needs:

- C++20 compiler (e.g., GCC 10.x+, clang 10+)
- CMake 3.19+
- [Boost](https://www.boost.org/) (we're testing with `1.78`)
- [`pkg-config`](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [`libnl-route`](http://www.infradead.org/~tgr/libnl/) for talking to the Linux kernel
- [`libsystemd`](https://www.freedesktop.org/software/systemd/man/libsystemd.html) and [`systemd`](https://www.freedesktop.org/wiki/Software/systemd/) at runtime
- [`libyang-cpp`](https://github.com/CESNET/libyang-cpp) - C++ bindings for *libyang*
- [`sysrepo-cpp`](https://github.com/sysrepo/sysrepo-cpp) - C++ bindings for *sysrepo*
- [`spdlog`](https://github.com/gabime/spdlog) - a logging library
- [`sdbus-c++`](https://github.com/Kistler-Group/sdbus-cpp) - C++ library for D-Bus
- [`fmt`](https://fmt.dev/) - C++ string formatting library
- [`nlohmann_json`](https://json.nlohmann.me/) - C++ JSON library
- [`docopt`](https://github.com/docopt/docopt.cpp) for CLI option parsing
- [`nft`](https://www.netfilter.org/projects/nftables/index.html) - the netfilter tool
- optionally, [Doctest](https://github.com/onqtam/doctest/) as a C++ unit test framework
- optionally, [trompeloeil](https://github.com/rollbear/trompeloeil) for mock objects in C++
- optionally, [`iproute2`](https://wiki.linuxfoundation.org/networking/iproute2) - the `ip` tool for testing
- optionally, [`jq`](https://jqlang.github.io/jq/) to run [some CLI utilities](./cli) and for testing them

The build process uses [CMake](https://cmake.org/runningcmake/).
A quick-and-dirty build with no fancy options can be as simple as `mkdir build && cd build && cmake .. && make && make install`.
