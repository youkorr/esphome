# The OV02C10 driver, as an ESP-IDF component of its own beside
# espressif/esp_cam_sensor, which has no driver for it. ESPHome does not
# compile this directory as part of esp_video_camera (only the files at the
# top of a component are); esp_video_camera adds it with add_idf_component()
# when sensor_model: ov02c10 is given.
#
# Only the sources depend on Kconfig, as in esp_cam_sensor's own CMakeLists:
# ESP-IDF works out the requirements before it has the configuration.
set(srcs)
if(CONFIG_CAMERA_OV02C10)
    list(APPEND srcs "ov02c10.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES esp_cam_sensor
                       PRIV_REQUIRES esp_driver_gpio)

# esp_cam_sensor finds a driver through a linker section, and nothing refers
# to this one by name: without this the linker leaves ov02c10.o out of the
# archive and the sensor is never probed. esp_cam_sensor does the same for
# its own drivers.
if(CONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")
endif()
