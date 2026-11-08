file(REMOVE_RECURSE "${BUILD_DIR}/package-prefix" "${BUILD_DIR}/consumer")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}" --prefix "${BUILD_DIR}/package-prefix" RESULT_VARIABLE result)
if(result)
  message(FATAL_ERROR "Package installation failed: ${result}")
endif()
file(GLOB configs "${BUILD_DIR}/package-prefix/lib/cmake/LcarsUI/*.cmake")
foreach(config IN LISTS configs)
  file(READ "${config}" content)
  foreach(path IN ITEMS "${BUILD_DIR}" "${SOURCE_DIR}")
    string(FIND "${content}" "${path}" pos)
    if(NOT pos EQUAL -1)
      message(FATAL_ERROR "Absolute build/source path in ${config}")
    endif()
  endforeach()
endforeach()
file(GLOB_RECURSE actual RELATIVE "${BUILD_DIR}/package-prefix" "${BUILD_DIR}/package-prefix/*")
string(TOLOWER "${CONFIG}" config_lower)
set(expected
  include/lcars/appearance.hpp include/lcars/core.hpp include/lcars/presentation.hpp include/lcars/layout.hpp include/lcars/version.hpp
  lib/lcars_core.lib lib/lcars_appearance_web.lib lib/lcars_appearance_material.lib
  lib/cmake/LcarsUI/LcarsUIConfig.cmake lib/cmake/LcarsUI/LcarsUIConfigVersion.cmake
  lib/cmake/LcarsUI/LcarsUICoreTargets.cmake lib/cmake/LcarsUI/LcarsUICoreTargets-${config_lower}.cmake
  lib/cmake/LcarsUI/LcarsUIAppearanceWebTargets.cmake lib/cmake/LcarsUI/LcarsUIAppearanceWebTargets-${config_lower}.cmake
  lib/cmake/LcarsUI/LcarsUIAppearanceMaterialTargets.cmake lib/cmake/LcarsUI/LcarsUIAppearanceMaterialTargets-${config_lower}.cmake
  share/doc/LcarsUI/LICENSE)
if(CONFIG STREQUAL "Debug")
  list(TRANSFORM expected REPLACE "(lcars_(core|appearance_web|appearance_material))\\.lib$" "\\1d.lib")
endif()
list(SORT actual)
list(SORT expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "Unexpected installed files: ${actual}; expected ${expected}")
endif()
