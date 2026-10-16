file(GLOB FONT_SRCS "fonts/*.c")
# No REQUIRES: "main" then sees every component, which works the same whether LVGL and the
# ILI9341 driver sit in components/ or are downloaded by the component manager.
idf_component_register(
    SRCS "app_main.c" "cfg.c" "display.c" "touch.c" "net.c" "portal.c" "link.c" "ui.c" ${FONT_SRCS}
    INCLUDE_DIRS "."
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
