# YANG System management for embedded devices running Linux

Together with [sysrepo](https://www.sysrepo.org/), this software provides "general system management" for embedded network devices.
The target platform is anything that runs Linux with [systemd](https://systemd.io/).
The software runs in production on [CzechLight SDN DWDM devices](https://czechlight.cesnet.cz/en/open-line-system/sdn-roadm).

## Features

- *system management*
    - basic system info via the [`ietf-system`, RFC 7317](https://tools.ietf.org/html/rfc7317) YANG model
    - user accounts and authentication
        - passwords and SSH keys, [`/czechlight-system:authentication`](./yang/czechlight-system@2022-07-08.yang))
    - firmware updates via [RAUC](https://rauc.io/), in the [`/czechlight-system:firmware` YANG](./yang/czechlight-system@2022-07-08.yang)
    - access to hardware's LEDs via the [`/czechlight-system:leds` YANG](./yang/czechlight-system@2022-07-08.yang)
    - remote logging via [`systemd-journal-upload`](https://www.freedesktop.org/software/systemd/man/latest/systemd-journal-upload.service.html)
- *health reporting* of both hardware and software
    - tracks restarts and failures of all enabled [`systemd.unit(5)`](https://www.freedesktop.org/software/systemd/man/systemd.unit.html)
    - hardware health (temperature, fan RPM, voltages, missing components)
    - available via the [`ietf-alarms` (RFC 8632)](https://datatracker.ietf.org/doc/html/rfc8632) and [`ietf-hardware` (RFC 8348)](https://tools.ietf.org/html/rfc8348) YANG models
    - status LED
- *network management*
    - configuration with [`systemd-networkd`](https://www.freedesktop.org/software/systemd/man/systemd.network.html)
        - focus on Ethernet interfaces and basic setup of bridges
        - IPv4 and IPv6 configuration, with DHCP and autoconfiguration
    - real-time status and statistics via [`netlink(7)`](https://man7.org/linux/man-pages/man7/netlink.7.html)
    - support for [`ietf-interfaces`, RFC 8343](https://tools.ietf.org/html/rfc8343) and [`ietf-routing`, RFC 8349](https://tools.ietf.org/html/rfc8349) with [extensions](./yang/czechlight-network@2021-02-22.yang)
    - firewall ([`ietf-access-control-list`, RFC 8519](https://tools.ietf.org/html/rfc8519) with [deviations](./yang/czechlight-firewall@2021-01-25.yang))

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
- [`sysrepo-ietf-alarms`](https://github.com/CESNET/sysrepo-ietf-alarms) - the sysrepo alarm manager
- optionally, [Doctest](https://github.com/doctest/doctest/) as a C++ unit test framework
- optionally, [trompeloeil](https://github.com/rollbear/trompeloeil) for mock objects in C++
- optionally, [`iproute2`](https://wiki.linuxfoundation.org/networking/iproute2) - the `ip` tool for testing
- optionally, [`jq`](https://jqlang.github.io/jq/) to run [some CLI utilities](./cli) and for testing them

The build process uses [CMake](https://cmake.org/runningcmake/).
A quick-and-dirty build with no fancy options can be as simple as `mkdir build && cd build && cmake .. && make && make install`.
