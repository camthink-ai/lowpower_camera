set(src "mmosal_shim_freertos_esp32.c"
        "mmhal.c"
        "wlan_hal.c")
set(inc "include")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib spi_flash app_update log driver
                       WHOLE_ARCHIVE)

target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mm6108.mbin.o")

# Kconfig variables are used to determine which bcf file to link against
if(CONFIG_MM_BCF_MF08651)
    message(STATUS "Using BCF for MM6108_MF08651")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08651.mbin.o")
elseif(CONFIG_MM_BCF_MF08551)
    message(STATUS "Using BCF for MM6108_MF08551")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08551.mbin.o")
elseif(CONFIG_MM_BCF_MF08251)
    message(STATUS "Using BCF for MM6108_MF08251")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08251.mbin.o")
elseif(CONFIG_MM_BCF_MF03120)
    message(STATUS "Using BCF for MM6108_MF03120")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf03120.mbin.o")
elseif(CONFIG_MM_BCF_MF08251_CE)
    message(STATUS "Using BCF for MM6108_MF08251_CE")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08251_ce.mbin.o")
elseif(CONFIG_MM_BCF_MF08251_FCC)
    message(STATUS "Using BCF for MM6108_MF08251_FCC")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08251_fcc.mbin.o")
else()
    message(FATAL_ERROR "No BCF specified for mm_shims")
endif()
