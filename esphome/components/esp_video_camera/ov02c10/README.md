# OV02C10 driver for esp_cam_sensor

Espressif's esp_cam_sensor does not carry an OV02C10 driver, so ESPHome's
`esp_video_camera` carries this one. It is an ESP-IDF component of its own:
ESPHome does not compile it as part of `esp_video_camera`, which adds it to the
build with `add_idf_component()` only when the YAML says
`sensor_model: ov02c10`. It then finds the sensor the way esp_cam_sensor finds
its own: through the `.esp_cam_sensor_detect_fn` section, kept in by
`-u ov02c10_detect`.

- `ov02c10.c`, `include/`, `private_include/ov02c10_regs.h`,
  `private_include/ov02c10_settings.h` and `cfg/ov02c10_default.json` are
  Espressif's driver, Apache-2.0 (`LICENSE`), as kept in
  youkorr/esphome_esp-video. Modified: the formats of
  `private_include/ov02c10_custom_formats.h` are added to its format table, and
  its include path changed.
- `Kconfig` lists the formats in the table's order, so the boot format index
  is the right one. Its defaults are what the driver was run with: 1288x728,
  CSI line sync off, gain limited to 16x, analog gain first.
- `project_include.cmake` hands `cfg/ov02c10_default.json` to esp_ipa, as
  esp_cam_sensor does for its own sensors.

`CMakeLists.txt` is in ESPHome's `.gitignore`, which is meant for build
directories: it was added with `git add -f`, and has to be again if it is ever
removed and re-created. Without it ESP-IDF finds the directory and stops with
"does not contain a component".
