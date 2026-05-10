cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(MAX30009_nRF5340DK)

target_sources(app PRIVATE 
                src/main.c
                src/spi_functions.c
                src/max30009_bioz.c)

