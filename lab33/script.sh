#!/bin/bash
for i in {1..510}
do
 ./client $1 > /dev/null &
done