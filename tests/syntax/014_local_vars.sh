#!/bin/bash
outer="global"
setit() {
  local outer="inner"
  echo "inside: $outer"
}
setit
echo "outside: $outer"
#__EXPECT__
inside: inner
outside: global
