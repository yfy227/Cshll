#!/bin/bash
(echo "bg ran") &
wait
echo "after bg"
#__EXPECT__
bg ran
after bg
