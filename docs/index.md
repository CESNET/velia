# High-level overview

This project monitors system state health and provides data about the health to the user.
Velia monitors a complete system state (mainly from the systemd's view) and produces a single-value (OK, WARNING, ERROR) output about the system health.
This information is used (for example) to switch on the corresponding LEDs so user can see that the state is good or that something went wrong.
Velia also monitors hardware state and provides the information to the user via [NETCONF](https://en.wikipedia.org/wiki/NETCONF) protocol using YANG [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) model.
See below for more information.

## Health of system services
Velia monitors system services and outputs the aggregated state via LEDs.
Individual inputs (responsible for monitoring a specific part of the system) are connected to the state manager and push the state of the component they are responsible for.
The manager then aggregates the state (takes the worst one) and notifies the outputs (e.g., a LED driver) that are connected to the manager.

### Provided modules
Inputs:
 * Systemd-monitor: Connect to systemd DBus API and monitor states of units in the system. It can also ignore selected units.
 * Semaphore-monitor: Connect to some DBus object that provides a state. This can be useful for reading the state of other daemons that can export their state this way.

Outputs:
 * LED: Output the current state by turning on some LEDs via the sysfs interface.

### Example
A systemd-monitor and semaphore-monitor inputs are connected to velia along with a LED output.
Semaphore-monitor will connect to some DBus object's notifications and provide (let's say) the state OK.
All units are reported as active and running by systemd.

Both inputs signal the manager with the state OK, therefore a manager notifies the outputs with state OK and the LED will shine with a green light.
When a single (not-ignored) systemd unit fails (e.g., a daemon crashes), the systemd-monitor ínput will get a notification by systemd DBus API and notifies the manager.
The manager now receives an ERROR state from one input and OK state from the second one.
This makes the manager notify the outputs with the ERROR state and the LED will change its colour to red.

## IETF Hardware
Velia allows reading the hardware state of a node running this daemon via a standardized protocol, NETCONF.
Users connect to a [Netopeer2 server](https://github.com/CESNET/netopeer2) and configure their stuff through Netopeer2-server delegates configuration management to [Sysrepo](http://www.sysrepo.org/).
The Sysrepo talks to `velia-hardwarestated` (this project) and asks for the current hardware state.
This daemon feeds the Sysrepo with the data it asked for.

### Sysfs interface
The interesting data are provided by kernel using the sysfs interface.
The `velia::ietf_hardware::sysfs::EMMC` and `velia::ietf_hardware::sysfs::HWMon` read and provide the sysfs data.
These drivers are constructed with a path to a single sysfs directory and they collect all interesting attributes (i.e. contents of interesting files) provided by the specific sysfs directory.
The interface in the communication is unspecified.
The eMMC driver returns the attributes provided by kernel as a `std::map<std::string, std::string>`.
The HWMon driver returns the data as a `std::map<std::string, int64_t>`.
Any object of these classes handle exactly one sysfs directory (specified when constructing the object).
To monitor two sysfs hwmon directories you must have two separate objects.

### HardwareState class
Providing the data about the hardware state is handled by a `velia::ietf_hardware::HardwareState` class.
It is designed to be modular.
There is a set of predefined data readers (i.e. a module for a temperature readout from sysfs, eMMC block device state, etc.).
After constructing `HardwareState` instance you should register some of the predefined data readers or provide yours (see next paragraph).
Whenever `HardwareState` is asked for the current hardware state, it asks all the registered data readers to provide the data, then it merges the data and returns them to the caller.

The class is unaware of anything like Sysrepo or NETCONF.
However, the data readers are aware of [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) YANG model structure.
All the readers return the data in the form of mapping from a XPath to a value.
The XPaths correspond to the paths defined by the YANG model.

A data reader returns the data (i.e., XPath to a node and its value) it wants to propagate into the resulting YANG data tree when it is invoked.
There is a bunch of predefined components but you can simply define yours.
The components are basically any function objects with the `DataTree(void)` function signature.
The predefined components usually read something from sysfs so they are implemented as functors that take one object (eMMC or HWMon, see above) and ask it to provide the data.

### Sysrepo interface
The Sysrepo callback is implemented by `velia::ietf_hardware::sysrepo::OpsCallback` class.
The `ietf-hardware-state` YANG model works with nonconfiguration operational data only.
The `OpsCallback` class expects an already constructed `velia::ietf_hardware::HardwareState` instance.
When the callback is invoked by Sysrepo, the data are requested from the `HardwareState` instance.
After the data are obtained (mapping XPath -> value), the YANG tree is constructed and pushed into sysrepo.

### How it is glued together
The `velia::ietf_hardware::HardwareState` instance is constructed and data readers are registered.
A Sysrepo callback is constructed with the instance of `HardwareState` and registered in Sysrepo.
When somebody requests the data from Sysrepo, Sysrepo invokes the callback and asks the `HardwareState` instance to provide the data.
The `HardwareState` asks all its data readers to send the data.
The data reader usually ask some underlying drivers (eMMC, HWMon) to provide the data from kernel and place them in the tree which is returned.
The `HardwareState` then merges all the data and returns them back to Sysrepo callback.
The Sysrepo callback pushes the corresponding YANG nodes back to Sysrepo using the [libyang](https://github.com/CESNET/libyang) interface.
