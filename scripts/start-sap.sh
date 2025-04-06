#!/bin/bash

APAB_PLATFORM_IMAGE="sapse/abap-cloud-developer-trial:ABAPTRIAL_2022_SP01"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PROFILE_FILE="$SCRIPT_DIR/A4H_D00_vhcala4hci.profile"
LICENSE_FILE="${1:-$SCRIPT_DIR/A4H_Multiple.txt}"

docker run \
	--stop-timeout 3600 \
	-i \
	--rm \
	--name a4h \
	-h vhcala4hci \
	--mac-address="02:42:ac:11:00:02" \
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
	-v $LICENSE_FILE:/opt/sap/ASABAP_license \
	$APAB_PLATFORM_IMAGE -agree-to-sap-license -skip-limits-check
