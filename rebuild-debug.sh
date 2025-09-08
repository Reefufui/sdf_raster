rm -rf debug-build
mkdir debug-build
cd debug-build
export CXX="/usr/local/bin/g++-15" 
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j 12
ln -s ../../SdfTaskTemplate/example_grid.grid
cd ..
