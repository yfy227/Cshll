#!/bin/bash
arr=(one two three)
echo "first: ${arr[0]}"
echo "count: ${#arr[@]}"
echo "all: ${arr[*]}"
#__EXPECT__
first: one
count: 3
all: one two three
