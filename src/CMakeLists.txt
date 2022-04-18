file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_executable(trrntzip ${SOURCES})
target_link_libraries(trrntzip m ZLIB::ZLIB)
install(TARGETS trrntzip EXPORT ${PROJECT_NAME}-targets DESTINATION bin)
