# OBS Asynchronous Audio Source Plugin

## Introduction

This plugin provides a asynchronous audio source to debug audio buffering issues.

I hope this plugin is useful to reproduce and debug the issue.
https://github.com/obsproject/obs-studio/issues/4600

## Properties

### Frequency L, R

The frequency of a sine wave output by this plugin.

### Skew

Make audio data a little slower or faster if set to negative or positive, respectively.

## Build and install
### Linux
Use cmake to build on Linux. After checkout, run these commands.
```
sed -i 's;${CMAKE_INSTALL_FULL_LIBDIR};/usr/lib;' CMakeLists.txt
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make
sudo make install
```

### macOS
Use cmake to build on Linux. After checkout, run these commands.
```
mkdir build && cd build
cmake ..
make
```
