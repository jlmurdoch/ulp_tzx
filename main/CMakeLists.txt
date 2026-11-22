idf_component_register(SRCS "main.c"
                    REQUIRES esp_driver_gpio soc ulp esp_timer
                    INCLUDE_DIRS "")

#
# ULP RISC-V support additions to component CMakeLists.txt.
#
set(ulp_riscv_app_name ulp_riscv_main)
set(ulp_riscv_sources "ulp_riscv/main.S")

ulp_embed_binary(${ulp_riscv_app_name} "${ulp_riscv_sources}" "" TYPE riscv)
