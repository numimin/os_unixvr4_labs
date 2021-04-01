#!/bin/bash

for i in {1..10000} 
do
    kill -s SIGINT $1
done
kill -s SIGQUIT $1