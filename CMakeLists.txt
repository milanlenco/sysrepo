cmake_minimum_required(VERSION 2.8)
project(sysrepo)
set(SYSREPO_DESC "YANG-based system repository")
include(GNUInstallDirs)

# setup version
set(SYSREPO_MAJOR_VERSION 0)
set(SYSREPO_MINOR_VERSION 5)
set(SYSREPO_MICRO_VERSION 11)
set(SYSREPO_VERSION ${SYSREPO_MAJOR_VERSION}.${SYSREPO_MINOR_VERSION}.${SYSREPO_MICRO_VERSION})

# setup bindings
set(GEN_LANGUAGE_BINDINGS 1 CACHE BOOL "Enable language bindings generation.")
set(GEN_PYTHON_BINDINGS 1 CACHE BOOL "Enable python bindings.")
set(GEN_LUA_BINDINGS 1 CACHE BOOL "Enable Lua bindings.")
set(GEN_CPP_BINDINGS 1 CACHE BOOL "Enable C++ bindings.")
set(BUILD_CPP_EXAMPLES 1 CACHE BOOL "Build C++ examples.")
set(GEN_JAVA_BINDINGS 0 CACHE BOOL "Enable Java bindings.")
set(GEN_PYTHON_VERSION "2" CACHE STRING "Python version")
set(GEN_LUA_VERSION "5.1" CACHE STRING "Lua version")
set(GEN_PYTHON2_TESTS 1 CACHE BOOL "Enable Python2 tests.")

# set default build type if not specified by user
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE debug)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_C_FLAGS         "${CMAKE_C_FLAGS} -Wall -Wpedantic -std=gnu11")
if(CMAKE_COMPILER_IS_GNUCC)
    # disable strict aliasing in GCC, since it produces false alarams in libev
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-strict-aliasing")
endif()
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2")
set(CMAKE_C_FLAGS_DEBUG   "-g -O0")

if(NOT UNIX)
    message(FATAL_ERROR "Only Unix-like systems are supported.")
endif()

