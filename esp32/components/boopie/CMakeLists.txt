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
    "game/boopie_whack.c"
    "ime/boopie_pinyin.c"
    "ime/boopie_pinyin_dict.c"
    "store/boopie_assets.c"
    "sound/boopie_sound.c"
    "boopie_text.c"
    "font/boopie_ui_metrics.c"
    "font/boopie_ui_metrics_data.c")
set(embed)
if(CONFIG_MUSE_ENABLED)
    # LVGL comes from idf_component.yml, on the same boards as Muse's UI.
    list(APPEND srcs "font/boopie_font.c")
    # The UI font itself (font/boopie_font.h), 1.5 MB.
    list(APPEND embed "font/ui.otf")
    # The clock (NTP) and the IMU, on the device only.
    list(APPEND srcs "device/boopie_clock.c" "device/boopie_imu.c")
    # The user data partition (LittleFS, from idf_component.yml).
    list(APPEND srcs "store/boopie_store.c")
endif()

idf_component_register(
    SRCS ${srcs}
    EMBED_FILES ${embed}
    INCLUDE_DIRS "." "font" "avatar" "device" "pet" "ui" "game" "ime" "store" "sound" "setup"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES nvs_flash esp_netif esp_timer esp_partition joltwallet__littlefs
)
