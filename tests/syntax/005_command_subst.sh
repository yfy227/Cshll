#!/bin/bash
echo "today: $(echo monday)"
files=$(echo a b c)
echo "got: $files"
#__EXPECT__
today: monday
got: a b c
