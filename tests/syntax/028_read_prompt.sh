#!/bin/bash
read -p "Name: " n
read -rp "Age: " a
read -p "Plain: "
read -s -p "PW: " pw
echo "got: $n $a REPLY=$REPLY pwlen=${#pw}"
#__EXPECT__
got:   REPLY= pwlen=0
