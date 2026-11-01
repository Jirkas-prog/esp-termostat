set(src app.cpp sensor.cpp relay.cpp ui.cpp storage.cpp ../common/core.cpp)
if(IDF_TARGET STREQUAL "esp32c6")
  list(APPEND src zigbee.cpp)
else()
  list(APPEND src network.cpp)
endif()
idf_component_register(SRCS ${src} INCLUDE_DIRS "." "../common"
  REQUIRES driver esp_timer esp_system nvs_flash esp_lcd esp_wifi esp_event esp_netif mqtt json)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-unused-parameter)
