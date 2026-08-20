#!/bin/bash
x=5
y=$((x + 3))
echo "sum: $y"
echo "pre: $((y * 2))"
#__EXPECT__
sum: 8
pre: 16
