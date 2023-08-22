#!/bin/bash

docker run --stop-timeout 3600 -i --rm --name a4h -h vhcala4hci -p 3200:3200 -p 3300:3300 -p 8443:8443 -p 30213:30213 -p 50000:50000 -p 50001:50001 sapse/abap-platform-trial:1909 -skip-limits-check -agree-to-sap-license
