#!/bin/bash
greet() {
  echo "hello, $1"
}
greet "alice"
greet "bob"
#__EXPECT__
hello, alice
hello, bob
