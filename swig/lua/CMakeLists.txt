add_definitions(-DSWIG)
add_definitions(-DSWIGLUA)

project(Sysrepo_Lua)

add_library(Sysrepo_Lua SHARED ${SYSREPO_CPP_SOURCES})

# include custom Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Sysrepo_Lua sysrepo)

if(${GEN_LUA_VERSION} STREQUAL "5.1")
    set(LUA_SWIG_BINDING libsysrepoLua51)
elseif(${GEN_LUA_VERSION} STREQUAL "5.2")
    set(LUA_SWIG_BINDING libsysrepoLua52)
elseif(${GEN_LUA_VERSION} STREQUAL "5.3")
    set(LUA_SWIG_BINDING libsysrepoLua53)
endif()

include(${SWIG_USE_FILE})
include_directories(${LUA_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_SWIG_FLAGS "-c++")
set(CMAKE_SWIG_OUTDIR ${CMAKE_BINARY_DIR})
set(CMAKE_SWIG_FLAGS "-I${PROJECT_SOURCE_DIR}/inc")
set(CMAKE_SWIG_OUTDIR ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(${LUA_SWIG_BINDING}.i PROPERTIES CPLUSPLUS ON)

file(COPY "examples" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

swig_add_module(${LUA_SWIG_BINDING} lua ${LUA_SWIG_BINDING}.i)
swig_link_libraries(${LUA_SWIG_BINDING} ${LUA_LIBRARIES} Sysrepo_Lua)

# install binary
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${LUA_SWIG_BINDING}.so" DESTINATION "${CMAKE_INSTALL_LIBDIR}/lua/${GEN_LUA_VERSION}/")
install(TARGETS Sysrepo_Lua DESTINATION ${CMAKE_INSTALL_LIBDIR})