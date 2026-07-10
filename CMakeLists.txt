if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # Keep src/disp*.c as maintainable sources and generate display-ready
  # byte-swapped image sources during the GitHub Actions/ZMK CMake build.
  set(O67R_SWAPPED_DISP_SOURCES)

  foreach(O67R_DISP_INDEX RANGE 1 3)
    set(O67R_SWAPPED_DISP_C ${CMAKE_CURRENT_BINARY_DIR}/o67r_disp${O67R_DISP_INDEX}_swapped.c)

    execute_process(
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/src/swapper.py
        ${CMAKE_CURRENT_LIST_DIR}/src/disp${O67R_DISP_INDEX}.c
        ${O67R_SWAPPED_DISP_C}
      RESULT_VARIABLE O67R_SWAPPER_RESULT
    )

    if(NOT O67R_SWAPPER_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to generate ${O67R_SWAPPED_DISP_C}")
    endif()

    list(APPEND O67R_SWAPPED_DISP_SOURCES ${O67R_SWAPPED_DISP_C})
  endforeach()

  target_sources(app PRIVATE
    src/o67_deck.c
    ${O67R_SWAPPED_DISP_SOURCES}
  )
endif()

target_sources(app PRIVATE
  src/o67r_task_move.c
)
