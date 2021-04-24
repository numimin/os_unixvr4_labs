#!/bin/bash
for i in {1..510}
do
 ./client $1 $i > /dev/null &
done