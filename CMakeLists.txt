cmake_minimum_required (VERSION 2.6)
project (xtag)

include_directories ("./libutil")
include_directories ("./libdb/")
include_directories ("./libparser/")
include_directories ("./action/")

set (CMAKE_C_FLAGS ${CMAKE_C_FLAGS} -DHAVE_CONFIG_H)
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBINDIR='\"/usr/local/bin\"'")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDATADIR='\"/usr/local/share\"'")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLOCALSTATEDIR='\"/usr/local/var\"'")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSYSCONFDIR='\"/usr/local/etc\"'")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

FILE (GLOB SRC ./libutil/*.[ch] ./libparser/*.[ch] ./libdb/*.[ch]
   ./action/*.c ./*.c ./*.h)
add_executable (xtag ${SRC})

# FILE (GLOB SRC ./libutil/*.[ch] ./libparser/*.[ch] ./libdb/*.[ch] 
#     ./action/*.c ./global/*.c)
# add_executable (global ${SRC})

