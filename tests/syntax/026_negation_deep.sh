#!/bin/bash
if ! false; then echo "n1"; fi
if ! true; then echo "WRONG"; else echo "n2"; fi
! true || echo "n3"
! false && echo "n4"
if !! true; then echo "n5"; fi
#__EXPECT__
n1
n2
n3
n4
n5
