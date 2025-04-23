idf_component_register(SRCS "uvc.c" "pir.c" "wifi_iperf.c" "ping.c" "cat1.c" "net_module.c" "iot_mip.c" "morse.c" "system.c" "misc.c" "sleep.c" "utils.c" "debug.c" "camera.c" "storage.c" "config.c" "ota.c" "mqtt.c" "http.c" "http_client.c" "wifi.c" "main.c" 
                    INCLUDE_DIRS "."
                    EMBED_FILES "web/favicon.ico" "web/dist/index.html" "web/dist/assets/index.js" "web/dist/assets/index.css")

# use spiffs_create_partition_image package "web" to storage.bin
# spiffs_create_partition_image(storage web FLASH_IN_PROJECT)
