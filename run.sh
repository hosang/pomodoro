#!/bin/bash
bazel run -c opt --cxxopt='-std=c++20' //:cprd
