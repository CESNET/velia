# Health tracking for embedded devices running Linux

This software tracks health of an embedded device which runs Linux with systemd.

Velia tracks health of systemd units. In case some of them are failing, the
system is considered unhealthy.

By default, the health of state is shown by flashing certain LEDs. This is
however customizable by using your own callbacks.
