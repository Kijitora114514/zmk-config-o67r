if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  set(O67R_SWAPPED_DISP_C ${CMAKE_CURRENT_BINARY_DIR}/o67r_disp_swapped.c)

  add_custom_command(
    OUTPUT ${O67R_SWAPPED_DISP_C}
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_LIST_DIR}/src/swapper.py
      ${CMAKE_CURRENT_LIST_DIR}/src/disp.c
      ${O67R_SWAPPED_DISP_C}
    DEPENDS
      ${CMAKE_CURRENT_LIST_DIR}/src/disp.c
      ${CMAKE_CURRENT_LIST_DIR}/src/swapper.py
    VERBATIM
  )

  target_sources(app PRIVATE
    src/o67r_layer_status_screen.c
    ${O67R_SWAPPED_DISP_C}
  )
endif()
