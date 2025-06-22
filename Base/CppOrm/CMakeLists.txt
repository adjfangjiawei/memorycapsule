file(GLOB_RECURSE CPPORM_SOURCES
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(CppOrm ${CPPORM_SOURCES})
target_include_directories(CppOrm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_link_libraries(CppOrm PUBLIC Qt6::Core DataBaseDriver)

add_subdirectory(Example)