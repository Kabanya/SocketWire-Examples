project(raylib-entity-eater)

set(ENTITY_EATER_CLIENT
  main.cpp
  protocol.cpp
  )

set(ENTITY_EATER_SERVER
  server.cpp
  protocol.cpp
  )

# set(W4_BITSTREAM_SOURCES
#   bitstream.h
#   bitstream.cpp
#   )

include_directories("../3rdParty/enet/include")

if(MSVC)
  # https://github.com/raysan5/raylib/issues/857
  add_compile_definitions(NOVIRTUALKEYCODES NOWINMESSAGES NOWINSTYLES NOSYSMETRICS NOMENUS NOICONS NOKEYSTATES NOSYSCOMMANDS NORASTEROPS NOSHOWWINDOW OEMRESOURCE NOATOM NOCLIPBOARD NOCOLOR NOCTLMGR NODRAWTEXT NOGDI NOKERNEL NOUSER NOMB NOMEMMGR NOMETAFILE NOMINMAX NOMSG NOOPENFILE NOSCROLL NOSERVICE NOSOUND NOTEXTMETRIC NOWH NOWINOFFSETS NOCOMM NOKANJI NOHELP NOPROFILER NODEFERWINDOWPOS NOMCX)
endif()

add_executable(entity-eater-client ${ENTITY_EATER_CLIENT})
target_link_libraries(entity-eater-client PUBLIC project_options project_warnings)
target_link_libraries(entity-eater-client PUBLIC raylib enet)

add_executable(entity-eater-server ${ENTITY_EATER_SERVER})
target_link_libraries(entity-eater-server PUBLIC project_options project_warnings)
target_link_libraries(entity-eater-server PUBLIC enet)

target_link_libraries(entity-eater-client PRIVATE SocketWire)
target_link_libraries(entity-eater-server PRIVATE SocketWire)
# target_link_libraries(test_client_socket PRIVATE SocketWire)
# target_link_libraries(test_server_socket PRIVATE SocketWire)

# add_executable(w4_bitstream ${W4_BITSTREAM_SOURCES})
# target_link_libraries(w4_bitstream PUBLIC project_options project_warnings)

# add_library(w4_bitstream ${W4_BITSTREAM_SOURCES})
# target_link_libraries(w4_bitstream PUBLIC project_options project_warnings)

if(MSVC)
  target_link_libraries(entity-eater-client PUBLIC ws2_32.lib winmm.lib)
  target_link_libraries(entity-eater-server PUBLIC ws2_32.lib winmm.lib)
endif()