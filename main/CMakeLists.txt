idf_component_register(
    SRCS "usb_descriptors.c" "main.c" "audio.c" "usb.c" "led.c" "touchsensor.c" "usb_descriptors.c"
    INCLUDE_DIRS "include" "public_include")

# Pass tusb_config.h from this component to TinyUSB
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "public_include")