set(PLUGINS_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/sysrepo/plugins/" CACHE PATH "Sysrepo plugins directory.")

if(CMAKE_BUILD_TYPE_LOWER MATCHES "debug" AND NOT CMAKE_BUILD_TYPE_LOWER MATCHES "^rel")
    set(IS_DEBUG_BUILD true)
else()
    set(IS_DEBUG_BUILD false)
endif()

# set build-type specific settings
if(${IS_DEBUG_BUILD})
    MESSAGE(STATUS "Preparing debug build of sysrepo v. ${SYSREPO_VERSION}")
    set(DAEMON_PID_FILE "/tmp/sysrepod.pid" CACHE PATH "Sysrepo daemon PID file.")
    set(DAEMON_SOCKET "/tmp/sysrepod.sock" CACHE PATH "Sysrepo deamon server socket path.")
    set(PLUGIN_DAEMON_PID_FILE "/tmp/sysrepo-plugind.pid" CACHE PATH "Sysrepo plugin daemon PID file.")
    set(SUBSCRIPTIONS_SOCKET_DIR "/tmp/sysrepo-subscriptions" CACHE PATH "Sysrepo subscriptions socket directory.")
else()
    MESSAGE(STATUS "Preparing release build of sysrepo v. ${SYSREPO_VERSION}")
    set(DAEMON_PID_FILE "/var/run/sysrepod.pid" CACHE PATH "Sysrepo daemon PID file.")
    set(DAEMON_SOCKET "/var/run/sysrepod.sock" CACHE PATH "Sysrepo deamon server socket path.")
    set(PLUGIN_DAEMON_PID_FILE "/var/run/sysrepo-plugind.pid" CACHE PATH "Sysrepo plugin daemon PID file.")
    set(SUBSCRIPTIONS_SOCKET_DIR "/var/run/sysrepo-subscriptions" CACHE PATH "Sysrepo subscriptions socket directory.")
endif()

# location of system repository
if(${IS_DEBUG_BUILD})
    set(REPOSITORY_LOC "${CMAKE_BINARY_DIR}/repository" CACHE PATH "System repository location, contains configuration schema and data files.")
else()
    set(REPOSITORY_LOC "/etc/sysrepo" CACHE PATH "System repository location, contains configuration schema and data files.")
endif()
set(SCHEMA_SEARCH_DIR "${REPOSITORY_LOC}/yang/")
set(DATA_SEARCH_DIR "${REPOSITORY_LOC}/data/")
set(INTERNAL_SCHEMA_SEARCH_DIR "${REPOSITORY_LOC}/yang/internal/")
set(INTERNAL_DATA_SEARCH_DIR "${REPOSITORY_LOC}/data/internal/")
set(NOTIF_DATA_SEARCH_DIR "${REPOSITORY_LOC}/data/notifications/")
MESSAGE(STATUS "sysrepo repository location: ${REPOSITORY_LOC}")

# include custom Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/inc")

# find required libraries
find_package(EV REQUIRED)
include_directories(${EV_INCLUDE_DIR})

find_package(YANG REQUIRED)
include_directories(${YANG_INCLUDE_DIR})

find_package(Protobuf-c REQUIRED)
include_directories(${PROTOBUF-C_INCLUDE_DIR})

# find libavl and/or libredblack
find_package(AVL)
find_package(RedBlack)
if((NOT AVL_FOUND) AND (NOT REDBLACK_FOUND))
    MESSAGE(WARNING "libavl or libredblack must be installed.")
endif()

if(NOT DEFINED USE_AVL_LIB)
    # no preference configured, try to find libavl and then libredblack
    if(AVL_FOUND)
        SET(USE_AVL_LIB 1 CACHE BOOL "Use libavl (1) or libredblack (0) for binary tree manipulations.")
    else(AVL_FOUND)
        if(REDBLACK_FOUND)
            SET(USE_AVL_LIB 0 CACHE BOOL "Use libavl (1) or libredblack (0) for binary tree manipulations.")
        endif(REDBLACK_FOUND)
    endif(AVL_FOUND)
endif(NOT DEFINED USE_AVL_LIB)
if(USE_AVL_LIB)
    if(AVL_FOUND)
        MESSAGE(STATUS "libavl will be used for binary tree manipulations.")
        include_directories(${AVL_INCLUDE_DIR})
    else(AVL_FOUND)
         MESSAGE(WARNING "libavl cannot be found.")
    endif(AVL_FOUND)
else(USE_AVL_LIB)
    if(REDBLACK_FOUND)
        MESSAGE(STATUS "libredblack will be used for binary tree manipulations.")
        include_directories(${REDBLACK_INCLUDE_DIR})
    else(REDBLACK_FOUND)
         MESSAGE(WARNING "libredblack cannot be found.")
    endif(REDBLACK_FOUND)
endif(USE_AVL_LIB)

# check for non-portable functions and headers
set(CMAKE_REQUIRED_LIBRARIES pthread)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckStructHasMember)
CHECK_FUNCTION_EXISTS(pthread_rwlockattr_setkind_np HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP)
CHECK_FUNCTION_EXISTS(getpeereid HAVE_GETPEEREID)
CHECK_FUNCTION_EXISTS(getpeerucred HAVE_GETPEERUCRED)
CHECK_INCLUDE_FILES(ucred.h HAVE_UCRED_H)
CHECK_FUNCTION_EXISTS(pthread_mutex_timedlock HAVE_TIMED_LOCK)
CHECK_FUNCTION_EXISTS(setfsuid HAVE_SETFSUID)
CHECK_FUNCTION_EXISTS(fsetxattr HAVE_FSETXATTR)
CHECK_STRUCT_HAS_MEMBER("struct stat" st_mtim "sys/stat.h" HAVE_STAT_ST_MTIM)

# user options
set(ENABLE_NACM 1 CACHE BOOL
    "Enable NETCONF Access Control Model (RFC 6536).")

set(NACM_RECOVERY_UID 0 CACHE INTEGER
    "UID to be used to identify NACM recovery session, default value is 0.")

set(ENABLE_NOTIF_STORE 1 CACHE BOOL
    "Enable event notifications store & notifications replay.")

set(USE_SR_MEM_MGMT 1 CACHE BOOL
    "Use Sysrepo's own memory management (better overall performance but more difficult to track memory bugs).")

set(LOG_THREAD_ID 0 CACHE BOOL
    "If enabled, sysrepo logger will append thread ID (as well as function name) to each printed message.")

set(ENABLE_CONFIG_CHANGE_NOTIF 1 CACHE BOOL
    "Generate config-change notifications (RFC 6470).")

#TODO: enable storing config change once instance-id validation is done
set(STORE_CONFIG_CHANGE_NOTIF 0 CACHE BOOL
    "Save config-change notifications (RFC 6470) in the notification store (slows down the commit process).")

# timeouts
set(REQUEST_TIMEOUT 3 CACHE INTEGER
    "Timeout (in seconds) for standard Sysrepo API requests.")

set(LONG_REQUEST_TIMEOUT 15 CACHE INTEGER
    "Timeout (in seconds) for Sysrepo API requests that can take longer than standard requests (commit, copy-config, rpc, action).")

set(COMMIT_VERIFY_TIMEOUT 10 CACHE INTEGER
    "Timeout (in seconds) that a commit request can wait for answer from commit verifiers and change notification subscribers.")

