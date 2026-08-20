#!/bin/bash
x=out
(
  x=in
  (
    echo "nested: $x"
    x=in2
  )
  echo "mid: $x"
)
echo "outer: $x"
( cd / && echo "cwd-locked: ok" )
#__EXPECT__
nested: in
mid: in
outer: out
cwd-locked: ok
