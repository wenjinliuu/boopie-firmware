# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

# Boopie's own code, kept apart from the upstream Muse firmware so upstream
# merges stay clean. Everything but font/boopie_font.c is plain C with no IDF
# dependencies, so tests/ builds it on the host too. avatar/boopie_avatar.c is
# built into the muse component, whose renderer it calls.
set(srcs
    "boopie_expr.c"
    "avatar/boopie_pixel.c"
    "device/boopie_shake.c"
    "pet/boopie_pet.c"
    "pet/boopie_posture.c"
    "pet/boopie_garden.c"
    "game/boopie_whack.c"
    "game/boopie_catch.c"
    "game/boopie_maze.c"
    "game/boopie_hop.c"
    "world/boopie_world.c"
    "world/boopie_world_draw.c"
    "world/boopie_world_art.c"
    "world/boopie_season.c"
    "ime/boopie_pinyin.c"
    "ime/boopie_pinyin_dict.c"
    "store/boopie_assets.c"
    "sound/boopie_sound.c"
    "sound/boopie_noise.c"
    "boopie_text.c"
    "font/boopie_pixel_font.c"
    "font/boopie_pixel_glyphs.c"
    "setup/boopie_sdk_token.c"
    "vpn/boopie_blake3.c"
    "vpn/boopie_ss.c"
    "vpn/boopie_vpn_nodes.c")
set(embed)
if(CONFIG_MUSE_ENABLED)
    # LVGL comes from idf_component.yml, on the same boards as Muse's UI.
    list(APPEND srcs "font/boopie_font.c")
    # The UI font itself (font/boopie_font.h), 1.5 MB: in the assets partition
    # where there is one (BOOPIE_DATA_IN_ASSETS), else linked in.
    if(NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_boopie_32mb.csv")
        list(APPEND embed "font/ui.otf")
    endif()
    # The clock (NTP) and the IMU, on the device only.
    list(APPEND srcs "device/boopie_clock.c" "device/boopie_imu.c" "device/boopie_crash.c")
    # The user data partition (LittleFS, from idf_component.yml).
    list(APPEND srcs "store/boopie_store.c" "store/boopie_history.c")
    # The VPN: the relay, the subscription and the resolve hook.
    list(APPEND srcs "vpn/boopie_vpn.c")
endif()

idf_component_register(
    SRCS ${srcs}
    EMBED_FILES ${embed}
    INCLUDE_DIRS "." "font" "avatar" "device" "pet" "ui" "game" "ime" "store" "sound" "setup" "vpn" "world" "xiaozhi" "tools"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES espcoredump nvs_flash esp_netif esp_timer esp_partition joltwallet__littlefs mbedtls esp_http_client lwip
)

if(CONFIG_MUSE_ENABLED)
    # lwip calls the VPN's resolve hook (CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_CUSTOM);
    # nothing else in the firmware refers to it, so it's asked for here.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lwip_hook_netconn_external_resolve")
endif()

# Boopie: with the assets partition, the UI font, the pixel font's bitmaps and
# the small world's backgrounds are read from it (tools/boopie/pack_assets.py
# --firmware-data) instead of being linked in, where they'd sit in PSRAM.
if(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_boopie_32mb.csv")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOOPIE_DATA_IN_ASSETS=1)
endif()
