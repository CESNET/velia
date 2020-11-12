# High-level overview

This project monitors system state health.
It consists of two parts.
The first one (`velia-ietf-hardwared`) monitors hardware state and provides the information to the user via [NETCONF](https://en.wikipedia.org/wiki/NETCONF) protocol using YANG [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) model.
The second one (`velia-healthd`) monitors a complete system state and produces a single-value (OK, WARNING, ERROR) output about the system health.
See below for more information.

## Health of system services (velia-healthd)
`velia-healthd` monitors system services and outputs the aggregated state via LEDs.
Individual inputs (responsible for monitoring a specific part of the system) are connected to the state manager and push the state of the component they are responsible for.
The manager then aggregates the state (takes the worst one) and notifies the outputs (e.g., a LED driver) that are connected to the manager.

### Provided modules
Inputs:
 * Systemd-monitor: Connect to systemd DBus API and monitor states of units in the system. It can also ignore selected units.
 * Semaphore-monitor: Connect to some DBus object that provides a state. This can be useful for reading the state of other daemons that can export their state this way.

Outputs:
 * LED: Output the current state by turning on some LEDs.

### Example
A systemd-monitor and semaphore-monitor inputs are connected to `velia-systemstated` along with a LED output.
Semaphore-monitor will connect to some DBus object's notifications and provide (let's say) the state OK.
All units are reported as active and running by systemd.

Both inputs signal the manager with the state OK, therefore a manager notifies the outputs with state OK and the LED will shine with a green light.
When a single (not-ignored) systemd unit fails (e.g., a daemon crashes), the systemd-monitor ínput will get a notification by systemd DBus API and notifies the manager.
The manager now receives an ERROR state from one input and OK state from the second one.
This makes the manager notify the outputs with the ERROR state and the LED will change its colour to red.

## IETF Hardware (velia-ietf-hardwared)
`velia-ietf-hardwared` allows reading the hardware state of a node running this daemon via a standardized protocol, NETCONF.
Users connect to a [Netopeer2 server](https://github.com/CESNET/netopeer2) and configure their stuff through
Netopeer2-server delegates configuration management to [Sysrepo](http://www.sysrepo.org/).
The Sysrepo talks to `velia-hardwarestated` (this project) and asks for the current hardware state.
This daemon feeds the Sysrepo with the data it asked for.

### Sysfs interface
The interesting data are provided by kernel using the sysfs interface.
The `velia::ietf_hardware::sysfs::EMMC` and `velia::ietf_hardware::sysfs::HWMon` classes are the middlewares in the communication.
These drivers are constructed with a path to a single sysfs directory and they collect all interesting attributes (i.e. contents of interesting files) provided by the specific sysfs directory.
The interface in the communication is unspecified.
The eMMC driver returns the attributes provided by kernel as a `std::map<std::string, std::string>`.
The HWMon driver returns the data as a `std::map<std::string, int64_t>`.
Any object of this class handles exactly one sysfs directory (specified when constructing the object).
To monitor two sysfs hwmon directories you must have two separate `velia::ietf_hardware::sysfs::HWMon` instances.

### HardwareState class
Providing the data about the hardware state is handled by a `velia::ietf_hardware::HardwareState` class.
It is designed to be modular.
There is a set of predefined components (i.e. a module for a temperature readout from sysfs, eMMC block device state, etc.).
After constructing `velia::ietf_hardware::HardwareState` instance you should register some of the components or provide yours (see next paragraph).
Whenever `velia::ietf_hardware::HardwareState` is asked for the current hardware state, it asks all the registered components for their state, merges the data and returns the merged data to the caller.

The class is unaware of anything like Sysrepo.
However, it is aware of the structure of [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) YANG model.
All the components return their data in the form of mapping from XPath to a value.
The XPath corresponds to the paths defined by the YANG model.

A component returns the data (i.e., XPath to a node and its value) it wants to propagate into the resulting YANG data tree when it is invoked.
There is a bunch of predefined components but you can simply define yours.
The components are basically any function objects with the `DataTree(void)` function signature.
The predefined components usually read something from sysfs so they are implemented as functors that take one object (eMMC or HWMon, see above) and ask it to provide the data.

### Sysrepo interface
The Sysrepo callback is implemented by `velia::ietf_hardware::sysrepo::OpsCallback` class.
The `ietf-hardware-state` YANG model works with nonconfiguration operational data only.
The `velia::ietf_hardware::sysrepo::OpsCallback` class expects an already constructed `velia::ietf_hardware::HardwareState` instance.
When the callback is invoked by Sysrepo, the data are requested from the `velia::ietf_hardware::HardwareState` instance.
After the data are obtained (mapping XPath -> value), the YANG tree is constructed and pushed into sysrepo.

### How it is glued together
The `velia::ietf_hardware::HardwareState` instance is constructed and components are registered.
A Sysrepo callback is constructed with the instance of `velia::ietf_hardware::HardwareState` and registered in Sysrepo.
When somebody requests the data we provide from Sysrepo, it invokes the callback and asks the `velia::ietf_hardware::HardwareState` instance to provide the data.
The `velia::ietf_hardware::HardwareState` asks all its components to send the data and merges them.
The components usually ask some underlying drivers (eMMC, HWMon) to provide the data from kernel and then they place them in the tree.
