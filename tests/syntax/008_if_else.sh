#!/bin/bash
if [ 1 -eq 1 ]; then
  echo "eq"
else
  echo "ne"
fi
if [ 1 -eq 2 ]; then
  echo "wrong"
elif [ 2 -eq 2 ]; then
  echo "elif hit"
fi
#__EXPECT__
eq
elif hit
