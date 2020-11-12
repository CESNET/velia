# High-level overview

This project monitors system state health and provides data about the health to the user.
Velia monitors a [complete system state](#health-of-system-services) (mainly from the systemd's view) and produces a single-value (OK, WARNING, ERROR) output about the system health.
This information is used (for example) to switch on the corresponding LEDs so user can see that the state is good or that something went wrong.
Velia also monitors [hardware state](#ietf-hardware) and provides the information to the user via [NETCONF](https://en.wikipedia.org/wiki/NETCONF) protocol using YANG [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) model.
See below for more information.

## Health of system services
Velia monitors system services and outputs the aggregated state via LEDs.
Individual inputs (responsible for monitoring a specific part of the system) are connected to the state manager (`velia::health::StateManager`) and push the state of the component they are responsible for.
The manager then aggregates the state (takes the worst one) and notifies the outputs (e.g., a LED driver) that are connected to the manager.

### Provided modules
Inputs:
 * Systemd-monitor (`velia::health::DbusSystemdInput`): Connect to systemd DBus API and monitor states of units in the system. It can also ignore selected units.
 * Semaphore-monitor (`velia::health::DbusSemaphoreInput`): Connect to some DBus object that provides a state. This can be useful for reading the state of other daemons that can export their state this way.

Outputs:
 * LED (`velia::health::LedSysfsDriver`): Output the current state by turning on some LEDs via the sysfs interface.

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
Users connect to a [Netopeer2 server](https://github.com/CESNET/netopeer2) and configure their stuff through Netopeer2-server which delegates configuration management to [Sysrepo](http://www.sysrepo.org/).
The Sysrepo talks to velia and asks for the current hardware state.
Velia then feeds the Sysrepo with the requested data.

### How are the requests processed?
The `velia::ietf_hardware::IETFHardware` instance is constructed and data readers are registered.
Then a Sysrepo communication class `velia::ietf_hardware::IETFHardwareSysrepo` is constructed with the instance of `IETFHardware`.
When somebody requests the data from Sysrepo, the `IETFHardwareSysrepo` instance asks the `IETFHardware` instance to provide the data and `IETFHardware` then asks all its data readers to provide the data they manage.
The data reader usually ask some underlying drivers (eMMC, HWMon) to provide the data from kernel and place them in the tree which is returned.
`IETFHardware` then merges all the data and returns them back to Sysrepo callback.
The Sysrepo callback pushes the corresponding YANG nodes back to Sysrepo using the [libyang](https://github.com/CESNET/libyang) interface.

### Sysrepo interface
The [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) YANG model works with nonconfiguration operational data only.
The actual data retrieval is implemented by `velia::ietf_hardware::sysrepo::IETFHardwareSysrepo` class.
Its constructor expects an instance of `velia::ietf_hardware::IETFHardware`.
The class automatically registers a callback into Sysrepo which is invoked when somebody requests the data.
When the callback is invoked by Sysrepo, the data are requested from the `IETFHardware` instance.
After the data are obtained (mapping XPath -> value), the `IETFHardwareSysrepo` instance builds a complete YANG tree and pushes the result into Sysrepo.

### IETFHardware class
Providing the data about the hardware state is handled by a `velia::ietf_hardware::IETFHardware` class.
The class is unaware of anything like Sysrepo or NETCONF.
However, the it returns [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) YANG model structure.
When asked, the instance returns a mapping from a XPath to a value, where the XPath corresponds to the path in the YANG data tree.

The class itself does not know what components are managed with the `ietf-hardware` module and it does not query the system for any hardware state on its own.
That is the job for a data reader.
After constructing `IETFHardware` instance you should register some of them.
The job of a data reader is to read out the actual hardware data and return the data in the form of mapping from a XPath to a value (the XPaths correspond to the paths defined by the YANG model).
Whenever `IETFHardware` is asked for the current hardware state, it asks all the registered data readers to provide the data (parts of the returned mapping), then it merges the data and returns them to the caller.

There is a set of predefined data readers.
For instance, there is a data reader for a temperature reading from sysfs (`velia::ietf_hardware::data_reader::SysfsTemperature`), or eMMC block device state (`velia::ietf_hardware::data_reader::EMMC`), etc.
The predefined components usually read something from sysfs so they are implemented as functors that take one object (eMMC or HWMon, see [Sysfs interface](#sysfs-interface)) and ask the object to provide the data.
However, you can easily create custom data readers.
A data reader is nothing else than just a function object with the `DataTree(void)` function signature.
It returns the data (i.e., XPath to a node and its value) it wants to propagate into the resulting YANG data tree when it is invoked.


### Sysfs interface
The interesting data are provided by kernel using the sysfs interface.
The `velia::ietf_hardware::sysfs::EMMC` and `velia::ietf_hardware::sysfs::HWMon` read and provide the sysfs data.
These drivers are constructed with a path to a single sysfs directory and they collect all interesting attributes (i.e. contents of interesting files) provided by the specific sysfs directory.
The interface for exporting the gathered data is unspecified.
Many drivers return a mapping `std::map<std::string, T>`, so that they can return attribute names and its values in one go.
Any object of these classes handle exactly one sysfs directory (specified when constructing the object).
To monitor two sysfs hwmon directories you must have two separate objects.

