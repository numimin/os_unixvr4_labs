#!/bin/bash
for i in {1..100}
do
 ../lab33/client $1 $i > /dev/null &
done
