#!/bin/bash

gcc -o client -std=gnu99 lab33-client.c
gcc -o proxy -std=gnu99 lab33-proxy.c socket_utils.c server_management.c iobuffer.c
gcc -o server -std=gnu99 lab33-server.c socket_utils.c usual_server_management.c