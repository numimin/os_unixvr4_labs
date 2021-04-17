#!/bin/bash

for i in {client,server,proxy}
do
gcc -o "$i" -std=gnu99 "lab33-$i.c"
done