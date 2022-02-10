#!/bin/bash

set -x
set -e

PREFIX=/home/tomas/zdrojaky/cesnet/build/prefixes/clean-new/usr
export LD_LIBRARY_PATH=$PREFIX/lib

rm -fr /dev/shm/velia* /dev/shm/cla* /dev/shm/sr*
rm -fr /home/tomas/zdrojaky/cesnet/build/prefixes/_sysrepo/repository/*

pushd $1

$PREFIX/bin/sysrepoctl -v3 --search-dirs=$(pwd) -i ietf-alarms.yang -e alarm-shelving
$PREFIX/bin/sysrepoctl -v3 --search-dirs=$(pwd) -i czechlight-alarms.yang
$PREFIX/bin/sysrepoctl -v3 --search-dirs=$(pwd) -i czechlight-alarm-manager.yang
