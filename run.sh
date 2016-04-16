#!/bin/bash
for i in `ls testcases/bin\\`; do
    echo $i
    "./testcases/bin/$i"
    echo ""
done

