# MangoHud for FreeBSD
WIP FreeBSD MangoHud patches

### Note: ###
Reading CPU wattage information requires the cpuctl kernel module to be loaded and /dev/cpuctl0 readable by the user, such as via a devfs rule.

In **/etc/devfs.rules** add:

```[localrules=5]
add path 'cpuctl*' mode 0640 group cpuctl```

Then:

```pw groupadd cpuctl
pw groupmod cpuctl -m yourusername
sysrc devfs_system_ruleset="localrules"
service devfs restart```
