# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/project.cmake")
project(muse-gadget)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/validate_config.cmake")

# Boopie: the assets partition's image (fonts, sounds), packed from assets/ and
# flashed with the app, into the merged image too (docs/boopie-storage.md).
# It also carries the UI font, the pixel font's bitmaps and the small world's
# backgrounds, which the device reads from flash rather than keeping in PSRAM.
if(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_boopie_32mb.csv")
    idf_build_get_property(boopie_python PYTHON)
    set(boopie_assets_bin "${CMAKE_BINARY_DIR}/boopie_assets.bin")
    file(GLOB_RECURSE boopie_asset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/*")
    set(boopie_packer "${CMAKE_CURRENT_LIST_DIR}/../tools/boopie/pack_assets.py")
    add_custom_command(OUTPUT "${boopie_assets_bin}"
        COMMAND ${boopie_python} "${boopie_packer}" --dir "${CMAKE_CURRENT_LIST_DIR}/assets"
                --out "${boopie_assets_bin}" --max 0x3C0000 --firmware-data
        DEPENDS ${boopie_asset_files} "${boopie_packer}"
                "${CMAKE_CURRENT_LIST_DIR}/components/boopie/font/ui.otf"
                "${CMAKE_CURRENT_LIST_DIR}/components/boopie/font/boopie_pixel_glyphs.c"
                "${CMAKE_CURRENT_LIST_DIR}/components/boopie/world/boopie_world_art.c"
        VERBATIM)
    add_custom_target(boopie_assets ALL DEPENDS "${boopie_assets_bin}")
    esptool_py_flash_to_partition(flash "assets" "${boopie_assets_bin}")
endif()
