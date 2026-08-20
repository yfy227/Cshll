#!/bin/bash
n=0
while [ $n -lt 3 ]; do
  echo "n=$n"
  n=$((n + 1))
done
#__EXPECT__
n=0
n=1
n=2
