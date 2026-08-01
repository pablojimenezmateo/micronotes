# CMake generated Testfile for 
# Source directory: /home/gef/Documents/projects/micronotes
# Build directory: /home/gef/Documents/projects/micronotes/build-asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[micronotes_tests]=] "/home/gef/Documents/projects/micronotes/build-asan/bin/micronotes_tests")
set_tests_properties([=[micronotes_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gef/Documents/projects/micronotes/CMakeLists.txt;206;add_test;/home/gef/Documents/projects/micronotes/CMakeLists.txt;0;")
add_test([=[micronotes_core_manifest]=] "/home/gef/Documents/projects/micronotes/tools/sync-core.sh" "--check")
set_tests_properties([=[micronotes_core_manifest]=] PROPERTIES  WORKING_DIRECTORY "/home/gef/Documents/projects/micronotes" _BACKTRACE_TRIPLES "/home/gef/Documents/projects/micronotes/CMakeLists.txt;211;add_test;/home/gef/Documents/projects/micronotes/CMakeLists.txt;0;")
