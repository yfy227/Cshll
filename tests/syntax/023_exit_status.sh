#!/bin/bash
true
echo "rc0: $?"
false
echo "rc1: $?"
#__EXPECT__
rc0: 0
rc1: 1
