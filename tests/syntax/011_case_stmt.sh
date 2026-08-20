#!/bin/bash
for x in 1 2 3; do
  case $x in
    1) echo "one" ;;
    2) echo "two" ;;
    *) echo "many" ;;
  esac
done
#__EXPECT__
one
two
many
