project(test)
cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
find_package(doctest REQUIRED)

pkg_check_modules(SYSREPO REQUIRED sysrepo-cpp>=1.0.0 IMPORTED_TARGET sysrepo)
pkg_check_modules(LIBYANG REQUIRED libyang-cpp>=1.0.0 IMPORTED_TARGET libyang)

#add_executable(update update.cpp)
add_executable(test   test.cpp)
add_executable(test2  test2.cpp)

#target_link_libraries(update PUBLIC PkgConfig::SYSREPO PkgConfig::LIBYANG PRIVATE doctest::doctest)
target_link_libraries(test   PUBLIC PkgConfig::SYSREPO PkgConfig::LIBYANG PRIVATE doctest::doctest)
target_link_libraries(test2  PUBLIC PkgConfig::SYSREPO PkgConfig::LIBYANG PRIVATE doctest::doctest)

add_custom_target(restart ${CMAKE_SOURCE_DIR}/clean.sh ${CMAKE_SOURCE_DIR})