set(OPER_DATA_PROVIDE_TIMEOUT 2 CACHE INTEGER
    "Timeout (in seconds) that a request can wait for operational data from data providers.")

set(NOTIF_AGE_TIMEOUT 60 CACHE INTEGER
    "Timeout (in minutes) after which stored notifications will be aged out and erased from notification store.")

set(NOTIF_TIME_WINDOW 10 CACHE INTEGER
    "Time window (in minutes) for notifications to be grouped into one data file (larger window produces larger data files).")

# add subdirectories
add_subdirectory(src)

# execute command at the install time
macro(EXEC_AT_INSTALL_TIME CMD)
    install(CODE "message(STATUS \"Exec: ${CMD}\")
        execute_process(COMMAND ${CMD} OUTPUT_QUIET RESULT_VARIABLE ret)
        if (NOT \${ret} EQUAL 0)
          message(FATAL_ERROR \"Error: \${ret}\")
        endif()"
        )
endmacro(EXEC_AT_INSTALL_TIME)

macro(INSTALL_YANG MODULE_NAME REVISION PERMISSIONS)
    EXEC_AT_INSTALL_TIME("${CMAKE_BINARY_DIR}/src/sysrepoctl --install --yang=${CMAKE_CURRENT_SOURCE_DIR}/yang/${MODULE_NAME}${REVISION}.yang --permissions=${PERMISSIONS}")
endmacro(INSTALL_YANG)

# Examples
SET(BUILD_EXAMPLES 1 CACHE BOOL "Build examples and install example YANG models.")
if(BUILD_EXAMPLES)
    MESSAGE(STATUS "Example code and YANG models will be built and installed.")
    add_subdirectory(examples)
endif()

# Testing
SET(ENABLE_TESTS 1 CACHE BOOL "Enable unit tests.")
if(ENABLE_TESTS)
    find_package(CMOCKA)
    if(CMOCKA_FOUND)
        MESSAGE(STATUS "CMocka found, tests are enabled.")
        enable_testing()
        add_subdirectory(tests)
    else(CMOCKA_FOUND)
        MESSAGE(WARNING "CMocka not found, tests are disabled.")
    endif(CMOCKA_FOUND)
endif(ENABLE_TESTS)

# Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif(DOXYGEN_FOUND)
if(NOT MAN_INSTALL_DIR)
    set(MAN_INSTALL_DIR share/man)
endif()

# install repository directories
install (DIRECTORY DESTINATION ${REPOSITORY_LOC} DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
install (DIRECTORY DESTINATION ${SCHEMA_SEARCH_DIR} DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
install (DIRECTORY DESTINATION ${DATA_SEARCH_DIR} DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
install (DIRECTORY DESTINATION ${INTERNAL_DATA_SEARCH_DIR} DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

# install subscriptions socket directory
install (DIRECTORY DESTINATION ${SUBSCRIPTIONS_SOCKET_DIR} DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_WRITE GROUP_EXECUTE
    WORLD_READ WORLD_WRITE WORLD_EXECUTE)

# install plugins directory
install(DIRECTORY DESTINATION ${PLUGINS_DIR})

# install internal YANGs
set(INTERNAL_YANGS
    ${PROJECT_SOURCE_DIR}/yang/sysrepo-persistent-data.yang
    ${PROJECT_SOURCE_DIR}/yang/sysrepo-module-dependencies.yang
    ${PROJECT_SOURCE_DIR}/yang/sysrepo-notification-store.yang
)
install (FILES ${INTERNAL_YANGS} DESTINATION ${INTERNAL_SCHEMA_SEARCH_DIR})

# install NACM YANG module
if(ENABLE_NACM)
    INSTALL_YANG("ietf-netconf-acm" "@2012-02-22" "644")
endif(ENABLE_NACM)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    # generate and install pkg-config file
    configure_file("libsysrepo.pc.in" "libsysrepo.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libsysrepo.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

# install doc (man)
install(FILES ${PROJECT_SOURCE_DIR}/doc/sysrepoctl.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES ${PROJECT_SOURCE_DIR}/doc/sysrepocfg.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(WITH_SYSTEMD)
    # systemd units
    add_custom_target(systemd-units SOURCES
        ${PROJECT_SOURCE_DIR}/deploy/systemd/sysrepod.service
        ${PROJECT_SOURCE_DIR}/deploy/systemd/sysrepo-plugind.service
    )
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/deploy/systemd/ DESTINATION "/lib/systemd/system"
        FILES_MATCHING PATTERN "*.service")
endif()

INSTALL_YANG("ietf-netconf-notifications" "" "666")
INSTALL_YANG("nc-notifications" "" "666")

if(GEN_LANGUAGE_BINDINGS)
    add_subdirectory(swig)
endif()
