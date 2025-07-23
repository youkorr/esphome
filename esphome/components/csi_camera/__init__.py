import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import gpio, i2c
from esphome.const import CONF_ID, CONF_NAME, CONF_PIN, CONF_FREQUENCY, CONF_ADDRESS
from esphome import pins

DEPENDENCIES = ['esp32', 'i2c']

csi_camera_ns = cg.esphome_ns.namespace('csi_camera')
CsiCamera = csi_camera_ns.class_('CsiCamera', cg.Component, i2c.I2CDevice)

CONF_EXTERNAL_CLOCK = "external_clock"
CONF_RESET_PIN = "reset_pin"
CONF_SENSOR_ADDRESS = "sensor_address"

# Schema for external_clock
EXTERNAL_CLOCK_SCHEMA = cv.Schema({
    cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_FREQUENCY, default="20MHz"): cv.frequency,
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CsiCamera),
    cv.Optional(CONF_NAME, default="CSI Camera"): cv.string,
    cv.Optional(CONF_EXTERNAL_CLOCK): EXTERNAL_CLOCK_SCHEMA,
    cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_SENSOR_ADDRESS, default=0x24): cv.i2c_address,
}).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(default_address=0x24))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    
    # Name configuration
    cg.add(var.set_name(config[CONF_NAME]))
    
    # Sensor address configuration
    if CONF_SENSOR_ADDRESS in config:
        cg.add(var.set_sensor_address(config[CONF_SENSOR_ADDRESS]))
    
    # External clock configuration
    if CONF_EXTERNAL_CLOCK in config:
        clock_config = config[CONF_EXTERNAL_CLOCK]
        
        # Clock pin - correction to get GPIO number
        pin_config = clock_config[CONF_PIN]
        if isinstance(pin_config, dict):
            pin_num = pin_config.get('number', pin_config.get('pin', 0))
        else:
            pin_num = int(pin_config)
        cg.add(var.set_external_clock_pin(pin_num))
        
        # Clock frequency (convert to Hz)
        freq = clock_config[CONF_FREQUENCY]
        cg.add(var.set_external_clock_frequency(int(freq)))
    
    # Reset pin configuration
    if CONF_RESET_PIN in config:
        reset_pin = await gpio.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset_pin))
        
    # Add necessary build flags
    cg.add_build_flag("-DHAS_ESP32_P4_CAMERA=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

