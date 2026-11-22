#!/usr/bin/env bash
export PATH=/ucrt64/bin:$PATH
cd scripts/windows
{
for t in occt_test2; do
g++ -O1 -std=gnu++17 -I/ucrt64/include/opencascade $t.cpp -o /tmp/$t.exe -lTKernel -lTKMath -lTKG3d -lTKGeomBase -lTKBRep -lTKTopAlgo -lTKPrim -lTKBO -lTKFillet -lTKG2d -lTKGeomAlgo && echo built
/tmp/$t.exe; echo "exit=$?"
done
} > ../../logs/occt_test.log 2>&1
