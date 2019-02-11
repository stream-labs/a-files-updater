# Integration tests

## How?

```
mkdir build
cd build 
cmake -G "Visual Studio 15 2017 Win64" ../
cmake --build . --target ALL_BUILD --config Debug
cd ..\test
yarn install 
node src\run_tests.js
```

## What?

This test prepare test env and run debug build of updater to test what it can handle sommon usecases. 
* It crates 3 folder and generate some files in each folder such as it represent 1 some slobs instalation 2 some update on the server 3 what result should be. 
* Then it start local https server what will emulate update server. 
* And start updater with option that it will use folder 1 and local server.
* After updater finishes then test compare folder 1 with folder 3 to check if all files was updated as expected.  

It also test `failed usecases` in which something block/interupt update. And it that case content of folder 1 should not be changed. 