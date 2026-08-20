#!/bin/bash
s="hello world"
echo "len: ${#s}"
echo "upper first: ${s^}"
echo "sub: ${s:0:5}"
echo "rep: ${s/world/shell}"
#__EXPECT__
len: 11
upper first: Hello world
sub: hello
rep: hello shell
