#!/bin/bash
x="outer"
(
  x="inner"
  echo "sub: $x"
)
echo "main: $x"
#__EXPECT__
sub: inner
main: outer
