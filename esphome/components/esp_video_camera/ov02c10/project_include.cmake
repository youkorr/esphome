# The colour tuning esp_ipa compiles in for this sensor, the way
# esp_cam_sensor's own project_include.cmake does it for its drivers.
if(CONFIG_CAMERA_OV02C10)
    if(CONFIG_CAMERA_OV02C10_DEFAULT_IPA_JSON_CONFIGURATION_FILE)
        idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH "${CMAKE_CURRENT_LIST_DIR}/cfg/ov02c10_default.json" APPEND)
    elseif(CONFIG_CAMERA_OV02C10_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE)
        idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH ${CONFIG_CAMERA_OV02C10_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH} APPEND)
    endif()
endif()
