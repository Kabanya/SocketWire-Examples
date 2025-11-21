project(raylib-examples)

set(SIMPLE_GAME
  game.cpp
  )

set(EARN_GOLD_CLIENT
  client.cpp
  protocol.cpp
  )

set(EARN_GOLD_SERVER
  server.cpp
  protocol.cpp
  )

if(MSVC)
  # https://github.com/raysan5/raylib/issues/857
  add_compile_definitions(NOVIRTUALKEYCODES NOWINMESSAGES NOWINSTYLES NOSYSMETRICS NOMENUS NOICONS NOKEYSTATES NOSYSCOMMANDS NORASTEROPS NOSHOWWINDOW OEMRESOURCE NOATOM NOCLIPBOARD NOCOLOR NOCTLMGR NODRAWTEXT NOGDI NOKERNEL NOUSER NOMB NOMEMMGR NOMETAFILE NOMINMAX NOMSG NOOPENFILE NOSCROLL NOSERVICE NOSOUND NOTEXTMETRIC NOWH NOWINOFFSETS NOCOMM NOKANJI NOHELP NOPROFILER NODEFERWINDOWPOS NOMCX)
endif()

add_executable(simple-game ${SIMPLE_GAME})
target_link_libraries(simple-game PUBLIC project_options project_warnings)
target_link_libraries(simple-game PUBLIC raylib)
target_include_directories(simple-game PUBLIC ${raylib_SOURCE_DIR}/include)

add_executable(earn-gold-client ${EARN_GOLD_CLIENT})
target_link_libraries(earn-gold-client PUBLIC project_options project_warnings)
target_link_libraries(earn-gold-client PUBLIC raylib)
target_include_directories(earn-gold-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(earn-gold-client PUBLIC ${PROJECT_SOURCE_DIR}/../../common)
target_link_libraries(earn-gold-client PRIVATE SocketWire)

add_executable(earn-gold-server ${EARN_GOLD_SERVER})
target_link_libraries(earn-gold-server PUBLIC project_options project_warnings)
target_include_directories(earn-gold-server PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(earn-gold-server PUBLIC ${PROJECT_SOURCE_DIR}/../../common)
target_link_libraries(earn-gold-server PRIVATE SocketWire)

if(MSVC)
  target_link_libraries(simple-game PUBLIC ws2_32.lib winmm.lib)
  target_link_libraries(earn-gold-client PUBLIC ws2_32.lib winmm.lib)
  target_link_libraries(earn-gold-server PUBLIC ws2_32.lib winmm.lib)
endif()
