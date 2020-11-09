# Health tracking for embedded devices running Linux

This software tracks health of an embedded device which runs Linux with systemd.

Velia tracks health of systemd units. In case some of them are failing, the
system is considered unhealthy.
You can disable monitoring of some units by using `--systemd-ignore-unit` CLI
flags. For example, to disable monitoring unit `sshd.service` you should start
velia with `--systemd-ignore-unit=sshd.service`. In order to disable multiple
units use the flag multiple times.

By default, the health of state is shown by flashing certain LEDs. This is
however customizable by using your own callbacks.
