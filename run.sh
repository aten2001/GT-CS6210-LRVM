#!/bin/bash
for i in `ls testcases\\`; do
    echo $i
    g++ -I. -g "testcases/$i" librvm.a 
    ./a.out
    echo ""
done

