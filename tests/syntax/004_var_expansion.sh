#!/bin/bash
name="world"
echo "hello $name"
echo "braced ${name}!"
#__EXPECT__
hello world
braced world!
