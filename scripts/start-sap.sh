#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PROFILE_FILE="$SCRIPT_DIR/A4H_D00_vhcala4hci.profile"

docker run \
	--stop-timeout 3600 \
	-i \
	--rm \
	--name a4h \
	-h vhcala4hci \
	-p 3200:3200 \
	-p 3300:3300 \
	-p 8443:8443 \
	-p 30213:30213 \
	-p 50000:50000 \
	-p 50001:50001 \
	--sysctl kernel.shmmax=42949672960 \
	--sysctl kernel.shmmni=32768 \
	--sysctl kernel.shmall=10485760 \
	--sysctl kernel.msgmni=1024 \
	--sysctl kernel.sem="1250 256000 100 8192" \
	--ulimit nofile=1048576:1048576 \
	-v $PROFILE_FILE:/usr/sap/A4H/SYS/profile/A4H_D00_vhcala4hci \
	sapse/abap-platform-trial:1909 -agree-to-sap-license
