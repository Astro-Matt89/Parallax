# CMake generated Testfile for 
# Source directory: C:/Parallax-dev/Parallax/tests
# Build directory: C:/Parallax-dev/Parallax/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[TimeSystem]=] "C:/Parallax-dev/Parallax/build/bin/Debug/test_time_system.exe")
  set_tests_properties([=[TimeSystem]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Parallax-dev/Parallax/tests/CMakeLists.txt;24;add_test;C:/Parallax-dev/Parallax/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[TimeSystem]=] "C:/Parallax-dev/Parallax/build/bin/Release/test_time_system.exe")
  set_tests_properties([=[TimeSystem]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Parallax-dev/Parallax/tests/CMakeLists.txt;24;add_test;C:/Parallax-dev/Parallax/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[TimeSystem]=] "C:/Parallax-dev/Parallax/build/bin/MinSizeRel/test_time_system.exe")
  set_tests_properties([=[TimeSystem]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Parallax-dev/Parallax/tests/CMakeLists.txt;24;add_test;C:/Parallax-dev/Parallax/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[TimeSystem]=] "C:/Parallax-dev/Parallax/build/bin/RelWithDebInfo/test_time_system.exe")
  set_tests_properties([=[TimeSystem]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Parallax-dev/Parallax/tests/CMakeLists.txt;24;add_test;C:/Parallax-dev/Parallax/tests/CMakeLists.txt;0;")
else()
  add_test([=[TimeSystem]=] NOT_AVAILABLE)
endif()
