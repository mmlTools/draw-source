add_executable(drawing-tests drawing_tests.cpp ../src/drawing_document.cpp)
target_include_directories(drawing-tests PRIVATE ../src)
target_link_libraries(drawing-tests PRIVATE Qt6::Gui)
target_compile_features(drawing-tests PRIVATE cxx_std_17)
add_test(NAME drawing-document COMMAND drawing-tests)
if(WIN32)
  set_tests_properties(
    drawing-document
    PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )
endif()
