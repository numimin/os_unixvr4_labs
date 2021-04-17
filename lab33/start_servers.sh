#!/bin/bash

listen_port=$1
dest_port=$2

./proxy $listen_port 127.0.0.1 $dest_port &
./server $dest_port > /dev/null &
