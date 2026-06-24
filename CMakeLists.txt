if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # Keep src/disp.c as the maintainable source and generate the display-ready
  # byte-swapped image source during the GitHub Actions/ZMK CMake build.
  set(O67R_SWAPPED_DISP_C ${CMAKE_CURRENT_BINARY_DIR}/o67r_disp_swapped.c)

  execute_process(
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_LIST_DIR}/src/swapper.py
      ${CMAKE_CURRENT_LIST_DIR}/src/disp.c
      ${O67R_SWAPPED_DISP_C}
    RESULT_VARIABLE O67R_SWAPPER_RESULT
  )

  if(NOT O67R_SWAPPER_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate ${O67R_SWAPPED_DISP_C}")
  endif()

  target_sources(app PRIVATE
    src/o67r_layer_status_screen.c
    ${O67R_SWAPPED_DISP_C}
  )
endif()
