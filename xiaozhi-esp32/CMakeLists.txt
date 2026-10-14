# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.5.0")
project(xiaozhi)

# These two components already link prebuilt archives by their full path.
# Their redundant unquoted -L fragments break Windows workspace paths with spaces.
if(CMAKE_HOST_WIN32 AND CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO)
    foreach(codec_target __idf_espressif__esp_audio_codec __idf_espressif__esp_image_effects)
        foreach(link_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(codec_links ${codec_target} ${link_property})
            if(codec_links)
                set(safe_codec_links "")
                foreach(codec_link IN LISTS codec_links)
                    if(NOT codec_link MATCHES "^-L " AND NOT codec_link MATCHES "^\\$<LINK_ONLY:-L ")
                        list(APPEND safe_codec_links "${codec_link}")
                    endif()
                endforeach()
                set_property(TARGET ${codec_target} PROPERTY ${link_property} "${safe_codec_links}")
            endif()
        endforeach()
    endforeach()
endif()
