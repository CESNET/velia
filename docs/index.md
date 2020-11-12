# High-level overview

This project monitors system state health.
It consists of two parts.
The first one (`velia-hardware`) monitors hardware state and provides the information to the user via [NETCONF](https://en.wikipedia.org/wiki/NETCONF) 
protocol using YANG [ietf-hardware-state (RFC 8348)](https://tools.ietf.org/html/rfc8348#appendix-A) model.
The second one (`velia-systemstate`) monitors a complete system state and produces a single-value (OK, WARNING, ERROR) output about the system health.
See below for more information 

## System state (velia-systemstate)
`velia-systemstate` is a simple state aggregator.
Individual inputs (responsible for monitoring a specific part of the system) are connected to the state manager and push the state of the component they are responsible for.
The manager then aggregates the state (takes the worst one) and notifies the outputs that are connected to the manager.

### Provided modules
Inputs:
 * Systemd-monitor: Connect to systemd DBus API and monitor states of units in the system. It can also ignore selected units.
 * Semaphore-monitor: Connect to some DBus object that provides a state. This can be useful for reading the state of other daemons that can export their state this way.

Outputs:
 * LED: Output the current state by turning on some LEDs.

### Example
A systemd-monitor and semaphore-monitor inputs are connected to `velia-systemstate` along with a LED output.
Semaphore-monitor will connect to some DBus object's notifications and provide (let's say) the state OK.
All units are reported as active and running by systemd.

Both inputs signal the manager with the state OK, therefore a manager notifies the outputs with state OK and the LED will shine with a green light.
When a single (not-ignored) systemd unit fails (e.g., a daemon crashes), the systemd-monitor ínput will get a notification by systemd DBus API and notifies the manager.
The manager now receives an ERROR state from one input and OK state from the second one.
This makes the manager notify the outputs with the ERROR state and the LED will change its colour to red. 

## Hardware state
This project enables reading the hardware state of a node running this daemon via a standardized protocol, NETCONF.
Users connect to a [Netopeer2 server](https://github.com/CESNET/netopeer2) and configure their stuff through 
Netopeer2-server delegates configuration management to [Sysrepo](http://www.sysrepo.org/).
The Sysrepo talks to ``velia-hardware`` (this project) and asks for the current hardware state.
This daemon feeds the Sysrepo with the data it asked for. 

### Sysfs drivers 
Systems usually provide its sensor data via some files in sysfs (hwmon, emmc, ...)
We implement basic sysfs drivers (`src/velia-hardware/sysfs`) to read these data.
These drivers are constructed with a path to sysfs and they collect all interesting attributes (i.e., files) provided by the specific sysfs directory.
You can use these classes to query the current state (i.e., current content of the files in the directory).

### Hardware state
Hardware state is designed modularly.
There is a set of predefined callbacks (i.e. a module for temperature, emmc block device, ...).
After constructing `HardwareState` class you select which modules you wish to enable and where to put them in the resulting YANG data tree.
These modules implement callbacks used by the `HardwareState` class.
`HardwareState` class will collect all the data (for enabled modules) and return a dictionary data mapping of YANG data tree nodes to their values. 

### Sysrepo interface
Sysrepo interface is fairly simple in this daemon.
`ietf-hardware-state` YANG model works with nonconfiguration operational data only.
An operational data callback on "/ietf-hardware-state:hardware" is registered in Sysrepo. 
When the callback is invoked by Sysrepo, the data are requested from (already constructed) `HardwareState` instance.
After the data are obtained, the YANG tree is constructed and pushed into sysrepo.
