/*
 * GENERADO POR tools/gen_symbols.py - no editar a mano.
 *
 * Simbolos que el firmware le presta a las apps dinamicas.
 * Librerias: lvgl__lvgl, lvgl_port_lib, aos_hal, aos_ui, aos_apps, aos_board, aos_fonts
 * Mas 86 funciones de libc/libm agregadas a mano.
 * Total: 2552 simbolos.
 */

#include <stddef.h>
#include "private/elf_symbol.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
extern int LODEPNG_VERSION_STRING;
extern int __addsf3;
extern int __divdi3;
extern int __divsf3;
extern int __extendsfdf2;
extern int __fixsfsi;
extern int __fixunssfsi;
extern int __floatsisf;
extern int __floatunsisf;
extern int __moddi3;
extern int __mulsf3;
extern int __subsf3;
extern int __truncdfsf2;
extern int __udivdi3;
extern int __umoddi3;
extern int abs;
extern int aos_alarm_service_tick;
extern int aos_app_activity_get;
extern int aos_app_alarm_get;
extern int aos_app_calc_get;
extern int aos_app_calendar_get;
extern int aos_app_convert_get;
extern int aos_app_flashlight_get;
extern int aos_app_level_get;
extern int aos_app_life_get;
extern int aos_app_music_get;
extern int aos_app_notifs_get;
extern int aos_app_photos_get;
extern int aos_app_pomodoro_get;
extern int aos_app_power_get;
extern int aos_app_remote_get;
extern int aos_app_settings_get;
extern int aos_app_stopwatch_get;
extern int aos_app_timer_get;
extern int aos_app_worldclock_get;
extern int aos_apps_register_builtin;
extern int aos_board_imu_gyro_enable;
extern int aos_board_imu_gyro_enabled;
extern int aos_board_imu_orientation;
extern int aos_board_imu_poll;
extern int aos_board_imu_read;
extern int aos_board_imu_steps;
extern int aos_board_imu_steps_reset;
extern int aos_board_imu_wrist_raised;
extern int aos_board_init;
extern int aos_board_pmu_charge_current_set;
extern int aos_board_pmu_charge_target_set;
extern int aos_board_pmu_charger_get;
extern int aos_board_pmu_configure;
extern int aos_board_pmu_dump;
extern int aos_board_pmu_poll_irq;
extern int aos_board_pmu_power_off_reason;
extern int aos_board_pmu_power_on_reason;
extern int aos_board_pmu_read;
extern int aos_board_pmu_shutdown;
extern int aos_board_power_key_down;
extern int aos_board_rtc_alarm_clear;
extern int aos_board_rtc_alarm_set;
extern int aos_board_rtc_get;
extern int aos_board_rtc_present;
extern int aos_board_rtc_set;
extern int aos_board_variant;
extern int aos_board_variant_name;
extern int aos_button;
extern int aos_day_name;
extern int aos_face_analog_get;
extern int aos_face_binary_get;
extern int aos_face_digital_get;
extern int aos_face_flip_get;
extern int aos_face_minimal_get;
extern int aos_face_nixie_get;
extern int aos_face_rings_get;
extern int aos_font_body;
extern int aos_font_huge;
extern int aos_font_small;
extern int aos_font_title;
extern int aos_hal_activity;
extern int aos_hal_aod_brightness_get;
extern int aos_hal_aod_brightness_set;
extern int aos_hal_aod_enable;
extern int aos_hal_aod_enabled;
extern int aos_hal_audio_is_playing;
extern int aos_hal_audio_stop;
extern int aos_hal_battery_care_enable;
extern int aos_hal_battery_care_enabled;
extern int aos_hal_battery_read;
extern int aos_hal_beep;
extern int aos_hal_board_name;
extern int aos_hal_brightness_get;
extern int aos_hal_brightness_set;
extern int aos_hal_bt_bonded;
extern int aos_hal_bt_enable;
extern int aos_hal_bt_enabled;
extern int aos_hal_bt_forget;
extern int aos_hal_bt_pair_begin;
extern int aos_hal_bt_pair_cancel;
extern int aos_hal_bt_pair_code;
extern int aos_hal_bt_pair_confirm;
extern int aos_hal_bt_peer;
extern int aos_hal_bt_phone_battery;
extern int aos_hal_bt_state;
extern int aos_hal_display_is_on;
extern int aos_hal_display_on;
extern int aos_hal_display_set_state;
extern int aos_hal_display_state;
extern int aos_hal_firmware_version;
extern int aos_hal_heap_info;
extern int aos_hal_http_body;
extern int aos_hal_http_get;
extern int aos_hal_http_len;
extern int aos_hal_http_release;
extern int aos_hal_http_request;
extern int aos_hal_http_state;
extern int aos_hal_http_status;
extern int aos_hal_imu_gyro_request;
extern int aos_hal_imu_orientation;
extern int aos_hal_imu_read;
extern int aos_hal_imu_steps;
extern int aos_hal_imu_steps_reset;
extern int aos_hal_init;
extern int aos_hal_lock;
extern int aos_hal_log;
extern int aos_hal_media_command;
extern int aos_hal_media_enable;
extern int aos_hal_media_enabled;
extern int aos_hal_media_info;
extern int aos_hal_media_link;
extern int aos_hal_media_peer;
extern int aos_hal_media_player;
extern int aos_hal_mic_available;
extern int aos_hal_mic_close;
extern int aos_hal_mic_gain_get;
extern int aos_hal_mic_gain_set;
extern int aos_hal_mic_level;
extern int aos_hal_mic_open;
extern int aos_hal_mic_read;
extern int aos_hal_mic_status;
extern int aos_hal_net_ap_active;
extern int aos_hal_net_ap_default_ssid;
extern int aos_hal_net_ap_ip;
extern int aos_hal_net_ap_pass;
extern int aos_hal_net_ap_pass_mode;
extern int aos_hal_net_ap_set_config;
extern int aos_hal_net_ap_ssid;
extern int aos_hal_net_ap_start;
extern int aos_hal_net_ap_stop;
extern int aos_hal_net_enable;
extern int aos_hal_net_enabled;
extern int aos_hal_net_forget;
extern int aos_hal_net_has_credentials;
extern int aos_hal_net_ip;
extern int aos_hal_net_rssi;
extern int aos_hal_net_scan;
extern int aos_hal_net_set_credentials;
extern int aos_hal_net_ssid;
extern int aos_hal_net_state;
extern int aos_hal_net_sync_time;
extern int aos_hal_notif_action;
extern int aos_hal_notif_action_failed;
extern int aos_hal_notif_at;
extern int aos_hal_notif_calls_always;
extern int aos_hal_notif_calls_always_set;
extern int aos_hal_notif_categories;
extern int aos_hal_notif_categories_set;
extern int aos_hal_notif_clear;
extern int aos_hal_notif_count;
extern int aos_hal_notif_enable;
extern int aos_hal_notif_enabled;
extern int aos_hal_notif_pop;
extern int aos_hal_notif_pop_removed;
extern int aos_hal_notif_remove;
extern int aos_hal_notif_sound;
extern int aos_hal_notif_sound_set;
extern int aos_hal_ota_abort;
extern int aos_hal_ota_begin;
extern int aos_hal_ota_end;
extern int aos_hal_ota_error;
extern int aos_hal_ota_mark_valid;
extern int aos_hal_ota_pending_verify;
extern int aos_hal_ota_running_slot;
extern int aos_hal_ota_write;
extern int aos_hal_panel_sleep_enable;
extern int aos_hal_panel_sleep_enabled;
extern int aos_hal_path_apps;
extern int aos_hal_path_data;
extern int aos_hal_path_lang;
extern int aos_hal_path_music;
extern int aos_hal_path_photos;
extern int aos_hal_path_recordings;
extern int aos_hal_path_scans;
extern int aos_hal_play_file;
extern int aos_hal_player_pause;
extern int aos_hal_player_play;
extern int aos_hal_player_resume;
extern int aos_hal_player_status;
extern int aos_hal_player_stop;
extern int aos_hal_power_info;
extern int aos_hal_power_saving_enable;
extern int aos_hal_power_saving_enabled;
extern int aos_hal_pref_erase;
extern int aos_hal_pref_get_i32;
extern int aos_hal_pref_get_str;
extern int aos_hal_pref_set_i32;
extern int aos_hal_pref_set_str;
extern int aos_hal_reboot;
extern int aos_hal_rec_pause;
extern int aos_hal_rec_peaks;
extern int aos_hal_rec_resume;
extern int aos_hal_rec_start;
extern int aos_hal_rec_status;
extern int aos_hal_rec_stop;
extern int aos_hal_rtc_alarm_clear;
extern int aos_hal_rtc_alarm_set;
extern int aos_hal_scan_start;
extern int aos_hal_scan_status;
extern int aos_hal_scan_stop;
extern int aos_hal_sd_present;
extern int aos_hal_sd_usage;
extern int aos_hal_set_button_cb;
extern int aos_hal_set_display_state_cb;
extern int aos_hal_set_power_event_cb;
extern int aos_hal_shutdown;
extern int aos_hal_sleep;
extern int aos_hal_time_is_valid;
extern int aos_hal_time_now;
extern int aos_hal_time_set;
extern int aos_hal_timezone_get;
extern int aos_hal_timezone_set;
extern int aos_hal_touch_gesture;
extern int aos_hal_unlock;
extern int aos_hal_uptime_ms;
extern int aos_hal_volume_get;
extern int aos_hal_volume_set;
extern int aos_hand_create;
extern int aos_hand_set_angle;
extern int aos_i18n_app_count;
extern int aos_i18n_app_load;
extern int aos_i18n_app_unload;
extern int aos_i18n_count;
extern int aos_i18n_current;
extern int aos_i18n_init;
extern int aos_i18n_scan;
extern int aos_i18n_set;
extern int aos_icon_create;
extern int aos_imu_start;
extern int aos_label;
extern int aos_label_boxed;
extern int aos_label_scaled;
extern int aos_lang_pack_count;
extern int aos_lang_packs;
extern int aos_launcher_create;
extern int aos_make_decorative;
extern int aos_month_name;
extern int aos_montserrat_14;
extern int aos_montserrat_16;
extern int aos_montserrat_20;
extern int aos_montserrat_28;
extern int aos_montserrat_36;
extern int aos_montserrat_48;
extern int aos_notif_action_failed;
extern int aos_notif_push;
extern int aos_notif_push_removed;
extern int aos_notif_reset_pending;
extern int aos_notif_ui_close;
extern int aos_notif_ui_show;
extern int aos_notif_ui_tick;
extern int aos_notif_ui_uid;
extern int aos_notif_ui_visible;
extern int aos_page;
extern int aos_pair_ui_cancel;
extern int aos_pair_ui_suppress;
extern int aos_pair_ui_tick;
extern int aos_pair_ui_visible;
extern int aos_rtc_start;
extern int aos_text_font_has;
extern int aos_text_safe;
extern int aos_theme_init;
extern int aos_tr;
extern int aos_trc;
extern int aos_ui_app_at;
extern int aos_ui_app_count;
extern int aos_ui_app_find;
extern int aos_ui_back;
extern int aos_ui_button;
extern int aos_ui_current_app;
extern int aos_ui_home;
extern int aos_ui_init;
extern int aos_ui_launcher_get_style;
extern int aos_ui_launcher_set_style;
extern int aos_ui_open;
extern int aos_ui_register_app;
extern int aos_ui_request_language;
extern int aos_ui_request_snapshot;
extern int aos_ui_request_watchface_picker;
extern int aos_ui_show_launcher;
extern int aos_ui_snapshot_peek;
extern int aos_ui_snapshot_release;
extern int aos_ui_statusbar_refresh;
extern int aos_ui_statusbar_set_visible;
extern int aos_ui_take_gesture;
extern int aos_ui_tick;
extern int aos_ui_toast;
extern int aos_ui_touch_calibration_reset;
extern int aos_ui_touch_calibration_save;
extern int aos_ui_touch_raw;
extern int aos_ui_touch_stats;
extern int aos_ui_unregister_app;
extern int aos_watchface_at;
extern int aos_watchface_close_picker;
extern int aos_watchface_count;
extern int aos_watchface_create;
extern int aos_watchface_current;
extern int aos_watchface_is_aod;
extern int aos_watchface_open_picker;
extern int aos_watchface_picker_visible;
extern int aos_watchface_refresh;
extern int aos_watchface_register;
extern int aos_watchface_select;
extern int aos_watchface_set_aod;
extern int aos_watchfaces_register_builtin;
extern int aos_wifi_qr_text;
extern int atan2f;
extern int atoi;
extern int atol;
extern int axp2101_battery_percent;
extern int axp2101_battery_present;
extern int axp2101_battery_voltage;
extern int axp2101_charge_current_ma;
extern int axp2101_charge_current_set;
extern int axp2101_charge_state;
extern int axp2101_charge_state_name;
extern int axp2101_charge_target_mv;
extern int axp2101_charge_target_set;
extern int axp2101_charging_enable;
extern int axp2101_die_temperature;
extern int axp2101_dump;
extern int axp2101_init;
extern int axp2101_irq_enable;
extern int axp2101_irq_read_clear;
extern int axp2101_is_charging;
extern int axp2101_is_vbus_present;
extern int axp2101_low_battery_levels_get;
extern int axp2101_low_battery_levels_set;
extern int axp2101_power_key_timing_get;
extern int axp2101_power_key_timing_set;
extern int axp2101_power_off_source;
extern int axp2101_power_off_source_name;
extern int axp2101_power_on_source;
extern int axp2101_power_on_source_name;
extern int axp2101_poweroff_voltage_mv;
extern int axp2101_poweroff_voltage_set;
extern int axp2101_precharge_current_ma;
extern int axp2101_precharge_current_set;
extern int axp2101_rail_enable;
extern int axp2101_rail_is_enabled;
extern int axp2101_rail_name;
extern int axp2101_rail_voltage_mv;
extern int axp2101_shutdown;
extern int axp2101_system_voltage;
extern int axp2101_termination_current_ma;
extern int axp2101_termination_current_set;
extern int axp2101_ts_temperature;
extern int axp2101_ts_voltage;
extern int axp2101_vbus_current_limit_ma;
extern int axp2101_vbus_current_limit_set;
extern int axp2101_vbus_voltage;
extern int calloc;
extern int ceilf;
extern int closedir;
extern int cosf;
extern int exp2f;
extern int expf;
extern int fabsf;
extern int fclose;
extern int fflush;
extern int floorf;
extern int fmodf;
extern int fopen;
extern int fread;
extern int free;
extern int frogfs_decomp_raw;
extern int fseek;
extern int ftell;
extern int fwrite;
extern int getenv;
extern int gmtime_r;
extern int hypotf;
extern int jd_decomp;
extern int jd_mcu_load;
extern int jd_mcu_output;
extern int jd_prepare;
extern int jd_restart;
extern int labs;
extern int load_kern;
extern int localtime_r;
extern int lodepng_add_itext;
extern int lodepng_add_text;
extern int lodepng_can_have_alpha;
extern int lodepng_chunk_ancillary;
extern int lodepng_chunk_append;
extern int lodepng_chunk_check_crc;
extern int lodepng_chunk_create;
extern int lodepng_chunk_data;
extern int lodepng_chunk_data_const;
extern int lodepng_chunk_find;
extern int lodepng_chunk_find_const;
extern int lodepng_chunk_generate_crc;
extern int lodepng_chunk_length;
extern int lodepng_chunk_next;
extern int lodepng_chunk_next_const;
extern int lodepng_chunk_private;
extern int lodepng_chunk_safetocopy;
extern int lodepng_chunk_type;
extern int lodepng_chunk_type_equals;
extern int lodepng_clear_icc;
extern int lodepng_clear_itext;
extern int lodepng_clear_text;
extern int lodepng_color_mode_cleanup;
extern int lodepng_color_mode_copy;
extern int lodepng_color_mode_init;
extern int lodepng_color_mode_make;
extern int lodepng_color_stats_init;
extern int lodepng_compress_settings_init;
extern int lodepng_compute_color_stats;
extern int lodepng_convert;
extern int lodepng_crc32;
extern int lodepng_decode;
extern int lodepng_decode24;
extern int lodepng_decode24_file;
extern int lodepng_decode32;
extern int lodepng_decode32_file;
extern int lodepng_decode_file;
extern int lodepng_decode_memory;
extern int lodepng_decoder_settings_init;
extern int lodepng_decompress_settings_init;
extern int lodepng_default_compress_settings;
extern int lodepng_default_decompress_settings;
extern int lodepng_deflate;
extern int lodepng_encode;
extern int lodepng_encode24;
extern int lodepng_encode24_file;
extern int lodepng_encode32;
extern int lodepng_encode32_file;
extern int lodepng_encode_file;
extern int lodepng_encode_memory;
extern int lodepng_encoder_settings_init;
extern int lodepng_error_text;
extern int lodepng_get_bpp;
extern int lodepng_get_channels;
extern int lodepng_get_raw_size;
extern int lodepng_has_palette_alpha;
extern int lodepng_huffman_code_lengths;
extern int lodepng_inflate;
extern int lodepng_info_cleanup;
extern int lodepng_info_copy;
extern int lodepng_info_init;
extern int lodepng_inspect;
extern int lodepng_inspect_chunk;
extern int lodepng_is_alpha_type;
extern int lodepng_is_greyscale_type;
extern int lodepng_is_palette_type;
extern int lodepng_load_file;
extern int lodepng_palette_add;
extern int lodepng_palette_clear;
extern int lodepng_save_file;
extern int lodepng_set_icc;
extern int lodepng_state_cleanup;
extern int lodepng_state_copy;
extern int lodepng_state_init;
extern int lodepng_zlib_compress;
extern int lodepng_zlib_decompress;
extern int log10f;
extern int log2f;
extern int logf;
extern int lv_anim_core_deinit;
extern int lv_anim_core_init;
extern int lv_anim_count_running;
extern int lv_anim_custom_delete;
extern int lv_anim_custom_get;
extern int lv_anim_delete;
extern int lv_anim_delete_all;
extern int lv_anim_enable_vsync_mode;
extern int lv_anim_get;
extern int lv_anim_get_delay;
extern int lv_anim_get_playtime;
extern int lv_anim_get_repeat_count;
extern int lv_anim_get_time;
extern int lv_anim_get_timer;
extern int lv_anim_get_user_data;
extern int lv_anim_init;
extern int lv_anim_is_paused;
extern int lv_anim_path_bounce;
extern int lv_anim_path_custom_bezier3;
extern int lv_anim_path_ease_in;
extern int lv_anim_path_ease_in_out;
extern int lv_anim_path_ease_out;
extern int lv_anim_path_linear;
extern int lv_anim_path_overshoot;
extern int lv_anim_path_step;
extern int lv_anim_pause;
extern int lv_anim_pause_for;
extern int lv_anim_refr_now;
extern int lv_anim_resolve_speed;
extern int lv_anim_resume;
extern int lv_anim_set_bezier3_param;
extern int lv_anim_set_completed_cb;
extern int lv_anim_set_custom_exec_cb;
extern int lv_anim_set_delay;
extern int lv_anim_set_deleted_cb;
extern int lv_anim_set_duration;
extern int lv_anim_set_early_apply;
extern int lv_anim_set_exec_cb;
extern int lv_anim_set_get_value_cb;
extern int lv_anim_set_path_cb;
extern int lv_anim_set_repeat_count;
extern int lv_anim_set_repeat_delay;
extern int lv_anim_set_reverse_delay;
extern int lv_anim_set_reverse_duration;
extern int lv_anim_set_reverse_time;
extern int lv_anim_set_start_cb;
extern int lv_anim_set_user_data;
extern int lv_anim_set_values;
extern int lv_anim_set_var;
extern int lv_anim_speed;
extern int lv_anim_speed_clamped;
extern int lv_anim_speed_to_time;
extern int lv_anim_start;
extern int lv_anim_timeline_add;
extern int lv_anim_timeline_create;
extern int lv_anim_timeline_delete;
extern int lv_anim_timeline_get_delay;
extern int lv_anim_timeline_get_playtime;
extern int lv_anim_timeline_get_progress;
extern int lv_anim_timeline_get_repeat_count;
extern int lv_anim_timeline_get_repeat_delay;
extern int lv_anim_timeline_get_reverse;
extern int lv_anim_timeline_get_user_data;
extern int lv_anim_timeline_merge;
extern int lv_anim_timeline_pause;
extern int lv_anim_timeline_set_delay;
extern int lv_anim_timeline_set_progress;
extern int lv_anim_timeline_set_repeat_count;
extern int lv_anim_timeline_set_repeat_delay;
extern int lv_anim_timeline_set_reverse;
extern int lv_anim_timeline_set_user_data;
extern int lv_anim_timeline_start;
extern int lv_animimg_class;
extern int lv_animimg_create;
extern int lv_animimg_delete;
extern int lv_animimg_get_anim;
extern int lv_animimg_get_duration;
extern int lv_animimg_get_repeat_count;
extern int lv_animimg_get_src;
extern int lv_animimg_get_src_count;
extern int lv_animimg_set_completed_cb;
extern int lv_animimg_set_duration;
extern int lv_animimg_set_repeat_count;
extern int lv_animimg_set_reverse_delay;
extern int lv_animimg_set_reverse_duration;
extern int lv_animimg_set_src;
extern int lv_animimg_set_src_reverse;
extern int lv_animimg_set_start_cb;
extern int lv_animimg_start;
extern int lv_arc_align_obj_to_angle;
extern int lv_arc_bind_value;
extern int lv_arc_class;
extern int lv_arc_create;
extern int lv_arc_get_angle_end;
extern int lv_arc_get_angle_start;
extern int lv_arc_get_bg_angle_end;
extern int lv_arc_get_bg_angle_start;
extern int lv_arc_get_change_rate;
extern int lv_arc_get_knob_offset;
extern int lv_arc_get_max_value;
extern int lv_arc_get_min_value;
extern int lv_arc_get_mode;
extern int lv_arc_get_rotation;
extern int lv_arc_get_value;
extern int lv_arc_rotate_obj_to_angle;
extern int lv_arc_set_angles;
extern int lv_arc_set_bg_angles;
extern int lv_arc_set_bg_end_angle;
extern int lv_arc_set_bg_start_angle;
extern int lv_arc_set_change_rate;
extern int lv_arc_set_end_angle;
extern int lv_arc_set_knob_offset;
extern int lv_arc_set_max_value;
extern int lv_arc_set_min_value;
extern int lv_arc_set_mode;
extern int lv_arc_set_range;
extern int lv_arc_set_rotation;
extern int lv_arc_set_start_angle;
extern int lv_arc_set_value;
extern int lv_arclabel_class;
extern int lv_arclabel_create;
extern int lv_arclabel_get_angle_size;
extern int lv_arclabel_get_angle_start;
extern int lv_arclabel_get_center_offset_x;
extern int lv_arclabel_get_center_offset_y;
extern int lv_arclabel_get_dir;
extern int lv_arclabel_get_end_overlap;
extern int lv_arclabel_get_overflow;
extern int lv_arclabel_get_radius;
extern int lv_arclabel_get_recolor;
extern int lv_arclabel_get_text_angle;
extern int lv_arclabel_get_text_horizontal_align;
extern int lv_arclabel_get_text_vertical_align;
extern int lv_arclabel_set_angle_size;
extern int lv_arclabel_set_angle_start;
extern int lv_arclabel_set_center_offset_x;
extern int lv_arclabel_set_center_offset_y;
extern int lv_arclabel_set_dir;
extern int lv_arclabel_set_end_overlap;
extern int lv_arclabel_set_offset;
extern int lv_arclabel_set_overflow;
extern int lv_arclabel_set_radius;
extern int lv_arclabel_set_recolor;
extern int lv_arclabel_set_text;
extern int lv_arclabel_set_text_fmt;
extern int lv_arclabel_set_text_horizontal_align;
extern int lv_arclabel_set_text_static;
extern int lv_arclabel_set_text_vertical_align;
extern int lv_area_align;
extern int lv_area_diff;
extern int lv_area_get_height;
extern int lv_area_get_size;
extern int lv_area_get_width;
extern int lv_area_increase;
extern int lv_area_intersect;
extern int lv_area_is_equal;
extern int lv_area_is_in;
extern int lv_area_is_on;
extern int lv_area_is_out;
extern int lv_area_is_point_on;
extern int lv_area_join;
extern int lv_area_move;
extern int lv_area_set;
extern int lv_area_set_height;
extern int lv_area_set_pos;
extern int lv_area_set_width;
extern int lv_array_assign;
extern int lv_array_at;
extern int lv_array_concat;
extern int lv_array_copy;
extern int lv_array_deinit;
extern int lv_array_erase;
extern int lv_array_init;
extern int lv_array_init_from_buf;
extern int lv_array_push_back;
extern int lv_array_remove;
extern int lv_array_remove_unordered;
extern int lv_array_resize;
extern int lv_array_shrink;
extern int lv_async_call;
extern int lv_async_call_cancel;
extern int lv_atan2;
extern int lv_bar_bind_value;
extern int lv_bar_class;
extern int lv_bar_create;
extern int lv_bar_get_max_value;
extern int lv_bar_get_min_value;
extern int lv_bar_get_mode;
extern int lv_bar_get_orientation;
extern int lv_bar_get_start_value;
extern int lv_bar_get_value;
extern int lv_bar_is_symmetrical;
extern int lv_bar_set_max_value;
extern int lv_bar_set_min_value;
extern int lv_bar_set_mode;
extern int lv_bar_set_orientation;
extern int lv_bar_set_range;
extern int lv_bar_set_start_value;
extern int lv_bar_set_value;
extern int lv_bezier3;
extern int lv_bin_decoder_close;
extern int lv_bin_decoder_get_area;
extern int lv_bin_decoder_info;
extern int lv_bin_decoder_init;
extern int lv_bin_decoder_open;
extern int lv_binfont_create;
extern int lv_binfont_destroy;
extern int lv_binfont_font_class;
extern int lv_bmp_deinit;
extern int lv_bmp_init;
extern int lv_builtin_font_class;
extern int lv_button_class;
extern int lv_button_create;
extern int lv_buttonmatrix_class;
extern int lv_buttonmatrix_clear_button_ctrl;
extern int lv_buttonmatrix_clear_button_ctrl_all;
extern int lv_buttonmatrix_create;
extern int lv_buttonmatrix_get_button_text;
extern int lv_buttonmatrix_get_map;
extern int lv_buttonmatrix_get_one_checked;
extern int lv_buttonmatrix_get_selected_button;
extern int lv_buttonmatrix_has_button_ctrl;
extern int lv_buttonmatrix_set_button_ctrl;
extern int lv_buttonmatrix_set_button_ctrl_all;
extern int lv_buttonmatrix_set_button_width;
extern int lv_buttonmatrix_set_ctrl_map;
extern int lv_buttonmatrix_set_map;
extern int lv_buttonmatrix_set_one_checked;
extern int lv_buttonmatrix_set_selected_button;
extern int lv_cache_acquire;
extern int lv_cache_acquire_or_create;
extern int lv_cache_add;
extern int lv_cache_class_lru_ll_count;
extern int lv_cache_class_lru_ll_size;
extern int lv_cache_class_lru_rb_count;
extern int lv_cache_class_lru_rb_size;
extern int lv_cache_class_sc_da;
extern int lv_cache_create;
extern int lv_cache_destroy;
extern int lv_cache_drop;
extern int lv_cache_drop_all;
extern int lv_cache_entry_acquire_data;
extern int lv_cache_entry_alloc;
extern int lv_cache_entry_dec_ref;
extern int lv_cache_entry_delete;
extern int lv_cache_entry_get_cache;
extern int lv_cache_entry_get_data;
extern int lv_cache_entry_get_entry;
extern int lv_cache_entry_get_node_size;
extern int lv_cache_entry_get_ref;
extern int lv_cache_entry_get_size;
extern int lv_cache_entry_has_flag;
extern int lv_cache_entry_inc_ref;
extern int lv_cache_entry_init;
extern int lv_cache_entry_is_invalid;
extern int lv_cache_entry_release_data;
extern int lv_cache_entry_remove_flag;
extern int lv_cache_entry_reset_ref;
extern int lv_cache_entry_set_cache;
extern int lv_cache_entry_set_flag;
extern int lv_cache_entry_set_node_size;
extern int lv_cache_evict_one;
extern int lv_cache_get_free_size;
extern int lv_cache_get_max_size;
extern int lv_cache_get_name;
extern int lv_cache_get_size;
extern int lv_cache_is_enabled;
extern int lv_cache_iter_create;
extern int lv_cache_release;
extern int lv_cache_reserve;
extern int lv_cache_set_compare_cb;
extern int lv_cache_set_create_cb;
extern int lv_cache_set_free_cb;
extern int lv_cache_set_max_size;
extern int lv_cache_set_name;
extern int lv_calendar_add_header_arrow;
extern int lv_calendar_add_header_dropdown;
extern int lv_calendar_class;
extern int lv_calendar_create;
extern int lv_calendar_get_btnmatrix;
extern int lv_calendar_get_highlighted_dates;
extern int lv_calendar_get_highlighted_dates_num;
extern int lv_calendar_get_pressed_date;
extern int lv_calendar_get_showed_date;
extern int lv_calendar_get_today_date;
extern int lv_calendar_header_arrow_class;
extern int lv_calendar_header_dropdown_class;
extern int lv_calendar_header_dropdown_set_year_list;
extern int lv_calendar_set_day_names;
extern int lv_calendar_set_highlighted_dates;
extern int lv_calendar_set_month_shown;
extern int lv_calendar_set_shown_month;
extern int lv_calendar_set_shown_year;
extern int lv_calendar_set_today_date;
extern int lv_calendar_set_today_day;
extern int lv_calendar_set_today_month;
extern int lv_calendar_set_today_year;
extern int lv_calloc;
extern int lv_canvas_buf_size;
extern int lv_canvas_class;
extern int lv_canvas_copy_buf;
extern int lv_canvas_create;
extern int lv_canvas_fill_bg;
extern int lv_canvas_finish_layer;
extern int lv_canvas_get_buf;
extern int lv_canvas_get_draw_buf;
extern int lv_canvas_get_image;
extern int lv_canvas_get_px;
extern int lv_canvas_init_layer;
extern int lv_canvas_set_buffer;
extern int lv_canvas_set_draw_buf;
extern int lv_canvas_set_palette;
extern int lv_canvas_set_px;
extern int lv_chart_add_cursor;
extern int lv_chart_add_series;
extern int lv_chart_class;
extern int lv_chart_create;
extern int lv_chart_get_cursor_point;
extern int lv_chart_get_first_point_center_offset;
extern int lv_chart_get_hor_div_line_count;
extern int lv_chart_get_point_count;
extern int lv_chart_get_point_pos_by_id;
extern int lv_chart_get_pressed_point;
extern int lv_chart_get_series_color;
extern int lv_chart_get_series_next;
extern int lv_chart_get_series_x_array;
extern int lv_chart_get_series_y_array;
extern int lv_chart_get_type;
extern int lv_chart_get_update_mode;
extern int lv_chart_get_ver_div_line_count;
extern int lv_chart_get_x_start_point;
extern int lv_chart_hide_series;
extern int lv_chart_refresh;
extern int lv_chart_remove_cursor;
extern int lv_chart_remove_series;
extern int lv_chart_set_all_values;
extern int lv_chart_set_axis_max_value;
extern int lv_chart_set_axis_min_value;
extern int lv_chart_set_axis_range;
extern int lv_chart_set_cursor_point;
extern int lv_chart_set_cursor_pos;
extern int lv_chart_set_cursor_pos_x;
extern int lv_chart_set_cursor_pos_y;
extern int lv_chart_set_div_line_count;
extern int lv_chart_set_hor_div_line_count;
extern int lv_chart_set_next_value;
extern int lv_chart_set_next_value2;
extern int lv_chart_set_point_count;
extern int lv_chart_set_series_color;
extern int lv_chart_set_series_ext_x_array;
extern int lv_chart_set_series_ext_y_array;
extern int lv_chart_set_series_value_by_id;
extern int lv_chart_set_series_value_by_id2;
extern int lv_chart_set_series_values;
extern int lv_chart_set_series_values2;
extern int lv_chart_set_type;
extern int lv_chart_set_update_mode;
extern int lv_chart_set_ver_div_line_count;
extern int lv_chart_set_x_start_point;
extern int lv_checkbox_class;
extern int lv_checkbox_create;
extern int lv_checkbox_get_text;
extern int lv_checkbox_set_text;
extern int lv_checkbox_set_text_static;
extern int lv_circle_buf_capacity;
extern int lv_circle_buf_create;
extern int lv_circle_buf_create_from_array;
extern int lv_circle_buf_create_from_buf;
extern int lv_circle_buf_destroy;
extern int lv_circle_buf_fill;
extern int lv_circle_buf_head;
extern int lv_circle_buf_is_empty;
extern int lv_circle_buf_is_full;
extern int lv_circle_buf_peek;
extern int lv_circle_buf_peek_at;
extern int lv_circle_buf_read;
extern int lv_circle_buf_remain;
extern int lv_circle_buf_reset;
extern int lv_circle_buf_resize;
extern int lv_circle_buf_size;
extern int lv_circle_buf_skip;
extern int lv_circle_buf_tail;
extern int lv_circle_buf_write;
extern int lv_clamp_height;
extern int lv_clamp_width;
extern int lv_color16_luminance;
extern int lv_color16_premultiply;
extern int lv_color24_luminance;
extern int lv_color32_eq;
extern int lv_color32_luminance;
extern int lv_color32_make;
extern int lv_color_16_16_mix;
extern int lv_color_black;
extern int lv_color_brightness;
extern int lv_color_darken;
extern int lv_color_eq;
extern int lv_color_filter_dsc_init;
extern int lv_color_filter_shade;
extern int lv_color_format_get_bpp;
extern int lv_color_format_get_size;
extern int lv_color_format_has_alpha;
extern int lv_color_hex;
extern int lv_color_hex3;
extern int lv_color_hsv_to_rgb;
extern int lv_color_lighten;
extern int lv_color_luminance;
extern int lv_color_make;
extern int lv_color_mix;
extern int lv_color_mix32;
extern int lv_color_mix32_premultiplied;
extern int lv_color_over32;
extern int lv_color_premultiply;
extern int lv_color_rgb_to_hsv;
extern int lv_color_to_32;
extern int lv_color_to_hsv;
extern int lv_color_to_int;
extern int lv_color_to_u16;
extern int lv_color_to_u32;
extern int lv_color_white;
extern int lv_cubic_bezier;
extern int lv_deinit;
extern int lv_delay_ms;
extern int lv_delay_set_cb;
extern int lv_display_add_event_cb;
extern int lv_display_create;
extern int lv_display_delete;
extern int lv_display_delete_event;
extern int lv_display_delete_refr_timer;
extern int lv_display_dpx;
extern int lv_display_enable_invalidation;
extern int lv_display_flush_is_last;
extern int lv_display_flush_ready;
extern int lv_display_get_antialiasing;
extern int lv_display_get_buf_active;
extern int lv_display_get_color_format;
extern int lv_display_get_default;
extern int lv_display_get_dpi;
extern int lv_display_get_draw_buf_size;
extern int lv_display_get_driver_data;
extern int lv_display_get_event_count;
extern int lv_display_get_event_dsc;
extern int lv_display_get_horizontal_resolution;
extern int lv_display_get_inactive_time;
extern int lv_display_get_invalidated_draw_buf_size;
extern int lv_display_get_layer_bottom;
extern int lv_display_get_layer_sys;
extern int lv_display_get_layer_top;
extern int lv_display_get_matrix_rotation;
extern int lv_display_get_next;
extern int lv_display_get_offset_x;
extern int lv_display_get_offset_y;
extern int lv_display_get_original_horizontal_resolution;
extern int lv_display_get_original_vertical_resolution;
extern int lv_display_get_physical_horizontal_resolution;
extern int lv_display_get_physical_vertical_resolution;
extern int lv_display_get_refr_timer;
extern int lv_display_get_render_mode;
extern int lv_display_get_rotation;
extern int lv_display_get_screen_active;
extern int lv_display_get_screen_loading;
extern int lv_display_get_screen_prev;
extern int lv_display_get_theme;
extern int lv_display_get_tile_cnt;
extern int lv_display_get_user_data;
extern int lv_display_get_vertical_resolution;
extern int lv_display_is_double_buffered;
extern int lv_display_is_invalidation_enabled;
extern int lv_display_refr_timer;
extern int lv_display_register_vsync_event;
extern int lv_display_remove_event_cb_with_user_data;
extern int lv_display_rotate_area;
extern int lv_display_rotate_point;
extern int lv_display_send_event;
extern int lv_display_send_vsync_event;
extern int lv_display_set_3rd_draw_buffer;
extern int lv_display_set_antialiasing;
extern int lv_display_set_buffers;
extern int lv_display_set_buffers_with_stride;
extern int lv_display_set_color_format;
extern int lv_display_set_default;
extern int lv_display_set_dpi;
extern int lv_display_set_draw_buffers;
extern int lv_display_set_driver_data;
extern int lv_display_set_flush_cb;
extern int lv_display_set_flush_wait_cb;
extern int lv_display_set_matrix_rotation;
extern int lv_display_set_offset;
extern int lv_display_set_physical_resolution;
extern int lv_display_set_render_mode;
extern int lv_display_set_resolution;
extern int lv_display_set_rotation;
extern int lv_display_set_theme;
extern int lv_display_set_tile_cnt;
extern int lv_display_set_user_data;
extern int lv_display_trigger_activity;
extern int lv_display_unregister_vsync_event;
extern int lv_dpx;
extern int lv_draw_add_task;
extern int lv_draw_arc;
extern int lv_draw_arc_dsc_init;
extern int lv_draw_arc_get_area;
extern int lv_draw_blur;
extern int lv_draw_blur_dsc_init;
extern int lv_draw_border;
extern int lv_draw_border_dsc_init;
extern int lv_draw_box_shadow;
extern int lv_draw_box_shadow_dsc_init;
extern int lv_draw_buf_adjust_stride;
extern int lv_draw_buf_align;
extern int lv_draw_buf_align_ex;
extern int lv_draw_buf_clear;
extern int lv_draw_buf_convert_premultiply;
extern int lv_draw_buf_copy;
extern int lv_draw_buf_create;
extern int lv_draw_buf_create_ex;
extern int lv_draw_buf_destroy;
extern int lv_draw_buf_dup;
extern int lv_draw_buf_dup_ex;
extern int lv_draw_buf_flush_cache;
extern int lv_draw_buf_from_image;
extern int lv_draw_buf_get_font_handlers;
extern int lv_draw_buf_get_handlers;
extern int lv_draw_buf_get_image_handlers;
extern int lv_draw_buf_goto_xy;
extern int lv_draw_buf_handlers_init;
extern int lv_draw_buf_init;
extern int lv_draw_buf_init_handlers;
extern int lv_draw_buf_init_with_default_handlers;
extern int lv_draw_buf_invalidate_cache;
extern int lv_draw_buf_premultiply;
extern int lv_draw_buf_reshape;
extern int lv_draw_buf_save_to_file;
extern int lv_draw_buf_set_palette;
extern int lv_draw_buf_to_image;
extern int lv_draw_buf_width_to_stride;
extern int lv_draw_buf_width_to_stride_ex;
extern int lv_draw_character;
extern int lv_draw_create_unit;
extern int lv_draw_deinit;
extern int lv_draw_dispatch;
extern int lv_draw_dispatch_layer;
extern int lv_draw_dispatch_request;
extern int lv_draw_dispatch_wait_for_request;
extern int lv_draw_fill;
extern int lv_draw_fill_dsc_init;
extern int lv_draw_finalize_task_creation;
extern int lv_draw_get_available_task;
extern int lv_draw_get_dependent_count;
extern int lv_draw_get_next_available_task;
extern int lv_draw_get_unit_count;
extern int lv_draw_glyph_dsc_init;
extern int lv_draw_image;
extern int lv_draw_image_dsc_init;
extern int lv_draw_image_normal_helper;
extern int lv_draw_image_tiled_helper;
extern int lv_draw_init;
extern int lv_draw_label;
extern int lv_draw_label_dsc_init;
extern int lv_draw_label_iterate_characters;
extern int lv_draw_layer;
extern int lv_draw_layer_alloc_buf;
extern int lv_draw_layer_create;
extern int lv_draw_layer_create_drop_shadow;
extern int lv_draw_layer_finish_drop_shadow;
extern int lv_draw_layer_go_to_xy;
extern int lv_draw_layer_init;
extern int lv_draw_letter;
extern int lv_draw_letter_dsc_init;
extern int lv_draw_line;
extern int lv_draw_line_dsc_init;
extern int lv_draw_line_iterate;
extern int lv_draw_mask_rect;
extern int lv_draw_mask_rect_dsc_init;
extern int lv_draw_rect;
extern int lv_draw_rect_dsc_init;
extern int lv_draw_sw_arc;
extern int lv_draw_sw_blend;
extern int lv_draw_sw_blend_color_to_a8;
extern int lv_draw_sw_blend_color_to_al88;
extern int lv_draw_sw_blend_color_to_argb8888;
extern int lv_draw_sw_blend_color_to_argb8888_premultiplied;
extern int lv_draw_sw_blend_color_to_i1;
extern int lv_draw_sw_blend_color_to_l8;
extern int lv_draw_sw_blend_color_to_rgb565;
extern int lv_draw_sw_blend_color_to_rgb565_swapped;
extern int lv_draw_sw_blend_color_to_rgb888;
extern int lv_draw_sw_blend_image_to_a8;
extern int lv_draw_sw_blend_image_to_al88;
extern int lv_draw_sw_blend_image_to_argb8888;
extern int lv_draw_sw_blend_image_to_argb8888_premultiplied;
extern int lv_draw_sw_blend_image_to_i1;
extern int lv_draw_sw_blend_image_to_l8;
extern int lv_draw_sw_blend_image_to_rgb565;
extern int lv_draw_sw_blend_image_to_rgb565_swapped;
extern int lv_draw_sw_blend_image_to_rgb888;
extern int lv_draw_sw_blur;
extern int lv_draw_sw_border;
extern int lv_draw_sw_box_shadow;
extern int lv_draw_sw_deinit;
extern int lv_draw_sw_fill;
extern int lv_draw_sw_get_blend_handler;
extern int lv_draw_sw_grad_cleanup;
extern int lv_draw_sw_grad_color_calculate;
extern int lv_draw_sw_grad_get;
extern int lv_draw_sw_i1_convert_to_vtiled;
extern int lv_draw_sw_i1_invert;
extern int lv_draw_sw_i1_to_argb8888;
extern int lv_draw_sw_image;
extern int lv_draw_sw_init;
extern int lv_draw_sw_label;
extern int lv_draw_sw_layer;
extern int lv_draw_sw_letter;
extern int lv_draw_sw_line;
extern int lv_draw_sw_mask_angle_init;
extern int lv_draw_sw_mask_apply;
extern int lv_draw_sw_mask_cleanup;
extern int lv_draw_sw_mask_deinit;
extern int lv_draw_sw_mask_fade_init;
extern int lv_draw_sw_mask_free_param;
extern int lv_draw_sw_mask_init;
extern int lv_draw_sw_mask_line_angle_init;
extern int lv_draw_sw_mask_line_points_init;
extern int lv_draw_sw_mask_map_init;
extern int lv_draw_sw_mask_radius_init;
extern int lv_draw_sw_mask_rect;
extern int lv_draw_sw_register_blend_handler;
extern int lv_draw_sw_rgb565_swap;
extern int lv_draw_sw_rotate;
extern int lv_draw_sw_transform;
extern int lv_draw_sw_triangle;
extern int lv_draw_sw_unregister_blend_handler;
extern int lv_draw_task_get_arc_dsc;
extern int lv_draw_task_get_area;
extern int lv_draw_task_get_blur_dsc;
extern int lv_draw_task_get_border_dsc;
extern int lv_draw_task_get_box_shadow_dsc;
extern int lv_draw_task_get_draw_dsc;
extern int lv_draw_task_get_fill_dsc;
extern int lv_draw_task_get_image_dsc;
extern int lv_draw_task_get_label_dsc;
extern int lv_draw_task_get_line_dsc;
extern int lv_draw_task_get_mask_rect_dsc;
extern int lv_draw_task_get_triangle_dsc;
extern int lv_draw_task_get_type;
extern int lv_draw_triangle;
extern int lv_draw_triangle_dsc_init;
extern int lv_draw_unit_draw_letter;
extern int lv_draw_unit_send_event;
extern int lv_draw_wait_for_finish;
extern int lv_dropdown_add_option;
extern int lv_dropdown_bind_value;
extern int lv_dropdown_class;
extern int lv_dropdown_clear_options;
extern int lv_dropdown_close;
extern int lv_dropdown_create;
extern int lv_dropdown_get_dir;
extern int lv_dropdown_get_list;
extern int lv_dropdown_get_option_count;
extern int lv_dropdown_get_option_index;
extern int lv_dropdown_get_options;
extern int lv_dropdown_get_selected;
extern int lv_dropdown_get_selected_highlight;
extern int lv_dropdown_get_selected_str;
extern int lv_dropdown_get_symbol;
extern int lv_dropdown_get_text;
extern int lv_dropdown_is_open;
extern int lv_dropdown_open;
extern int lv_dropdown_set_dir;
extern int lv_dropdown_set_options;
extern int lv_dropdown_set_options_static;
extern int lv_dropdown_set_selected;
extern int lv_dropdown_set_selected_highlight;
extern int lv_dropdown_set_symbol;
extern int lv_dropdown_set_text;
extern int lv_dropdown_set_text_static;
extern int lv_dropdownlist_class;
extern int lv_event_add;
extern int lv_event_code_get_name;
extern int lv_event_dsc_get_cb;
extern int lv_event_dsc_get_user_data;
extern int lv_event_free_user_data_cb;
extern int lv_event_get_code;
extern int lv_event_get_count;
extern int lv_event_get_cover_area;
extern int lv_event_get_current_target;
extern int lv_event_get_current_target_obj;
extern int lv_event_get_draw_task;
extern int lv_event_get_dsc;
extern int lv_event_get_hit_test_info;
extern int lv_event_get_indev;
extern int lv_event_get_invalidated_area;
extern int lv_event_get_key;
extern int lv_event_get_layer;
extern int lv_event_get_old_size;
extern int lv_event_get_param;
extern int lv_event_get_prev_state;
extern int lv_event_get_rotary_diff;
extern int lv_event_get_scroll_anim;
extern int lv_event_get_self_size_info;
extern int lv_event_get_target;
extern int lv_event_get_target_obj;
extern int lv_event_get_user_data;
extern int lv_event_mark_deleted;
extern int lv_event_pop;
extern int lv_event_push;
extern int lv_event_push_and_send;
extern int lv_event_register_id;
extern int lv_event_remove;
extern int lv_event_remove_all;
extern int lv_event_remove_dsc;
extern int lv_event_send;
extern int lv_event_set_cover_res;
extern int lv_event_set_ext_draw_size;
extern int lv_event_stop_bubbling;
extern int lv_event_stop_processing;
extern int lv_event_stop_trickling;
extern int lv_flex_init;
extern int lv_font_get_bitmap_fmt_txt;
extern int lv_font_get_default;
extern int lv_font_get_glyph_bitmap;
extern int lv_font_get_glyph_dsc;
extern int lv_font_get_glyph_dsc_fmt_txt;
extern int lv_font_get_glyph_static_bitmap;
extern int lv_font_get_glyph_width;
extern int lv_font_get_line_height;
extern int lv_font_glyph_release_draw_data;
extern int lv_font_has_static_bitmap;
extern int lv_font_info_is_equal;
extern int lv_font_set_kerning;
extern int lv_free;
extern int lv_free_core;
extern int lv_fs_close;
extern int lv_fs_deinit;
extern int lv_fs_dir_close;
extern int lv_fs_dir_open;
extern int lv_fs_dir_read;
extern int lv_fs_drv_init;
extern int lv_fs_drv_register;
extern int lv_fs_get_buffer_from_path;
extern int lv_fs_get_drv;
extern int lv_fs_get_ext;
extern int lv_fs_get_last;
extern int lv_fs_get_letters;
extern int lv_fs_get_size;
extern int lv_fs_init;
extern int lv_fs_is_ready;
extern int lv_fs_load_to_buf;
extern int lv_fs_load_with_alloc;
extern int lv_fs_make_path_from_buffer;
extern int lv_fs_open;
extern int lv_fs_path_get_size;
extern int lv_fs_path_join;
extern int lv_fs_posix_init;
extern int lv_fs_read;
extern int lv_fs_remove_drive;
extern int lv_fs_seek;
extern int lv_fs_tell;
extern int lv_fs_up;
extern int lv_fs_write;
extern int lv_global;
extern int lv_grad_conical_init;
extern int lv_grad_horizontal_init;
extern int lv_grad_init_stops;
extern int lv_grad_linear_init;
extern int lv_grad_radial_init;
extern int lv_grad_radial_set_focal;
extern int lv_grad_vertical_init;
extern int lv_grid_fr;
extern int lv_grid_init;
extern int lv_group_add_obj;
extern int lv_group_by_index;
extern int lv_group_create;
extern int lv_group_deinit;
extern int lv_group_delete;
extern int lv_group_focus_freeze;
extern int lv_group_focus_next;
extern int lv_group_focus_obj;
extern int lv_group_focus_prev;
extern int lv_group_get_count;
extern int lv_group_get_default;
extern int lv_group_get_edge_cb;
extern int lv_group_get_editing;
extern int lv_group_get_focus_cb;
extern int lv_group_get_focused;
extern int lv_group_get_obj_by_index;
extern int lv_group_get_obj_count;
extern int lv_group_get_user_data;
extern int lv_group_get_wrap;
extern int lv_group_init;
extern int lv_group_remove_all_objs;
extern int lv_group_remove_obj;
extern int lv_group_send_data;
extern int lv_group_set_default;
extern int lv_group_set_edge_cb;
extern int lv_group_set_editing;
extern int lv_group_set_focus_cb;
extern int lv_group_set_refocus_policy;
extern int lv_group_set_user_data;
extern int lv_group_set_wrap;
extern int lv_group_swap_obj;
extern int lv_image_bind_src;
extern int lv_image_buf_free;
extern int lv_image_buf_get_transformed_area;
extern int lv_image_buf_set_palette;
extern int lv_image_cache_drop;
extern int lv_image_cache_dump;
extern int lv_image_cache_init;
extern int lv_image_cache_is_enabled;
extern int lv_image_cache_iter_create;
extern int lv_image_cache_resize;
extern int lv_image_class;
extern int lv_image_create;
extern int lv_image_decoder_add_to_cache;
extern int lv_image_decoder_close;
extern int lv_image_decoder_create;
extern int lv_image_decoder_deinit;
extern int lv_image_decoder_delete;
extern int lv_image_decoder_get_area;
extern int lv_image_decoder_get_info;
extern int lv_image_decoder_get_next;
extern int lv_image_decoder_init;
extern int lv_image_decoder_open;
extern int lv_image_decoder_post_process;
extern int lv_image_decoder_set_close_cb;
extern int lv_image_decoder_set_get_area_cb;
extern int lv_image_decoder_set_info_cb;
extern int lv_image_decoder_set_open_cb;
extern int lv_image_get_antialias;
extern int lv_image_get_bitmap_map_src;
extern int lv_image_get_blend_mode;
extern int lv_image_get_inner_align;
extern int lv_image_get_offset_x;
extern int lv_image_get_offset_y;
extern int lv_image_get_pivot;
extern int lv_image_get_rotation;
extern int lv_image_get_scale;
extern int lv_image_get_scale_x;
extern int lv_image_get_scale_y;
extern int lv_image_get_src;
extern int lv_image_get_src_height;
extern int lv_image_get_src_width;
extern int lv_image_get_transformed_height;
extern int lv_image_get_transformed_width;
extern int lv_image_header_cache_drop;
extern int lv_image_header_cache_dump;
extern int lv_image_header_cache_init;
extern int lv_image_header_cache_is_enabled;
extern int lv_image_header_cache_iter_create;
extern int lv_image_header_cache_resize;
extern int lv_image_set_antialias;
extern int lv_image_set_bitmap_map_src;
extern int lv_image_set_blend_mode;
extern int lv_image_set_inner_align;
extern int lv_image_set_offset_x;
extern int lv_image_set_offset_y;
extern int lv_image_set_pivot;
extern int lv_image_set_pivot_x;
extern int lv_image_set_pivot_y;
extern int lv_image_set_rotation;
extern int lv_image_set_scale;
extern int lv_image_set_scale_x;
extern int lv_image_set_scale_y;
extern int lv_image_set_src;
extern int lv_image_src_get_type;
extern int lv_imagebutton_class;
extern int lv_imagebutton_create;
extern int lv_imagebutton_get_src_left;
extern int lv_imagebutton_get_src_middle;
extern int lv_imagebutton_get_src_right;
extern int lv_imagebutton_set_src;
extern int lv_imagebutton_set_src_left;
extern int lv_imagebutton_set_src_mid;
extern int lv_imagebutton_set_src_right;
extern int lv_imagebutton_set_state;
extern int lv_indev_active;
extern int lv_indev_add_event_cb;
extern int lv_indev_create;
extern int lv_indev_delete;
extern int lv_indev_enable;
extern int lv_indev_find_scroll_obj;
extern int lv_indev_get_active_obj;
extern int lv_indev_get_cursor;
extern int lv_indev_get_display;
extern int lv_indev_get_driver_data;
extern int lv_indev_get_event_count;
extern int lv_indev_get_event_dsc;
extern int lv_indev_get_gesture_dir;
extern int lv_indev_get_group;
extern int lv_indev_get_key;
extern int lv_indev_get_mode;
extern int lv_indev_get_next;
extern int lv_indev_get_point;
extern int lv_indev_get_press_moved;
extern int lv_indev_get_read_cb;
extern int lv_indev_get_read_timer;
extern int lv_indev_get_scroll_dir;
extern int lv_indev_get_scroll_obj;
extern int lv_indev_get_short_click_streak;
extern int lv_indev_get_state;
extern int lv_indev_get_type;
extern int lv_indev_get_user_data;
extern int lv_indev_get_vect;
extern int lv_indev_read;
extern int lv_indev_read_timer_cb;
extern int lv_indev_remove_event;
extern int lv_indev_remove_event_cb_with_user_data;
extern int lv_indev_reset;
extern int lv_indev_reset_long_press;
extern int lv_indev_scroll_get_snap_dist;
extern int lv_indev_scroll_handler;
extern int lv_indev_scroll_throw_handler;
extern int lv_indev_scroll_throw_predict;
extern int lv_indev_search_obj;
extern int lv_indev_send_event;
extern int lv_indev_set_button_points;
extern int lv_indev_set_cursor;
extern int lv_indev_set_display;
extern int lv_indev_set_driver_data;
extern int lv_indev_set_gesture_min_distance;
extern int lv_indev_set_gesture_min_velocity;
extern int lv_indev_set_group;
extern int lv_indev_set_key_remap_cb;
extern int lv_indev_set_long_press_repeat_time;
extern int lv_indev_set_long_press_time;
extern int lv_indev_set_mode;
extern int lv_indev_set_read_cb;
extern int lv_indev_set_scroll_limit;
extern int lv_indev_set_scroll_throw;
extern int lv_indev_set_type;
extern int lv_indev_set_user_data;
extern int lv_indev_stop_processing;
extern int lv_indev_wait_release;
extern int lv_init;
extern int lv_inv_area;
extern int lv_is_initialized;
extern int lv_iter_create;
extern int lv_iter_destroy;
extern int lv_iter_get_context;
extern int lv_iter_inspect;
extern int lv_iter_make_peekable;
extern int lv_iter_next;
extern int lv_iter_peek;
extern int lv_iter_peek_advance;
extern int lv_iter_peek_reset;
extern int lv_keyboard_class;
extern int lv_keyboard_create;
extern int lv_keyboard_def_event_cb;
extern int lv_keyboard_get_button_text;
extern int lv_keyboard_get_map_array;
extern int lv_keyboard_get_mode;
extern int lv_keyboard_get_popovers;
extern int lv_keyboard_get_selected_button;
extern int lv_keyboard_get_textarea;
extern int lv_keyboard_set_map;
extern int lv_keyboard_set_mode;
extern int lv_keyboard_set_popovers;
extern int lv_keyboard_set_textarea;
extern int lv_label_bind_text;
extern int lv_label_class;
extern int lv_label_create;
extern int lv_label_cut_text;
extern int lv_label_get_letter_on;
extern int lv_label_get_letter_pos;
extern int lv_label_get_long_mode;
extern int lv_label_get_recolor;
extern int lv_label_get_text;
extern int lv_label_get_text_selection_end;
extern int lv_label_get_text_selection_start;
extern int lv_label_ins_text;
extern int lv_label_is_char_under_pos;
extern int lv_label_set_long_mode;
extern int lv_label_set_recolor;
extern int lv_label_set_text;
extern int lv_label_set_text_fmt;
extern int lv_label_set_text_selection_end;
extern int lv_label_set_text_selection_start;
extern int lv_label_set_text_static;
extern int lv_label_set_text_vfmt;
extern int lv_layer_bottom;
extern int lv_layer_init;
extern int lv_layer_reset;
extern int lv_layer_sys;
extern int lv_layer_top;
extern int lv_layout_apply;
extern int lv_layout_create;
extern int lv_layout_deinit;
extern int lv_layout_get_min_size;
extern int lv_layout_init;
extern int lv_layout_register;
extern int lv_led_class;
extern int lv_led_create;
extern int lv_led_get_brightness;
extern int lv_led_get_color;
extern int lv_led_off;
extern int lv_led_on;
extern int lv_led_set_brightness;
extern int lv_led_set_color;
extern int lv_led_toggle;
extern int lv_line_class;
extern int lv_line_create;
extern int lv_line_get_point_count;
extern int lv_line_get_points;
extern int lv_line_get_points_mutable;
extern int lv_line_get_y_invert;
extern int lv_line_is_point_array_mutable;
extern int lv_line_set_points;
extern int lv_line_set_points_mutable;
extern int lv_line_set_y_invert;
extern int lv_list_add_button;
extern int lv_list_add_text;
extern int lv_list_button_class;
extern int lv_list_class;
extern int lv_list_create;
extern int lv_list_get_button_text;
extern int lv_list_set_button_text;
extern int lv_list_text_class;
extern int lv_ll_chg_list;
extern int lv_ll_clear;
extern int lv_ll_clear_custom;
extern int lv_ll_get_head;
extern int lv_ll_get_len;
extern int lv_ll_get_next;
extern int lv_ll_get_prev;
extern int lv_ll_get_tail;
extern int lv_ll_init;
extern int lv_ll_ins_head;
extern int lv_ll_ins_prev;
extern int lv_ll_ins_tail;
extern int lv_ll_is_empty;
extern int lv_ll_move_before;
extern int lv_ll_remove;
extern int lv_lock;
extern int lv_lock_isr;
extern int lv_lodepng_deinit;
extern int lv_lodepng_init;
extern int lv_lru_create;
extern int lv_lru_delete;
extern int lv_lru_get;
extern int lv_lru_remove;
extern int lv_lru_remove_lru_item;
extern int lv_lru_set;
extern int lv_malloc;
extern int lv_malloc_core;
extern int lv_malloc_zeroed;
extern int lv_map;
extern int lv_mem_add_pool;
extern int lv_mem_deinit;
extern int lv_mem_init;
extern int lv_mem_monitor;
extern int lv_mem_monitor_core;
extern int lv_mem_remove_pool;
extern int lv_mem_test;
extern int lv_mem_test_core;
extern int lv_memcmp;
extern int lv_memcpy;
extern int lv_memmove;
extern int lv_memset;
extern int lv_menu_back_button_is_root;
extern int lv_menu_class;
extern int lv_menu_clear_history;
extern int lv_menu_cont_class;
extern int lv_menu_cont_create;
extern int lv_menu_create;
extern int lv_menu_get_cur_main_page;
extern int lv_menu_get_cur_sidebar_page;
extern int lv_menu_get_main_header;
extern int lv_menu_get_main_header_back_button;
extern int lv_menu_get_mode_header;
extern int lv_menu_get_mode_root_back_button;
extern int lv_menu_get_sidebar_header;
extern int lv_menu_get_sidebar_header_back_button;
extern int lv_menu_main_cont_class;
extern int lv_menu_main_header_cont_class;
extern int lv_menu_page_class;
extern int lv_menu_page_create;
extern int lv_menu_section_class;
extern int lv_menu_section_create;
extern int lv_menu_separator_class;
extern int lv_menu_separator_create;
extern int lv_menu_set_load_page_event;
extern int lv_menu_set_mode_header;
extern int lv_menu_set_mode_root_back_button;
extern int lv_menu_set_page;
extern int lv_menu_set_page_title;
extern int lv_menu_set_page_title_static;
extern int lv_menu_set_sidebar_page;
extern int lv_menu_sidebar_cont_class;
extern int lv_menu_sidebar_header_cont_class;
extern int lv_msgbox_add_close_button;
extern int lv_msgbox_add_footer_button;
extern int lv_msgbox_add_header_button;
extern int lv_msgbox_add_text;
extern int lv_msgbox_add_text_fmt;
extern int lv_msgbox_add_title;
extern int lv_msgbox_backdrop_class;
extern int lv_msgbox_class;
extern int lv_msgbox_close;
extern int lv_msgbox_close_async;
extern int lv_msgbox_content_class;
extern int lv_msgbox_create;
extern int lv_msgbox_footer_button_class;
extern int lv_msgbox_footer_class;
extern int lv_msgbox_get_content;
extern int lv_msgbox_get_footer;
extern int lv_msgbox_get_header;
extern int lv_msgbox_get_title;
extern int lv_msgbox_header_button_class;
extern int lv_msgbox_header_class;
extern int lv_obj_add_event_cb;
extern int lv_obj_add_flag;
extern int lv_obj_add_play_timeline_event;
extern int lv_obj_add_screen_create_event;
extern int lv_obj_add_screen_load_event;
extern int lv_obj_add_state;
extern int lv_obj_add_style;
extern int lv_obj_add_subject_increment_event;
extern int lv_obj_add_subject_set_int_event;
extern int lv_obj_add_subject_set_string_event;
extern int lv_obj_add_subject_toggle_event;
extern int lv_obj_align;
extern int lv_obj_align_to;
extern int lv_obj_allocate_spec_attr;
extern int lv_obj_area_is_visible;
extern int lv_obj_bind_checked;
extern int lv_obj_bind_flag_if_eq;
extern int lv_obj_bind_flag_if_ge;
extern int lv_obj_bind_flag_if_gt;
extern int lv_obj_bind_flag_if_le;
extern int lv_obj_bind_flag_if_lt;
extern int lv_obj_bind_flag_if_not_eq;
extern int lv_obj_bind_state_if_eq;
extern int lv_obj_bind_state_if_ge;
extern int lv_obj_bind_state_if_gt;
extern int lv_obj_bind_state_if_le;
extern int lv_obj_bind_state_if_lt;
extern int lv_obj_bind_state_if_not_eq;
extern int lv_obj_bind_style;
extern int lv_obj_bind_style_prop;
extern int lv_obj_calc_dynamic_height;
extern int lv_obj_calc_dynamic_width;
extern int lv_obj_calculate_ext_draw_size;
extern int lv_obj_calculate_style_text_align;
extern int lv_obj_center;
extern int lv_obj_check_type;
extern int lv_obj_class;
extern int lv_obj_class_create_obj;
extern int lv_obj_class_init_obj;
extern int lv_obj_clean;
extern int lv_obj_create;
extern int lv_obj_delete;
extern int lv_obj_delete_anim_completed_cb;
extern int lv_obj_delete_async;
extern int lv_obj_delete_delayed;
extern int lv_obj_destruct;
extern int lv_obj_dump_tree;
extern int lv_obj_enable_style_refresh;
extern int lv_obj_event_base;
extern int lv_obj_fade_in;
extern int lv_obj_fade_out;
extern int lv_obj_get_child;
extern int lv_obj_get_child_by_type;
extern int lv_obj_get_child_count;
extern int lv_obj_get_child_count_by_type;
extern int lv_obj_get_class;
extern int lv_obj_get_click_area;
extern int lv_obj_get_content_coords;
extern int lv_obj_get_content_height;
extern int lv_obj_get_content_width;
extern int lv_obj_get_coords;
extern int lv_obj_get_display;
extern int lv_obj_get_event_count;
extern int lv_obj_get_event_dsc;
extern int lv_obj_get_ext_draw_size;
extern int lv_obj_get_group;
extern int lv_obj_get_height;
extern int lv_obj_get_index;
extern int lv_obj_get_index_by_type;
extern int lv_obj_get_layer_type;
extern int lv_obj_get_local_style_prop;
extern int lv_obj_get_parent;
extern int lv_obj_get_screen;
extern int lv_obj_get_scroll_bottom;
extern int lv_obj_get_scroll_dir;
extern int lv_obj_get_scroll_end;
extern int lv_obj_get_scroll_left;
extern int lv_obj_get_scroll_right;
extern int lv_obj_get_scroll_snap_x;
extern int lv_obj_get_scroll_snap_y;
extern int lv_obj_get_scroll_top;
extern int lv_obj_get_scroll_x;
extern int lv_obj_get_scroll_y;
extern int lv_obj_get_scrollbar_area;
extern int lv_obj_get_scrollbar_mode;
extern int lv_obj_get_self_height;
extern int lv_obj_get_self_width;
extern int lv_obj_get_sibling;
extern int lv_obj_get_sibling_by_type;
extern int lv_obj_get_state;
extern int lv_obj_get_style_clamped_height;
extern int lv_obj_get_style_clamped_width;
extern int lv_obj_get_style_opa_recursive;
extern int lv_obj_get_style_prop;
extern int lv_obj_get_style_recolor_recursive;
extern int lv_obj_get_transform;
extern int lv_obj_get_transformed_area;
extern int lv_obj_get_user_data;
extern int lv_obj_get_width;
extern int lv_obj_get_x;
extern int lv_obj_get_x2;
extern int lv_obj_get_x_aligned;
extern int lv_obj_get_y;
extern int lv_obj_get_y2;
extern int lv_obj_get_y_aligned;
extern int lv_obj_has_class;
extern int lv_obj_has_flag;
extern int lv_obj_has_flag_any;
extern int lv_obj_has_state;
extern int lv_obj_has_style_prop;
extern int lv_obj_hit_test;
extern int lv_obj_init_draw_arc_dsc;
extern int lv_obj_init_draw_blur_dsc;
extern int lv_obj_init_draw_image_dsc;
extern int lv_obj_init_draw_label_dsc;
extern int lv_obj_init_draw_line_dsc;
extern int lv_obj_init_draw_rect_dsc;
extern int lv_obj_invalidate;
extern int lv_obj_invalidate_area;
extern int lv_obj_is_editable;
extern int lv_obj_is_group_def;
extern int lv_obj_is_height_max;
extern int lv_obj_is_height_min;
extern int lv_obj_is_layout_positioned;
extern int lv_obj_is_radio_button;
extern int lv_obj_is_scrolling;
extern int lv_obj_is_valid;
extern int lv_obj_is_visible;
extern int lv_obj_is_width_max;
extern int lv_obj_is_width_min;
extern int lv_obj_mark_layout_as_dirty;
extern int lv_obj_move_children_by;
extern int lv_obj_move_to;
extern int lv_obj_move_to_index;
extern int lv_obj_null_on_delete;
extern int lv_obj_readjust_scroll;
extern int lv_obj_redraw;
extern int lv_obj_refr;
extern int lv_obj_refr_pos;
extern int lv_obj_refr_size;
extern int lv_obj_refresh_ext_draw_size;
extern int lv_obj_refresh_self_size;
extern int lv_obj_refresh_style;
extern int lv_obj_remove_event;
extern int lv_obj_remove_event_cb;
extern int lv_obj_remove_event_cb_with_user_data;
extern int lv_obj_remove_event_dsc;
extern int lv_obj_remove_flag;
extern int lv_obj_remove_from_subject;
extern int lv_obj_remove_local_style_prop;
extern int lv_obj_remove_state;
extern int lv_obj_remove_style;
extern int lv_obj_remove_style_all;
extern int lv_obj_remove_theme;
extern int lv_obj_replace_style;
extern int lv_obj_report_style_change;
extern int lv_obj_reset_transform;
extern int lv_obj_scroll_by;
extern int lv_obj_scroll_by_bounded;
extern int lv_obj_scroll_by_raw;
extern int lv_obj_scroll_to;
extern int lv_obj_scroll_to_view;
extern int lv_obj_scroll_to_view_recursive;
extern int lv_obj_scroll_to_x;
extern int lv_obj_scroll_to_y;
extern int lv_obj_scrollbar_invalidate;
extern int lv_obj_send_event;
extern int lv_obj_set_align;
extern int lv_obj_set_content_height;
extern int lv_obj_set_content_width;
extern int lv_obj_set_ext_click_area;
extern int lv_obj_set_flag;
extern int lv_obj_set_flex_align;
extern int lv_obj_set_flex_flow;
extern int lv_obj_set_flex_grow;
extern int lv_obj_set_grid_align;
extern int lv_obj_set_grid_cell;
extern int lv_obj_set_grid_dsc_array;
extern int lv_obj_set_height;
extern int lv_obj_set_layout;
extern int lv_obj_set_local_style_prop;
extern int lv_obj_set_parent;
extern int lv_obj_set_pos;
extern int lv_obj_set_radio_button;
extern int lv_obj_set_scroll_dir;
extern int lv_obj_set_scroll_snap_x;
extern int lv_obj_set_scroll_snap_y;
extern int lv_obj_set_scrollbar_mode;
extern int lv_obj_set_size;
extern int lv_obj_set_state;
extern int lv_obj_set_style_align;
extern int lv_obj_set_style_anim;
extern int lv_obj_set_style_anim_duration;
extern int lv_obj_set_style_arc_color;
extern int lv_obj_set_style_arc_image_src;
extern int lv_obj_set_style_arc_opa;
extern int lv_obj_set_style_arc_rounded;
extern int lv_obj_set_style_arc_width;
extern int lv_obj_set_style_base_dir;
extern int lv_obj_set_style_bg_color;
extern int lv_obj_set_style_bg_grad;
extern int lv_obj_set_style_bg_grad_color;
extern int lv_obj_set_style_bg_grad_dir;
extern int lv_obj_set_style_bg_grad_opa;
extern int lv_obj_set_style_bg_grad_stop;
extern int lv_obj_set_style_bg_image_opa;
extern int lv_obj_set_style_bg_image_recolor;
extern int lv_obj_set_style_bg_image_recolor_opa;
extern int lv_obj_set_style_bg_image_src;
extern int lv_obj_set_style_bg_image_tiled;
extern int lv_obj_set_style_bg_main_opa;
extern int lv_obj_set_style_bg_main_stop;
extern int lv_obj_set_style_bg_opa;
extern int lv_obj_set_style_bitmap_mask_src;
extern int lv_obj_set_style_blend_mode;
extern int lv_obj_set_style_blur_backdrop;
extern int lv_obj_set_style_blur_quality;
extern int lv_obj_set_style_blur_radius;
extern int lv_obj_set_style_border_color;
extern int lv_obj_set_style_border_opa;
extern int lv_obj_set_style_border_post;
extern int lv_obj_set_style_border_side;
extern int lv_obj_set_style_border_width;
extern int lv_obj_set_style_clip_corner;
extern int lv_obj_set_style_color_filter_dsc;
extern int lv_obj_set_style_color_filter_opa;
extern int lv_obj_set_style_drop_shadow_color;
extern int lv_obj_set_style_drop_shadow_offset_x;
extern int lv_obj_set_style_drop_shadow_offset_y;
extern int lv_obj_set_style_drop_shadow_opa;
extern int lv_obj_set_style_drop_shadow_quality;
extern int lv_obj_set_style_drop_shadow_radius;
extern int lv_obj_set_style_flex_cross_place;
extern int lv_obj_set_style_flex_flow;
extern int lv_obj_set_style_flex_grow;
extern int lv_obj_set_style_flex_main_place;
extern int lv_obj_set_style_flex_track_place;
extern int lv_obj_set_style_grid_cell_column_pos;
extern int lv_obj_set_style_grid_cell_column_span;
extern int lv_obj_set_style_grid_cell_row_pos;
extern int lv_obj_set_style_grid_cell_row_span;
extern int lv_obj_set_style_grid_cell_x_align;
extern int lv_obj_set_style_grid_cell_y_align;
extern int lv_obj_set_style_grid_column_align;
extern int lv_obj_set_style_grid_column_dsc_array;
extern int lv_obj_set_style_grid_row_align;
extern int lv_obj_set_style_grid_row_dsc_array;
extern int lv_obj_set_style_height;
extern int lv_obj_set_style_image_colorkey;
extern int lv_obj_set_style_image_opa;
extern int lv_obj_set_style_image_recolor;
extern int lv_obj_set_style_image_recolor_opa;
extern int lv_obj_set_style_layout;
extern int lv_obj_set_style_length;
extern int lv_obj_set_style_line_color;
extern int lv_obj_set_style_line_dash_gap;
extern int lv_obj_set_style_line_dash_width;
extern int lv_obj_set_style_line_opa;
extern int lv_obj_set_style_line_rounded;
extern int lv_obj_set_style_line_width;
extern int lv_obj_set_style_margin_bottom;
extern int lv_obj_set_style_margin_left;
extern int lv_obj_set_style_margin_right;
extern int lv_obj_set_style_margin_top;
extern int lv_obj_set_style_max_height;
extern int lv_obj_set_style_max_width;
extern int lv_obj_set_style_min_height;
extern int lv_obj_set_style_min_width;
extern int lv_obj_set_style_opa;
extern int lv_obj_set_style_opa_layered;
extern int lv_obj_set_style_outline_color;
extern int lv_obj_set_style_outline_opa;
extern int lv_obj_set_style_outline_pad;
extern int lv_obj_set_style_outline_width;
extern int lv_obj_set_style_pad_bottom;
extern int lv_obj_set_style_pad_column;
extern int lv_obj_set_style_pad_left;
extern int lv_obj_set_style_pad_radial;
extern int lv_obj_set_style_pad_right;
extern int lv_obj_set_style_pad_row;
extern int lv_obj_set_style_pad_top;
extern int lv_obj_set_style_radial_offset;
extern int lv_obj_set_style_radius;
extern int lv_obj_set_style_recolor;
extern int lv_obj_set_style_recolor_opa;
extern int lv_obj_set_style_rotary_sensitivity;
extern int lv_obj_set_style_shadow_color;
extern int lv_obj_set_style_shadow_offset_x;
extern int lv_obj_set_style_shadow_offset_y;
extern int lv_obj_set_style_shadow_opa;
extern int lv_obj_set_style_shadow_spread;
extern int lv_obj_set_style_shadow_width;
extern int lv_obj_set_style_text_align;
extern int lv_obj_set_style_text_color;
extern int lv_obj_set_style_text_decor;
extern int lv_obj_set_style_text_font;
extern int lv_obj_set_style_text_letter_space;
extern int lv_obj_set_style_text_line_space;
extern int lv_obj_set_style_text_opa;
extern int lv_obj_set_style_text_outline_stroke_color;
extern int lv_obj_set_style_text_outline_stroke_opa;
extern int lv_obj_set_style_text_outline_stroke_width;
extern int lv_obj_set_style_transform_height;
extern int lv_obj_set_style_transform_pivot_x;
extern int lv_obj_set_style_transform_pivot_y;
extern int lv_obj_set_style_transform_rotation;
extern int lv_obj_set_style_transform_scale_x;
extern int lv_obj_set_style_transform_scale_y;
extern int lv_obj_set_style_transform_skew_x;
extern int lv_obj_set_style_transform_skew_y;
extern int lv_obj_set_style_transform_width;
extern int lv_obj_set_style_transition;
extern int lv_obj_set_style_translate_radial;
extern int lv_obj_set_style_translate_x;
extern int lv_obj_set_style_translate_y;
extern int lv_obj_set_style_width;
extern int lv_obj_set_style_x;
extern int lv_obj_set_style_y;
extern int lv_obj_set_subject_increment_event_max_value;
extern int lv_obj_set_subject_increment_event_min_value;
extern int lv_obj_set_subject_increment_event_rollover;
extern int lv_obj_set_transform;
extern int lv_obj_set_user_data;
extern int lv_obj_set_width;
extern int lv_obj_set_x;
extern int lv_obj_set_y;
extern int lv_obj_stop_scroll_anim;
extern int lv_obj_style_apply_color_filter;
extern int lv_obj_style_apply_recolor;
extern int lv_obj_style_create_transition;
extern int lv_obj_style_deinit;
extern int lv_obj_style_get_disabled;
extern int lv_obj_style_init;
extern int lv_obj_style_set_disabled;
extern int lv_obj_style_state_compare;
extern int lv_obj_swap;
extern int lv_obj_transform_point;
extern int lv_obj_transform_point_array;
extern int lv_obj_tree_walk;
extern int lv_obj_update_layer_type;
extern int lv_obj_update_layout;
extern int lv_obj_update_snap;
extern int lv_observer_get_target;
extern int lv_observer_get_target_obj;
extern int lv_observer_get_user_data;
extern int lv_observer_remove;
extern int lv_os_get_idle_percent;
extern int lv_os_init;
extern int lv_palette_darken;
extern int lv_palette_lighten;
extern int lv_palette_main;
extern int lv_pct;
extern int lv_pct_to_px;
extern int lv_pending_add;
extern int lv_pending_create;
extern int lv_pending_destroy;
extern int lv_pending_remove_all;
extern int lv_pending_set_free_cb;
extern int lv_pending_swap;
extern int lv_point_array_transform;
extern int lv_point_from_precise;
extern int lv_point_precise_set;
extern int lv_point_precise_swap;
extern int lv_point_set;
extern int lv_point_swap;
extern int lv_point_to_precise;
extern int lv_point_transform;
extern int lv_pow;
extern int lv_qrcode_class;
extern int lv_qrcode_create;
extern int lv_qrcode_set_dark_color;
extern int lv_qrcode_set_data;
extern int lv_qrcode_set_light_color;
extern int lv_qrcode_set_quiet_zone;
extern int lv_qrcode_set_size;
extern int lv_qrcode_update;
extern int lv_rand;
extern int lv_rand_set_seed;
extern int lv_rb_destroy;
extern int lv_rb_drop;
extern int lv_rb_drop_node;
extern int lv_rb_find;
extern int lv_rb_init;
extern int lv_rb_insert;
extern int lv_rb_maximum;
extern int lv_rb_maximum_from;
extern int lv_rb_minimum;
extern int lv_rb_minimum_from;
extern int lv_rb_remove;
extern int lv_rb_remove_node;
extern int lv_realloc;
extern int lv_realloc_core;
extern int lv_reallocf;
extern int lv_refr_deinit;
extern int lv_refr_get_disp_refreshing;
extern int lv_refr_get_top_obj;
extern int lv_refr_init;
extern int lv_refr_now;
extern int lv_refr_set_disp_refreshing;
extern int lv_roller_bind_value;
extern int lv_roller_class;
extern int lv_roller_create;
extern int lv_roller_get_option_count;
extern int lv_roller_get_option_str;
extern int lv_roller_get_options;
extern int lv_roller_get_selected;
extern int lv_roller_get_selected_str;
extern int lv_roller_label_class;
extern int lv_roller_set_options;
extern int lv_roller_set_selected;
extern int lv_roller_set_selected_str;
extern int lv_roller_set_visible_row_count;
extern int lv_scale_add_section;
extern int lv_scale_bind_section_max_value;
extern int lv_scale_bind_section_min_value;
extern int lv_scale_class;
extern int lv_scale_create;
extern int lv_scale_get_angle_range;
extern int lv_scale_get_label_show;
extern int lv_scale_get_major_tick_every;
extern int lv_scale_get_mode;
extern int lv_scale_get_range_max_value;
extern int lv_scale_get_range_min_value;
extern int lv_scale_get_rotation;
extern int lv_scale_get_total_tick_count;
extern int lv_scale_section_set_range;
extern int lv_scale_section_set_style;
extern int lv_scale_set_angle_range;
extern int lv_scale_set_draw_ticks_on_top;
extern int lv_scale_set_image_needle_value;
extern int lv_scale_set_label_show;
extern int lv_scale_set_line_needle_value;
extern int lv_scale_set_major_tick_every;
extern int lv_scale_set_max_value;
extern int lv_scale_set_min_value;
extern int lv_scale_set_mode;
extern int lv_scale_set_post_draw;
extern int lv_scale_set_range;
extern int lv_scale_set_rotation;
extern int lv_scale_set_section_max_value;
extern int lv_scale_set_section_min_value;
extern int lv_scale_set_section_range;
extern int lv_scale_set_section_style_indicator;
extern int lv_scale_set_section_style_items;
extern int lv_scale_set_section_style_main;
extern int lv_scale_set_text_src;
extern int lv_scale_set_total_tick_count;
extern int lv_screen_active;
extern int lv_screen_load;
extern int lv_screen_load_anim;
extern int lv_sleep_ms;
extern int lv_slider_bind_value;
extern int lv_slider_class;
extern int lv_slider_create;
extern int lv_slider_get_left_value;
extern int lv_slider_get_max_value;
extern int lv_slider_get_min_value;
extern int lv_slider_get_mode;
extern int lv_slider_get_orientation;
extern int lv_slider_get_value;
extern int lv_slider_is_dragged;
extern int lv_slider_is_symmetrical;
extern int lv_slider_set_max_value;
extern int lv_slider_set_min_value;
extern int lv_slider_set_mode;
extern int lv_slider_set_orientation;
extern int lv_slider_set_range;
extern int lv_slider_set_start_value;
extern int lv_slider_set_value;
extern int lv_snapshot_create_draw_buf;
extern int lv_snapshot_free;
extern int lv_snapshot_reshape_draw_buf;
extern int lv_snapshot_take;
extern int lv_snapshot_take_to_buf;
extern int lv_snapshot_take_to_draw_buf;
extern int lv_snprintf;
extern int lv_span_get_style;
extern int lv_span_get_text;
extern int lv_span_set_text;
extern int lv_span_set_text_fmt;
extern int lv_span_set_text_static;
extern int lv_span_stack_deinit;
extern int lv_span_stack_init;
extern int lv_spangroup_add_span;
extern int lv_spangroup_bind_span_text;
extern int lv_spangroup_class;
extern int lv_spangroup_create;
extern int lv_spangroup_delete_span;
extern int lv_spangroup_get_align;
extern int lv_spangroup_get_child;
extern int lv_spangroup_get_expand_height;
extern int lv_spangroup_get_expand_width;
extern int lv_spangroup_get_indent;
extern int lv_spangroup_get_max_line_height;
extern int lv_spangroup_get_max_lines;
extern int lv_spangroup_get_mode;
extern int lv_spangroup_get_overflow;
extern int lv_spangroup_get_span_by_point;
extern int lv_spangroup_get_span_coords;
extern int lv_spangroup_get_span_count;
extern int lv_spangroup_refresh;
extern int lv_spangroup_set_align;
extern int lv_spangroup_set_indent;
extern int lv_spangroup_set_max_lines;
extern int lv_spangroup_set_mode;
extern int lv_spangroup_set_overflow;
extern int lv_spangroup_set_span_style;
extern int lv_spangroup_set_span_text;
extern int lv_spangroup_set_span_text_fmt;
extern int lv_spangroup_set_span_text_static;
extern int lv_spinbox_bind_value;
extern int lv_spinbox_class;
extern int lv_spinbox_create;
extern int lv_spinbox_decrement;
extern int lv_spinbox_get_dec_point_pos;
extern int lv_spinbox_get_digit_count;
extern int lv_spinbox_get_digit_step_direction;
extern int lv_spinbox_get_max_value;
extern int lv_spinbox_get_min_value;
extern int lv_spinbox_get_rollover;
extern int lv_spinbox_get_step;
extern int lv_spinbox_get_value;
extern int lv_spinbox_increment;
extern int lv_spinbox_set_cursor_pos;
extern int lv_spinbox_set_dec_point_pos;
extern int lv_spinbox_set_digit_count;
extern int lv_spinbox_set_digit_format;
extern int lv_spinbox_set_digit_step_direction;
extern int lv_spinbox_set_max_value;
extern int lv_spinbox_set_min_value;
extern int lv_spinbox_set_range;
extern int lv_spinbox_set_rollover;
extern int lv_spinbox_set_step;
extern int lv_spinbox_set_value;
extern int lv_spinbox_step_next;
extern int lv_spinbox_step_prev;
extern int lv_spinner_class;
extern int lv_spinner_create;
extern int lv_spinner_get_anim_duration;
extern int lv_spinner_get_arc_sweep;
extern int lv_spinner_set_anim_duration;
extern int lv_spinner_set_anim_params;
extern int lv_spinner_set_arc_sweep;
extern int lv_sqrt;
extern int lv_sqrt32;
extern int lv_strcat;
extern int lv_strchr;
extern int lv_strcmp;
extern int lv_strcpy;
extern int lv_strdup;
extern int lv_strlcpy;
extern int lv_strlen;
extern int lv_strncat;
extern int lv_strncmp;
extern int lv_strncpy;
extern int lv_strndup;
extern int lv_strnlen;
extern int lv_style_builtin_prop_flag_lookup_table;
extern int lv_style_const_prop_id_inv;
extern int lv_style_copy;
extern int lv_style_get_num_custom_props;
extern int lv_style_get_prop;
extern int lv_style_init;
extern int lv_style_is_empty;
extern int lv_style_merge;
extern int lv_style_prop_get_default;
extern int lv_style_prop_lookup_flags;
extern int lv_style_register_prop;
extern int lv_style_remove_prop;
extern int lv_style_reset;
extern int lv_style_set_align;
extern int lv_style_set_anim;
extern int lv_style_set_anim_duration;
extern int lv_style_set_arc_color;
extern int lv_style_set_arc_image_src;
extern int lv_style_set_arc_opa;
extern int lv_style_set_arc_rounded;
extern int lv_style_set_arc_width;
extern int lv_style_set_base_dir;
extern int lv_style_set_bg_color;
extern int lv_style_set_bg_grad;
extern int lv_style_set_bg_grad_color;
extern int lv_style_set_bg_grad_dir;
extern int lv_style_set_bg_grad_opa;
extern int lv_style_set_bg_grad_stop;
extern int lv_style_set_bg_image_opa;
extern int lv_style_set_bg_image_recolor;
extern int lv_style_set_bg_image_recolor_opa;
extern int lv_style_set_bg_image_src;
extern int lv_style_set_bg_image_tiled;
extern int lv_style_set_bg_main_opa;
extern int lv_style_set_bg_main_stop;
extern int lv_style_set_bg_opa;
extern int lv_style_set_bitmap_mask_src;
extern int lv_style_set_blend_mode;
extern int lv_style_set_blur_backdrop;
extern int lv_style_set_blur_quality;
extern int lv_style_set_blur_radius;
extern int lv_style_set_border_color;
extern int lv_style_set_border_opa;
extern int lv_style_set_border_post;
extern int lv_style_set_border_side;
extern int lv_style_set_border_width;
extern int lv_style_set_clip_corner;
extern int lv_style_set_color_filter_dsc;
extern int lv_style_set_color_filter_opa;
extern int lv_style_set_drop_shadow_color;
extern int lv_style_set_drop_shadow_offset_x;
extern int lv_style_set_drop_shadow_offset_y;
extern int lv_style_set_drop_shadow_opa;
extern int lv_style_set_drop_shadow_quality;
extern int lv_style_set_drop_shadow_radius;
extern int lv_style_set_flex_cross_place;
extern int lv_style_set_flex_flow;
extern int lv_style_set_flex_grow;
extern int lv_style_set_flex_main_place;
extern int lv_style_set_flex_track_place;
extern int lv_style_set_grid_cell_column_pos;
extern int lv_style_set_grid_cell_column_span;
extern int lv_style_set_grid_cell_row_pos;
extern int lv_style_set_grid_cell_row_span;
extern int lv_style_set_grid_cell_x_align;
extern int lv_style_set_grid_cell_y_align;
extern int lv_style_set_grid_column_align;
extern int lv_style_set_grid_column_dsc_array;
extern int lv_style_set_grid_row_align;
extern int lv_style_set_grid_row_dsc_array;
extern int lv_style_set_height;
extern int lv_style_set_image_colorkey;
extern int lv_style_set_image_opa;
extern int lv_style_set_image_recolor;
extern int lv_style_set_image_recolor_opa;
extern int lv_style_set_layout;
extern int lv_style_set_length;
extern int lv_style_set_line_color;
extern int lv_style_set_line_dash_gap;
extern int lv_style_set_line_dash_width;
extern int lv_style_set_line_opa;
extern int lv_style_set_line_rounded;
extern int lv_style_set_line_width;
extern int lv_style_set_margin_bottom;
extern int lv_style_set_margin_left;
extern int lv_style_set_margin_right;
extern int lv_style_set_margin_top;
extern int lv_style_set_max_height;
extern int lv_style_set_max_width;
extern int lv_style_set_min_height;
extern int lv_style_set_min_width;
extern int lv_style_set_opa;
extern int lv_style_set_opa_layered;
extern int lv_style_set_outline_color;
extern int lv_style_set_outline_opa;
extern int lv_style_set_outline_pad;
extern int lv_style_set_outline_width;
extern int lv_style_set_pad_bottom;
extern int lv_style_set_pad_column;
extern int lv_style_set_pad_left;
extern int lv_style_set_pad_radial;
extern int lv_style_set_pad_right;
extern int lv_style_set_pad_row;
extern int lv_style_set_pad_top;
extern int lv_style_set_prop;
extern int lv_style_set_radial_offset;
extern int lv_style_set_radius;
extern int lv_style_set_recolor;
extern int lv_style_set_recolor_opa;
extern int lv_style_set_rotary_sensitivity;
extern int lv_style_set_shadow_color;
extern int lv_style_set_shadow_offset_x;
extern int lv_style_set_shadow_offset_y;
extern int lv_style_set_shadow_opa;
extern int lv_style_set_shadow_spread;
extern int lv_style_set_shadow_width;
extern int lv_style_set_text_align;
extern int lv_style_set_text_color;
extern int lv_style_set_text_decor;
extern int lv_style_set_text_font;
extern int lv_style_set_text_letter_space;
extern int lv_style_set_text_line_space;
extern int lv_style_set_text_opa;
extern int lv_style_set_text_outline_stroke_color;
extern int lv_style_set_text_outline_stroke_opa;
extern int lv_style_set_text_outline_stroke_width;
extern int lv_style_set_transform_height;
extern int lv_style_set_transform_pivot_x;
extern int lv_style_set_transform_pivot_y;
extern int lv_style_set_transform_rotation;
extern int lv_style_set_transform_scale_x;
extern int lv_style_set_transform_scale_y;
extern int lv_style_set_transform_skew_x;
extern int lv_style_set_transform_skew_y;
extern int lv_style_set_transform_width;
extern int lv_style_set_transition;
extern int lv_style_set_translate_radial;
extern int lv_style_set_translate_x;
extern int lv_style_set_translate_y;
extern int lv_style_set_width;
extern int lv_style_set_x;
extern int lv_style_set_y;
extern int lv_style_transition_dsc_init;
extern int lv_subject_add_observer;
extern int lv_subject_add_observer_obj;
extern int lv_subject_add_observer_with_target;
extern int lv_subject_copy_string;
extern int lv_subject_deinit;
extern int lv_subject_get_color;
extern int lv_subject_get_group_element;
extern int lv_subject_get_int;
extern int lv_subject_get_pointer;
extern int lv_subject_get_previous_color;
extern int lv_subject_get_previous_int;
extern int lv_subject_get_previous_pointer;
extern int lv_subject_get_previous_string;
extern int lv_subject_get_string;
extern int lv_subject_init_color;
extern int lv_subject_init_group;
extern int lv_subject_init_int;
extern int lv_subject_init_pointer;
extern int lv_subject_init_string;
extern int lv_subject_notify;
extern int lv_subject_set_color;
extern int lv_subject_set_int;
extern int lv_subject_set_max_value_int;
extern int lv_subject_set_min_value_int;
extern int lv_subject_set_pointer;
extern int lv_subject_snprintf;
extern int lv_switch_class;
extern int lv_switch_create;
extern int lv_switch_get_orientation;
extern int lv_switch_set_orientation;
extern int lv_sysmon_builtin_deinit;
extern int lv_sysmon_builtin_init;
extern int lv_sysmon_create;
extern int lv_table_class;
extern int lv_table_clear_cell_ctrl;
extern int lv_table_create;
extern int lv_table_get_cell_user_data;
extern int lv_table_get_cell_value;
extern int lv_table_get_column_count;
extern int lv_table_get_column_width;
extern int lv_table_get_row_count;
extern int lv_table_get_selected_cell;
extern int lv_table_has_cell_ctrl;
extern int lv_table_set_cell_ctrl;
extern int lv_table_set_cell_user_data;
extern int lv_table_set_cell_value;
extern int lv_table_set_cell_value_fmt;
extern int lv_table_set_column_count;
extern int lv_table_set_column_width;
extern int lv_table_set_row_count;
extern int lv_table_set_selected_cell;
extern int lv_tabview_add_tab;
extern int lv_tabview_class;
extern int lv_tabview_create;
extern int lv_tabview_get_content;
extern int lv_tabview_get_tab_active;
extern int lv_tabview_get_tab_bar;
extern int lv_tabview_get_tab_bar_position;
extern int lv_tabview_get_tab_button;
extern int lv_tabview_get_tab_count;
extern int lv_tabview_set_active;
extern int lv_tabview_set_tab_bar_position;
extern int lv_tabview_set_tab_bar_size;
extern int lv_tabview_set_tab_text;
extern int lv_text_attributes_init;
extern int lv_text_cut;
extern int lv_text_encoded_conv_wc;
extern int lv_text_encoded_get_byte_id;
extern int lv_text_encoded_get_char_id;
extern int lv_text_encoded_letter_next_2;
extern int lv_text_encoded_next;
extern int lv_text_encoded_prev;
extern int lv_text_encoded_size;
extern int lv_text_get_encoded_length;
extern int lv_text_get_next_line;
extern int lv_text_get_size;
extern int lv_text_get_size_attributes;
extern int lv_text_get_width;
extern int lv_text_ins;
extern int lv_text_is_cmd;
extern int lv_text_set_text_vfmt;
extern int lv_text_unicode_to_encoded;
extern int lv_textarea_add_char;
extern int lv_textarea_add_text;
extern int lv_textarea_class;
extern int lv_textarea_clear_selection;
extern int lv_textarea_create;
extern int lv_textarea_cursor_down;
extern int lv_textarea_cursor_left;
extern int lv_textarea_cursor_right;
extern int lv_textarea_cursor_up;
extern int lv_textarea_delete_char;
extern int lv_textarea_delete_char_forward;
extern int lv_textarea_get_accepted_chars;
extern int lv_textarea_get_current_char;
extern int lv_textarea_get_cursor_click_pos;
extern int lv_textarea_get_cursor_pos;
extern int lv_textarea_get_label;
extern int lv_textarea_get_max_length;
extern int lv_textarea_get_one_line;
extern int lv_textarea_get_password_bullet;
extern int lv_textarea_get_password_mode;
extern int lv_textarea_get_password_show_time;
extern int lv_textarea_get_placeholder_text;
extern int lv_textarea_get_text;
extern int lv_textarea_get_text_selection;
extern int lv_textarea_set_accepted_chars;
extern int lv_textarea_set_accepted_chars_static;
extern int lv_textarea_set_align;
extern int lv_textarea_set_cursor_click_pos;
extern int lv_textarea_set_cursor_pos;
extern int lv_textarea_set_insert_replace;
extern int lv_textarea_set_max_length;
extern int lv_textarea_set_one_line;
extern int lv_textarea_set_password_bullet;
extern int lv_textarea_set_password_mode;
extern int lv_textarea_set_password_show_time;
extern int lv_textarea_set_placeholder_text;
extern int lv_textarea_set_text;
extern int lv_textarea_set_text_selection;
extern int lv_textarea_text_is_selected;
extern int lv_theme_apply;
extern int lv_theme_copy;
extern int lv_theme_create;
extern int lv_theme_default_deinit;
extern int lv_theme_default_get;
extern int lv_theme_default_init;
extern int lv_theme_default_is_inited;
extern int lv_theme_delete;
extern int lv_theme_get_color_primary;
extern int lv_theme_get_color_secondary;
extern int lv_theme_get_font_large;
extern int lv_theme_get_font_normal;
extern int lv_theme_get_font_small;
extern int lv_theme_get_from_obj;
extern int lv_theme_set_apply_cb;
extern int lv_theme_set_parent;
extern int lv_theme_simple_deinit;
extern int lv_theme_simple_get;
extern int lv_theme_simple_init;
extern int lv_theme_simple_is_inited;
extern int lv_tick_diff;
extern int lv_tick_elaps;
extern int lv_tick_get;
extern int lv_tick_get_cb;
extern int lv_tick_inc;
extern int lv_tick_set_cb;
extern int lv_tileview_add_tile;
extern int lv_tileview_class;
extern int lv_tileview_create;
extern int lv_tileview_get_tile_active;
extern int lv_tileview_set_tile;
extern int lv_tileview_set_tile_by_index;
extern int lv_tileview_tile_class;
extern int lv_timer_core_deinit;
extern int lv_timer_core_init;
extern int lv_timer_create;
extern int lv_timer_create_basic;
extern int lv_timer_delete;
extern int lv_timer_enable;
extern int lv_timer_get_idle;
extern int lv_timer_get_next;
extern int lv_timer_get_paused;
extern int lv_timer_get_time_until_next;
extern int lv_timer_get_user_data;
extern int lv_timer_handler;
extern int lv_timer_handler_run_in_period;
extern int lv_timer_handler_set_resume_cb;
extern int lv_timer_pause;
extern int lv_timer_periodic_handler;
extern int lv_timer_ready;
extern int lv_timer_reset;
extern int lv_timer_resume;
extern int lv_timer_set_auto_delete;
extern int lv_timer_set_cb;
extern int lv_timer_set_period;
extern int lv_timer_set_repeat_count;
extern int lv_timer_set_user_data;
extern int lv_tjpgd_deinit;
extern int lv_tjpgd_init;
extern int lv_tree_node_class;
extern int lv_tree_node_create;
extern int lv_tree_node_delete;
extern int lv_tree_walk;
extern int lv_trigo_cos;
extern int lv_trigo_sin;
extern int lv_unlock;
extern int lv_utils_bsearch;
extern int lv_vsnprintf;
extern int lv_win_add_button;
extern int lv_win_add_title;
extern int lv_win_class;
extern int lv_win_create;
extern int lv_win_get_content;
extern int lv_win_get_header;
extern int lv_zalloc;
extern int lvgl_port_add_disp;
extern int lvgl_port_add_disp_dsi;
extern int lvgl_port_add_disp_rgb;
extern int lvgl_port_add_touch;
extern int lvgl_port_deinit;
extern int lvgl_port_flush_ready;
extern int lvgl_port_init;
extern int lvgl_port_lock;
extern int lvgl_port_remove_disp;
extern int lvgl_port_remove_touch;
extern int lvgl_port_resume;
extern int lvgl_port_rotate_area;
extern int lvgl_port_stop;
extern int lvgl_port_task_notify;
extern int lvgl_port_task_wake;
extern int lvgl_port_unlock;
extern int malloc;
extern int memcmp;
extern int memcpy;
extern int memmove;
extern int memset;
extern int mkdir;
extern int mktime;
extern int opendir;
extern int powf;
extern int putchar;
extern int puts;
extern int qrcodegen_calcSegmentBufferSize;
extern int qrcodegen_encodeBinary;
extern int qrcodegen_encodeSegments;
extern int qrcodegen_encodeSegmentsAdvanced;
extern int qrcodegen_encodeText;
extern int qrcodegen_getMinFitVersion;
extern int qrcodegen_getModule;
extern int qrcodegen_getSize;
extern int qrcodegen_isAlphanumeric;
extern int qrcodegen_isNumeric;
extern int qrcodegen_makeAlphanumeric;
extern int qrcodegen_makeBytes;
extern int qrcodegen_makeEci;
extern int qrcodegen_makeNumeric;
extern int qrcodegen_version2size;
extern int qsort;
extern int readdir;
extern int realloc;
extern int remove;
extern int rename;
extern int rewind;
extern int roundf;
extern int sinf;
extern int snprintf;
extern int sprintf;
extern int sqrtf;
extern int sscanf;
extern int stat;
extern int strcasecmp;
extern int strcat;
extern int strchr;
extern int strcmp;
extern int strcpy;
extern int strlen;
extern int strncmp;
extern int strncpy;
extern int strnlen;
extern int strrchr;
extern int strstr;
extern int strtod;
extern int strtof;
extern int strtol;
extern int strtoul;
extern int tanf;
extern int time;
extern int unlink;
extern int vsnprintf;
#pragma GCC diagnostic pop

const struct esp_elfsym aos_symbol_table[] = {
    ESP_ELFSYM_EXPORT(LODEPNG_VERSION_STRING),
    ESP_ELFSYM_EXPORT(__addsf3),
    ESP_ELFSYM_EXPORT(__divdi3),
    ESP_ELFSYM_EXPORT(__divsf3),
    ESP_ELFSYM_EXPORT(__extendsfdf2),
    ESP_ELFSYM_EXPORT(__fixsfsi),
    ESP_ELFSYM_EXPORT(__fixunssfsi),
    ESP_ELFSYM_EXPORT(__floatsisf),
    ESP_ELFSYM_EXPORT(__floatunsisf),
    ESP_ELFSYM_EXPORT(__moddi3),
    ESP_ELFSYM_EXPORT(__mulsf3),
    ESP_ELFSYM_EXPORT(__subsf3),
    ESP_ELFSYM_EXPORT(__truncdfsf2),
    ESP_ELFSYM_EXPORT(__udivdi3),
    ESP_ELFSYM_EXPORT(__umoddi3),
    ESP_ELFSYM_EXPORT(abs),
    ESP_ELFSYM_EXPORT(aos_alarm_service_tick),
    ESP_ELFSYM_EXPORT(aos_app_activity_get),
    ESP_ELFSYM_EXPORT(aos_app_alarm_get),
    ESP_ELFSYM_EXPORT(aos_app_calc_get),
    ESP_ELFSYM_EXPORT(aos_app_calendar_get),
    ESP_ELFSYM_EXPORT(aos_app_convert_get),
    ESP_ELFSYM_EXPORT(aos_app_flashlight_get),
    ESP_ELFSYM_EXPORT(aos_app_level_get),
    ESP_ELFSYM_EXPORT(aos_app_life_get),
    ESP_ELFSYM_EXPORT(aos_app_music_get),
    ESP_ELFSYM_EXPORT(aos_app_notifs_get),
    ESP_ELFSYM_EXPORT(aos_app_photos_get),
    ESP_ELFSYM_EXPORT(aos_app_pomodoro_get),
    ESP_ELFSYM_EXPORT(aos_app_power_get),
    ESP_ELFSYM_EXPORT(aos_app_remote_get),
    ESP_ELFSYM_EXPORT(aos_app_settings_get),
    ESP_ELFSYM_EXPORT(aos_app_stopwatch_get),
    ESP_ELFSYM_EXPORT(aos_app_timer_get),
    ESP_ELFSYM_EXPORT(aos_app_worldclock_get),
    ESP_ELFSYM_EXPORT(aos_apps_register_builtin),
    ESP_ELFSYM_EXPORT(aos_board_imu_gyro_enable),
    ESP_ELFSYM_EXPORT(aos_board_imu_gyro_enabled),
    ESP_ELFSYM_EXPORT(aos_board_imu_orientation),
    ESP_ELFSYM_EXPORT(aos_board_imu_poll),
    ESP_ELFSYM_EXPORT(aos_board_imu_read),
    ESP_ELFSYM_EXPORT(aos_board_imu_steps),
    ESP_ELFSYM_EXPORT(aos_board_imu_steps_reset),
    ESP_ELFSYM_EXPORT(aos_board_imu_wrist_raised),
    ESP_ELFSYM_EXPORT(aos_board_init),
    ESP_ELFSYM_EXPORT(aos_board_pmu_charge_current_set),
    ESP_ELFSYM_EXPORT(aos_board_pmu_charge_target_set),
    ESP_ELFSYM_EXPORT(aos_board_pmu_charger_get),
    ESP_ELFSYM_EXPORT(aos_board_pmu_configure),
    ESP_ELFSYM_EXPORT(aos_board_pmu_dump),
    ESP_ELFSYM_EXPORT(aos_board_pmu_poll_irq),
    ESP_ELFSYM_EXPORT(aos_board_pmu_power_off_reason),
    ESP_ELFSYM_EXPORT(aos_board_pmu_power_on_reason),
    ESP_ELFSYM_EXPORT(aos_board_pmu_read),
    ESP_ELFSYM_EXPORT(aos_board_pmu_shutdown),
    ESP_ELFSYM_EXPORT(aos_board_power_key_down),
    ESP_ELFSYM_EXPORT(aos_board_rtc_alarm_clear),
    ESP_ELFSYM_EXPORT(aos_board_rtc_alarm_set),
    ESP_ELFSYM_EXPORT(aos_board_rtc_get),
    ESP_ELFSYM_EXPORT(aos_board_rtc_present),
    ESP_ELFSYM_EXPORT(aos_board_rtc_set),
    ESP_ELFSYM_EXPORT(aos_board_variant),
    ESP_ELFSYM_EXPORT(aos_board_variant_name),
    ESP_ELFSYM_EXPORT(aos_button),
    ESP_ELFSYM_EXPORT(aos_day_name),
    ESP_ELFSYM_EXPORT(aos_face_analog_get),
    ESP_ELFSYM_EXPORT(aos_face_binary_get),
    ESP_ELFSYM_EXPORT(aos_face_digital_get),
    ESP_ELFSYM_EXPORT(aos_face_flip_get),
    ESP_ELFSYM_EXPORT(aos_face_minimal_get),
    ESP_ELFSYM_EXPORT(aos_face_nixie_get),
    ESP_ELFSYM_EXPORT(aos_face_rings_get),
    ESP_ELFSYM_EXPORT(aos_font_body),
    ESP_ELFSYM_EXPORT(aos_font_huge),
    ESP_ELFSYM_EXPORT(aos_font_small),
    ESP_ELFSYM_EXPORT(aos_font_title),
    ESP_ELFSYM_EXPORT(aos_hal_activity),
    ESP_ELFSYM_EXPORT(aos_hal_aod_brightness_get),
    ESP_ELFSYM_EXPORT(aos_hal_aod_brightness_set),
    ESP_ELFSYM_EXPORT(aos_hal_aod_enable),
    ESP_ELFSYM_EXPORT(aos_hal_aod_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_audio_is_playing),
    ESP_ELFSYM_EXPORT(aos_hal_audio_stop),
    ESP_ELFSYM_EXPORT(aos_hal_battery_care_enable),
    ESP_ELFSYM_EXPORT(aos_hal_battery_care_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_battery_read),
    ESP_ELFSYM_EXPORT(aos_hal_beep),
    ESP_ELFSYM_EXPORT(aos_hal_board_name),
    ESP_ELFSYM_EXPORT(aos_hal_brightness_get),
    ESP_ELFSYM_EXPORT(aos_hal_brightness_set),
    ESP_ELFSYM_EXPORT(aos_hal_bt_bonded),
    ESP_ELFSYM_EXPORT(aos_hal_bt_enable),
    ESP_ELFSYM_EXPORT(aos_hal_bt_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_bt_forget),
    ESP_ELFSYM_EXPORT(aos_hal_bt_pair_begin),
    ESP_ELFSYM_EXPORT(aos_hal_bt_pair_cancel),
    ESP_ELFSYM_EXPORT(aos_hal_bt_pair_code),
    ESP_ELFSYM_EXPORT(aos_hal_bt_pair_confirm),
    ESP_ELFSYM_EXPORT(aos_hal_bt_peer),
    ESP_ELFSYM_EXPORT(aos_hal_bt_phone_battery),
    ESP_ELFSYM_EXPORT(aos_hal_bt_state),
    ESP_ELFSYM_EXPORT(aos_hal_display_is_on),
    ESP_ELFSYM_EXPORT(aos_hal_display_on),
    ESP_ELFSYM_EXPORT(aos_hal_display_set_state),
    ESP_ELFSYM_EXPORT(aos_hal_display_state),
    ESP_ELFSYM_EXPORT(aos_hal_firmware_version),
    ESP_ELFSYM_EXPORT(aos_hal_heap_info),
    ESP_ELFSYM_EXPORT(aos_hal_http_body),
    ESP_ELFSYM_EXPORT(aos_hal_http_get),
    ESP_ELFSYM_EXPORT(aos_hal_http_len),
    ESP_ELFSYM_EXPORT(aos_hal_http_release),
    ESP_ELFSYM_EXPORT(aos_hal_http_request),
    ESP_ELFSYM_EXPORT(aos_hal_http_state),
    ESP_ELFSYM_EXPORT(aos_hal_http_status),
    ESP_ELFSYM_EXPORT(aos_hal_imu_gyro_request),
    ESP_ELFSYM_EXPORT(aos_hal_imu_orientation),
    ESP_ELFSYM_EXPORT(aos_hal_imu_read),
    ESP_ELFSYM_EXPORT(aos_hal_imu_steps),
    ESP_ELFSYM_EXPORT(aos_hal_imu_steps_reset),
    ESP_ELFSYM_EXPORT(aos_hal_init),
    ESP_ELFSYM_EXPORT(aos_hal_lock),
    ESP_ELFSYM_EXPORT(aos_hal_log),
    ESP_ELFSYM_EXPORT(aos_hal_media_command),
    ESP_ELFSYM_EXPORT(aos_hal_media_enable),
    ESP_ELFSYM_EXPORT(aos_hal_media_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_media_info),
    ESP_ELFSYM_EXPORT(aos_hal_media_link),
    ESP_ELFSYM_EXPORT(aos_hal_media_peer),
    ESP_ELFSYM_EXPORT(aos_hal_media_player),
    ESP_ELFSYM_EXPORT(aos_hal_mic_available),
    ESP_ELFSYM_EXPORT(aos_hal_mic_close),
    ESP_ELFSYM_EXPORT(aos_hal_mic_gain_get),
    ESP_ELFSYM_EXPORT(aos_hal_mic_gain_set),
    ESP_ELFSYM_EXPORT(aos_hal_mic_level),
    ESP_ELFSYM_EXPORT(aos_hal_mic_open),
    ESP_ELFSYM_EXPORT(aos_hal_mic_read),
    ESP_ELFSYM_EXPORT(aos_hal_mic_status),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_active),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_default_ssid),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_ip),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_pass),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_pass_mode),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_set_config),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_ssid),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_start),
    ESP_ELFSYM_EXPORT(aos_hal_net_ap_stop),
    ESP_ELFSYM_EXPORT(aos_hal_net_enable),
    ESP_ELFSYM_EXPORT(aos_hal_net_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_net_forget),
    ESP_ELFSYM_EXPORT(aos_hal_net_has_credentials),
    ESP_ELFSYM_EXPORT(aos_hal_net_ip),
    ESP_ELFSYM_EXPORT(aos_hal_net_rssi),
    ESP_ELFSYM_EXPORT(aos_hal_net_scan),
    ESP_ELFSYM_EXPORT(aos_hal_net_set_credentials),
    ESP_ELFSYM_EXPORT(aos_hal_net_ssid),
    ESP_ELFSYM_EXPORT(aos_hal_net_state),
    ESP_ELFSYM_EXPORT(aos_hal_net_sync_time),
    ESP_ELFSYM_EXPORT(aos_hal_notif_action),
    ESP_ELFSYM_EXPORT(aos_hal_notif_action_failed),
    ESP_ELFSYM_EXPORT(aos_hal_notif_at),
    ESP_ELFSYM_EXPORT(aos_hal_notif_calls_always),
    ESP_ELFSYM_EXPORT(aos_hal_notif_calls_always_set),
    ESP_ELFSYM_EXPORT(aos_hal_notif_categories),
    ESP_ELFSYM_EXPORT(aos_hal_notif_categories_set),
    ESP_ELFSYM_EXPORT(aos_hal_notif_clear),
    ESP_ELFSYM_EXPORT(aos_hal_notif_count),
    ESP_ELFSYM_EXPORT(aos_hal_notif_enable),
    ESP_ELFSYM_EXPORT(aos_hal_notif_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_notif_pop),
    ESP_ELFSYM_EXPORT(aos_hal_notif_pop_removed),
    ESP_ELFSYM_EXPORT(aos_hal_notif_remove),
    ESP_ELFSYM_EXPORT(aos_hal_notif_sound),
    ESP_ELFSYM_EXPORT(aos_hal_notif_sound_set),
    ESP_ELFSYM_EXPORT(aos_hal_ota_abort),
    ESP_ELFSYM_EXPORT(aos_hal_ota_begin),
    ESP_ELFSYM_EXPORT(aos_hal_ota_end),
    ESP_ELFSYM_EXPORT(aos_hal_ota_error),
    ESP_ELFSYM_EXPORT(aos_hal_ota_mark_valid),
    ESP_ELFSYM_EXPORT(aos_hal_ota_pending_verify),
    ESP_ELFSYM_EXPORT(aos_hal_ota_running_slot),
    ESP_ELFSYM_EXPORT(aos_hal_ota_write),
    ESP_ELFSYM_EXPORT(aos_hal_panel_sleep_enable),
    ESP_ELFSYM_EXPORT(aos_hal_panel_sleep_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_path_apps),
    ESP_ELFSYM_EXPORT(aos_hal_path_data),
    ESP_ELFSYM_EXPORT(aos_hal_path_lang),
    ESP_ELFSYM_EXPORT(aos_hal_path_music),
    ESP_ELFSYM_EXPORT(aos_hal_path_photos),
    ESP_ELFSYM_EXPORT(aos_hal_path_recordings),
    ESP_ELFSYM_EXPORT(aos_hal_path_scans),
    ESP_ELFSYM_EXPORT(aos_hal_play_file),
    ESP_ELFSYM_EXPORT(aos_hal_player_pause),
    ESP_ELFSYM_EXPORT(aos_hal_player_play),
    ESP_ELFSYM_EXPORT(aos_hal_player_resume),
    ESP_ELFSYM_EXPORT(aos_hal_player_status),
    ESP_ELFSYM_EXPORT(aos_hal_player_stop),
    ESP_ELFSYM_EXPORT(aos_hal_power_info),
    ESP_ELFSYM_EXPORT(aos_hal_power_saving_enable),
    ESP_ELFSYM_EXPORT(aos_hal_power_saving_enabled),
    ESP_ELFSYM_EXPORT(aos_hal_pref_erase),
    ESP_ELFSYM_EXPORT(aos_hal_pref_get_i32),
    ESP_ELFSYM_EXPORT(aos_hal_pref_get_str),
    ESP_ELFSYM_EXPORT(aos_hal_pref_set_i32),
    ESP_ELFSYM_EXPORT(aos_hal_pref_set_str),
    ESP_ELFSYM_EXPORT(aos_hal_reboot),
    ESP_ELFSYM_EXPORT(aos_hal_rec_pause),
    ESP_ELFSYM_EXPORT(aos_hal_rec_peaks),
    ESP_ELFSYM_EXPORT(aos_hal_rec_resume),
    ESP_ELFSYM_EXPORT(aos_hal_rec_start),
    ESP_ELFSYM_EXPORT(aos_hal_rec_status),
    ESP_ELFSYM_EXPORT(aos_hal_rec_stop),
    ESP_ELFSYM_EXPORT(aos_hal_rtc_alarm_clear),
    ESP_ELFSYM_EXPORT(aos_hal_rtc_alarm_set),
    ESP_ELFSYM_EXPORT(aos_hal_scan_start),
    ESP_ELFSYM_EXPORT(aos_hal_scan_status),
    ESP_ELFSYM_EXPORT(aos_hal_scan_stop),
    ESP_ELFSYM_EXPORT(aos_hal_sd_present),
    ESP_ELFSYM_EXPORT(aos_hal_sd_usage),
    ESP_ELFSYM_EXPORT(aos_hal_set_button_cb),
    ESP_ELFSYM_EXPORT(aos_hal_set_display_state_cb),
    ESP_ELFSYM_EXPORT(aos_hal_set_power_event_cb),
    ESP_ELFSYM_EXPORT(aos_hal_shutdown),
    ESP_ELFSYM_EXPORT(aos_hal_sleep),
    ESP_ELFSYM_EXPORT(aos_hal_time_is_valid),
    ESP_ELFSYM_EXPORT(aos_hal_time_now),
    ESP_ELFSYM_EXPORT(aos_hal_time_set),
    ESP_ELFSYM_EXPORT(aos_hal_timezone_get),
    ESP_ELFSYM_EXPORT(aos_hal_timezone_set),
    ESP_ELFSYM_EXPORT(aos_hal_touch_gesture),
    ESP_ELFSYM_EXPORT(aos_hal_unlock),
    ESP_ELFSYM_EXPORT(aos_hal_uptime_ms),
    ESP_ELFSYM_EXPORT(aos_hal_volume_get),
    ESP_ELFSYM_EXPORT(aos_hal_volume_set),
    ESP_ELFSYM_EXPORT(aos_hand_create),
    ESP_ELFSYM_EXPORT(aos_hand_set_angle),
    ESP_ELFSYM_EXPORT(aos_i18n_app_count),
    ESP_ELFSYM_EXPORT(aos_i18n_app_load),
    ESP_ELFSYM_EXPORT(aos_i18n_app_unload),
    ESP_ELFSYM_EXPORT(aos_i18n_count),
    ESP_ELFSYM_EXPORT(aos_i18n_current),
    ESP_ELFSYM_EXPORT(aos_i18n_init),
    ESP_ELFSYM_EXPORT(aos_i18n_scan),
    ESP_ELFSYM_EXPORT(aos_i18n_set),
    ESP_ELFSYM_EXPORT(aos_icon_create),
    ESP_ELFSYM_EXPORT(aos_imu_start),
    ESP_ELFSYM_EXPORT(aos_label),
    ESP_ELFSYM_EXPORT(aos_label_boxed),
    ESP_ELFSYM_EXPORT(aos_label_scaled),
    ESP_ELFSYM_EXPORT(aos_lang_pack_count),
    ESP_ELFSYM_EXPORT(aos_lang_packs),
    ESP_ELFSYM_EXPORT(aos_launcher_create),
    ESP_ELFSYM_EXPORT(aos_make_decorative),
    ESP_ELFSYM_EXPORT(aos_month_name),
    ESP_ELFSYM_EXPORT(aos_montserrat_14),
    ESP_ELFSYM_EXPORT(aos_montserrat_16),
    ESP_ELFSYM_EXPORT(aos_montserrat_20),
    ESP_ELFSYM_EXPORT(aos_montserrat_28),
    ESP_ELFSYM_EXPORT(aos_montserrat_36),
    ESP_ELFSYM_EXPORT(aos_montserrat_48),
    ESP_ELFSYM_EXPORT(aos_notif_action_failed),
    ESP_ELFSYM_EXPORT(aos_notif_push),
    ESP_ELFSYM_EXPORT(aos_notif_push_removed),
    ESP_ELFSYM_EXPORT(aos_notif_reset_pending),
    ESP_ELFSYM_EXPORT(aos_notif_ui_close),
    ESP_ELFSYM_EXPORT(aos_notif_ui_show),
    ESP_ELFSYM_EXPORT(aos_notif_ui_tick),
    ESP_ELFSYM_EXPORT(aos_notif_ui_uid),
    ESP_ELFSYM_EXPORT(aos_notif_ui_visible),
    ESP_ELFSYM_EXPORT(aos_page),
    ESP_ELFSYM_EXPORT(aos_pair_ui_cancel),
    ESP_ELFSYM_EXPORT(aos_pair_ui_suppress),
    ESP_ELFSYM_EXPORT(aos_pair_ui_tick),
    ESP_ELFSYM_EXPORT(aos_pair_ui_visible),
    ESP_ELFSYM_EXPORT(aos_rtc_start),
    ESP_ELFSYM_EXPORT(aos_text_font_has),
    ESP_ELFSYM_EXPORT(aos_text_safe),
    ESP_ELFSYM_EXPORT(aos_theme_init),
    ESP_ELFSYM_EXPORT(aos_tr),
    ESP_ELFSYM_EXPORT(aos_trc),
    ESP_ELFSYM_EXPORT(aos_ui_app_at),
    ESP_ELFSYM_EXPORT(aos_ui_app_count),
    ESP_ELFSYM_EXPORT(aos_ui_app_find),
    ESP_ELFSYM_EXPORT(aos_ui_back),
    ESP_ELFSYM_EXPORT(aos_ui_button),
    ESP_ELFSYM_EXPORT(aos_ui_current_app),
    ESP_ELFSYM_EXPORT(aos_ui_home),
    ESP_ELFSYM_EXPORT(aos_ui_init),
    ESP_ELFSYM_EXPORT(aos_ui_launcher_get_style),
    ESP_ELFSYM_EXPORT(aos_ui_launcher_set_style),
    ESP_ELFSYM_EXPORT(aos_ui_open),
    ESP_ELFSYM_EXPORT(aos_ui_register_app),
    ESP_ELFSYM_EXPORT(aos_ui_request_language),
    ESP_ELFSYM_EXPORT(aos_ui_request_snapshot),
    ESP_ELFSYM_EXPORT(aos_ui_request_watchface_picker),
    ESP_ELFSYM_EXPORT(aos_ui_show_launcher),
    ESP_ELFSYM_EXPORT(aos_ui_snapshot_peek),
    ESP_ELFSYM_EXPORT(aos_ui_snapshot_release),
    ESP_ELFSYM_EXPORT(aos_ui_statusbar_refresh),
    ESP_ELFSYM_EXPORT(aos_ui_statusbar_set_visible),
    ESP_ELFSYM_EXPORT(aos_ui_take_gesture),
    ESP_ELFSYM_EXPORT(aos_ui_tick),
    ESP_ELFSYM_EXPORT(aos_ui_toast),
    ESP_ELFSYM_EXPORT(aos_ui_touch_calibration_reset),
    ESP_ELFSYM_EXPORT(aos_ui_touch_calibration_save),
    ESP_ELFSYM_EXPORT(aos_ui_touch_raw),
    ESP_ELFSYM_EXPORT(aos_ui_touch_stats),
    ESP_ELFSYM_EXPORT(aos_ui_unregister_app),
    ESP_ELFSYM_EXPORT(aos_watchface_at),
    ESP_ELFSYM_EXPORT(aos_watchface_close_picker),
    ESP_ELFSYM_EXPORT(aos_watchface_count),
    ESP_ELFSYM_EXPORT(aos_watchface_create),
    ESP_ELFSYM_EXPORT(aos_watchface_current),
    ESP_ELFSYM_EXPORT(aos_watchface_is_aod),
    ESP_ELFSYM_EXPORT(aos_watchface_open_picker),
    ESP_ELFSYM_EXPORT(aos_watchface_picker_visible),
    ESP_ELFSYM_EXPORT(aos_watchface_refresh),
    ESP_ELFSYM_EXPORT(aos_watchface_register),
    ESP_ELFSYM_EXPORT(aos_watchface_select),
    ESP_ELFSYM_EXPORT(aos_watchface_set_aod),
    ESP_ELFSYM_EXPORT(aos_watchfaces_register_builtin),
    ESP_ELFSYM_EXPORT(aos_wifi_qr_text),
    ESP_ELFSYM_EXPORT(atan2f),
    ESP_ELFSYM_EXPORT(atoi),
    ESP_ELFSYM_EXPORT(atol),
    ESP_ELFSYM_EXPORT(axp2101_battery_percent),
    ESP_ELFSYM_EXPORT(axp2101_battery_present),
    ESP_ELFSYM_EXPORT(axp2101_battery_voltage),
    ESP_ELFSYM_EXPORT(axp2101_charge_current_ma),
    ESP_ELFSYM_EXPORT(axp2101_charge_current_set),
    ESP_ELFSYM_EXPORT(axp2101_charge_state),
    ESP_ELFSYM_EXPORT(axp2101_charge_state_name),
    ESP_ELFSYM_EXPORT(axp2101_charge_target_mv),
    ESP_ELFSYM_EXPORT(axp2101_charge_target_set),
    ESP_ELFSYM_EXPORT(axp2101_charging_enable),
    ESP_ELFSYM_EXPORT(axp2101_die_temperature),
    ESP_ELFSYM_EXPORT(axp2101_dump),
    ESP_ELFSYM_EXPORT(axp2101_init),
    ESP_ELFSYM_EXPORT(axp2101_irq_enable),
    ESP_ELFSYM_EXPORT(axp2101_irq_read_clear),
    ESP_ELFSYM_EXPORT(axp2101_is_charging),
    ESP_ELFSYM_EXPORT(axp2101_is_vbus_present),
    ESP_ELFSYM_EXPORT(axp2101_low_battery_levels_get),
    ESP_ELFSYM_EXPORT(axp2101_low_battery_levels_set),
    ESP_ELFSYM_EXPORT(axp2101_power_key_timing_get),
    ESP_ELFSYM_EXPORT(axp2101_power_key_timing_set),
    ESP_ELFSYM_EXPORT(axp2101_power_off_source),
    ESP_ELFSYM_EXPORT(axp2101_power_off_source_name),
    ESP_ELFSYM_EXPORT(axp2101_power_on_source),
    ESP_ELFSYM_EXPORT(axp2101_power_on_source_name),
    ESP_ELFSYM_EXPORT(axp2101_poweroff_voltage_mv),
    ESP_ELFSYM_EXPORT(axp2101_poweroff_voltage_set),
    ESP_ELFSYM_EXPORT(axp2101_precharge_current_ma),
    ESP_ELFSYM_EXPORT(axp2101_precharge_current_set),
    ESP_ELFSYM_EXPORT(axp2101_rail_enable),
    ESP_ELFSYM_EXPORT(axp2101_rail_is_enabled),
    ESP_ELFSYM_EXPORT(axp2101_rail_name),
    ESP_ELFSYM_EXPORT(axp2101_rail_voltage_mv),
    ESP_ELFSYM_EXPORT(axp2101_shutdown),
    ESP_ELFSYM_EXPORT(axp2101_system_voltage),
    ESP_ELFSYM_EXPORT(axp2101_termination_current_ma),
    ESP_ELFSYM_EXPORT(axp2101_termination_current_set),
    ESP_ELFSYM_EXPORT(axp2101_ts_temperature),
    ESP_ELFSYM_EXPORT(axp2101_ts_voltage),
    ESP_ELFSYM_EXPORT(axp2101_vbus_current_limit_ma),
    ESP_ELFSYM_EXPORT(axp2101_vbus_current_limit_set),
    ESP_ELFSYM_EXPORT(axp2101_vbus_voltage),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(ceilf),
    ESP_ELFSYM_EXPORT(closedir),
    ESP_ELFSYM_EXPORT(cosf),
    ESP_ELFSYM_EXPORT(exp2f),
    ESP_ELFSYM_EXPORT(expf),
    ESP_ELFSYM_EXPORT(fabsf),
    ESP_ELFSYM_EXPORT(fclose),
    ESP_ELFSYM_EXPORT(fflush),
    ESP_ELFSYM_EXPORT(floorf),
    ESP_ELFSYM_EXPORT(fmodf),
    ESP_ELFSYM_EXPORT(fopen),
    ESP_ELFSYM_EXPORT(fread),
    ESP_ELFSYM_EXPORT(free),
    ESP_ELFSYM_EXPORT(frogfs_decomp_raw),
    ESP_ELFSYM_EXPORT(fseek),
    ESP_ELFSYM_EXPORT(ftell),
    ESP_ELFSYM_EXPORT(fwrite),
    ESP_ELFSYM_EXPORT(getenv),
    ESP_ELFSYM_EXPORT(gmtime_r),
    ESP_ELFSYM_EXPORT(hypotf),
    ESP_ELFSYM_EXPORT(jd_decomp),
    ESP_ELFSYM_EXPORT(jd_mcu_load),
    ESP_ELFSYM_EXPORT(jd_mcu_output),
    ESP_ELFSYM_EXPORT(jd_prepare),
    ESP_ELFSYM_EXPORT(jd_restart),
    ESP_ELFSYM_EXPORT(labs),
    ESP_ELFSYM_EXPORT(load_kern),
    ESP_ELFSYM_EXPORT(localtime_r),
    ESP_ELFSYM_EXPORT(lodepng_add_itext),
    ESP_ELFSYM_EXPORT(lodepng_add_text),
    ESP_ELFSYM_EXPORT(lodepng_can_have_alpha),
    ESP_ELFSYM_EXPORT(lodepng_chunk_ancillary),
    ESP_ELFSYM_EXPORT(lodepng_chunk_append),
    ESP_ELFSYM_EXPORT(lodepng_chunk_check_crc),
    ESP_ELFSYM_EXPORT(lodepng_chunk_create),
    ESP_ELFSYM_EXPORT(lodepng_chunk_data),
    ESP_ELFSYM_EXPORT(lodepng_chunk_data_const),
    ESP_ELFSYM_EXPORT(lodepng_chunk_find),
    ESP_ELFSYM_EXPORT(lodepng_chunk_find_const),
    ESP_ELFSYM_EXPORT(lodepng_chunk_generate_crc),
    ESP_ELFSYM_EXPORT(lodepng_chunk_length),
    ESP_ELFSYM_EXPORT(lodepng_chunk_next),
    ESP_ELFSYM_EXPORT(lodepng_chunk_next_const),
    ESP_ELFSYM_EXPORT(lodepng_chunk_private),
    ESP_ELFSYM_EXPORT(lodepng_chunk_safetocopy),
    ESP_ELFSYM_EXPORT(lodepng_chunk_type),
    ESP_ELFSYM_EXPORT(lodepng_chunk_type_equals),
    ESP_ELFSYM_EXPORT(lodepng_clear_icc),
    ESP_ELFSYM_EXPORT(lodepng_clear_itext),
    ESP_ELFSYM_EXPORT(lodepng_clear_text),
    ESP_ELFSYM_EXPORT(lodepng_color_mode_cleanup),
    ESP_ELFSYM_EXPORT(lodepng_color_mode_copy),
    ESP_ELFSYM_EXPORT(lodepng_color_mode_init),
    ESP_ELFSYM_EXPORT(lodepng_color_mode_make),
    ESP_ELFSYM_EXPORT(lodepng_color_stats_init),
    ESP_ELFSYM_EXPORT(lodepng_compress_settings_init),
    ESP_ELFSYM_EXPORT(lodepng_compute_color_stats),
    ESP_ELFSYM_EXPORT(lodepng_convert),
    ESP_ELFSYM_EXPORT(lodepng_crc32),
    ESP_ELFSYM_EXPORT(lodepng_decode),
    ESP_ELFSYM_EXPORT(lodepng_decode24),
    ESP_ELFSYM_EXPORT(lodepng_decode24_file),
    ESP_ELFSYM_EXPORT(lodepng_decode32),
    ESP_ELFSYM_EXPORT(lodepng_decode32_file),
    ESP_ELFSYM_EXPORT(lodepng_decode_file),
    ESP_ELFSYM_EXPORT(lodepng_decode_memory),
    ESP_ELFSYM_EXPORT(lodepng_decoder_settings_init),
    ESP_ELFSYM_EXPORT(lodepng_decompress_settings_init),
    ESP_ELFSYM_EXPORT(lodepng_default_compress_settings),
    ESP_ELFSYM_EXPORT(lodepng_default_decompress_settings),
    ESP_ELFSYM_EXPORT(lodepng_deflate),
    ESP_ELFSYM_EXPORT(lodepng_encode),
    ESP_ELFSYM_EXPORT(lodepng_encode24),
    ESP_ELFSYM_EXPORT(lodepng_encode24_file),
    ESP_ELFSYM_EXPORT(lodepng_encode32),
    ESP_ELFSYM_EXPORT(lodepng_encode32_file),
    ESP_ELFSYM_EXPORT(lodepng_encode_file),
    ESP_ELFSYM_EXPORT(lodepng_encode_memory),
    ESP_ELFSYM_EXPORT(lodepng_encoder_settings_init),
    ESP_ELFSYM_EXPORT(lodepng_error_text),
    ESP_ELFSYM_EXPORT(lodepng_get_bpp),
    ESP_ELFSYM_EXPORT(lodepng_get_channels),
    ESP_ELFSYM_EXPORT(lodepng_get_raw_size),
    ESP_ELFSYM_EXPORT(lodepng_has_palette_alpha),
    ESP_ELFSYM_EXPORT(lodepng_huffman_code_lengths),
    ESP_ELFSYM_EXPORT(lodepng_inflate),
    ESP_ELFSYM_EXPORT(lodepng_info_cleanup),
    ESP_ELFSYM_EXPORT(lodepng_info_copy),
    ESP_ELFSYM_EXPORT(lodepng_info_init),
    ESP_ELFSYM_EXPORT(lodepng_inspect),
    ESP_ELFSYM_EXPORT(lodepng_inspect_chunk),
    ESP_ELFSYM_EXPORT(lodepng_is_alpha_type),
    ESP_ELFSYM_EXPORT(lodepng_is_greyscale_type),
    ESP_ELFSYM_EXPORT(lodepng_is_palette_type),
    ESP_ELFSYM_EXPORT(lodepng_load_file),
    ESP_ELFSYM_EXPORT(lodepng_palette_add),
    ESP_ELFSYM_EXPORT(lodepng_palette_clear),
    ESP_ELFSYM_EXPORT(lodepng_save_file),
    ESP_ELFSYM_EXPORT(lodepng_set_icc),
    ESP_ELFSYM_EXPORT(lodepng_state_cleanup),
    ESP_ELFSYM_EXPORT(lodepng_state_copy),
    ESP_ELFSYM_EXPORT(lodepng_state_init),
    ESP_ELFSYM_EXPORT(lodepng_zlib_compress),
    ESP_ELFSYM_EXPORT(lodepng_zlib_decompress),
    ESP_ELFSYM_EXPORT(log10f),
    ESP_ELFSYM_EXPORT(log2f),
    ESP_ELFSYM_EXPORT(logf),
    ESP_ELFSYM_EXPORT(lv_anim_core_deinit),
    ESP_ELFSYM_EXPORT(lv_anim_core_init),
    ESP_ELFSYM_EXPORT(lv_anim_count_running),
    ESP_ELFSYM_EXPORT(lv_anim_custom_delete),
    ESP_ELFSYM_EXPORT(lv_anim_custom_get),
    ESP_ELFSYM_EXPORT(lv_anim_delete),
    ESP_ELFSYM_EXPORT(lv_anim_delete_all),
    ESP_ELFSYM_EXPORT(lv_anim_enable_vsync_mode),
    ESP_ELFSYM_EXPORT(lv_anim_get),
    ESP_ELFSYM_EXPORT(lv_anim_get_delay),
    ESP_ELFSYM_EXPORT(lv_anim_get_playtime),
    ESP_ELFSYM_EXPORT(lv_anim_get_repeat_count),
    ESP_ELFSYM_EXPORT(lv_anim_get_time),
    ESP_ELFSYM_EXPORT(lv_anim_get_timer),
    ESP_ELFSYM_EXPORT(lv_anim_get_user_data),
    ESP_ELFSYM_EXPORT(lv_anim_init),
    ESP_ELFSYM_EXPORT(lv_anim_is_paused),
    ESP_ELFSYM_EXPORT(lv_anim_path_bounce),
    ESP_ELFSYM_EXPORT(lv_anim_path_custom_bezier3),
    ESP_ELFSYM_EXPORT(lv_anim_path_ease_in),
    ESP_ELFSYM_EXPORT(lv_anim_path_ease_in_out),
    ESP_ELFSYM_EXPORT(lv_anim_path_ease_out),
    ESP_ELFSYM_EXPORT(lv_anim_path_linear),
    ESP_ELFSYM_EXPORT(lv_anim_path_overshoot),
    ESP_ELFSYM_EXPORT(lv_anim_path_step),
    ESP_ELFSYM_EXPORT(lv_anim_pause),
    ESP_ELFSYM_EXPORT(lv_anim_pause_for),
    ESP_ELFSYM_EXPORT(lv_anim_refr_now),
    ESP_ELFSYM_EXPORT(lv_anim_resolve_speed),
    ESP_ELFSYM_EXPORT(lv_anim_resume),
    ESP_ELFSYM_EXPORT(lv_anim_set_bezier3_param),
    ESP_ELFSYM_EXPORT(lv_anim_set_completed_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_custom_exec_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_delay),
    ESP_ELFSYM_EXPORT(lv_anim_set_deleted_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_duration),
    ESP_ELFSYM_EXPORT(lv_anim_set_early_apply),
    ESP_ELFSYM_EXPORT(lv_anim_set_exec_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_get_value_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_path_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_repeat_count),
    ESP_ELFSYM_EXPORT(lv_anim_set_repeat_delay),
    ESP_ELFSYM_EXPORT(lv_anim_set_reverse_delay),
    ESP_ELFSYM_EXPORT(lv_anim_set_reverse_duration),
    ESP_ELFSYM_EXPORT(lv_anim_set_reverse_time),
    ESP_ELFSYM_EXPORT(lv_anim_set_start_cb),
    ESP_ELFSYM_EXPORT(lv_anim_set_user_data),
    ESP_ELFSYM_EXPORT(lv_anim_set_values),
    ESP_ELFSYM_EXPORT(lv_anim_set_var),
    ESP_ELFSYM_EXPORT(lv_anim_speed),
    ESP_ELFSYM_EXPORT(lv_anim_speed_clamped),
    ESP_ELFSYM_EXPORT(lv_anim_speed_to_time),
    ESP_ELFSYM_EXPORT(lv_anim_start),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_add),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_create),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_delete),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_delay),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_playtime),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_progress),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_repeat_count),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_repeat_delay),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_reverse),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_get_user_data),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_merge),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_pause),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_delay),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_progress),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_repeat_count),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_repeat_delay),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_reverse),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_set_user_data),
    ESP_ELFSYM_EXPORT(lv_anim_timeline_start),
    ESP_ELFSYM_EXPORT(lv_animimg_class),
    ESP_ELFSYM_EXPORT(lv_animimg_create),
    ESP_ELFSYM_EXPORT(lv_animimg_delete),
    ESP_ELFSYM_EXPORT(lv_animimg_get_anim),
    ESP_ELFSYM_EXPORT(lv_animimg_get_duration),
    ESP_ELFSYM_EXPORT(lv_animimg_get_repeat_count),
    ESP_ELFSYM_EXPORT(lv_animimg_get_src),
    ESP_ELFSYM_EXPORT(lv_animimg_get_src_count),
    ESP_ELFSYM_EXPORT(lv_animimg_set_completed_cb),
    ESP_ELFSYM_EXPORT(lv_animimg_set_duration),
    ESP_ELFSYM_EXPORT(lv_animimg_set_repeat_count),
    ESP_ELFSYM_EXPORT(lv_animimg_set_reverse_delay),
    ESP_ELFSYM_EXPORT(lv_animimg_set_reverse_duration),
    ESP_ELFSYM_EXPORT(lv_animimg_set_src),
    ESP_ELFSYM_EXPORT(lv_animimg_set_src_reverse),
    ESP_ELFSYM_EXPORT(lv_animimg_set_start_cb),
    ESP_ELFSYM_EXPORT(lv_animimg_start),
    ESP_ELFSYM_EXPORT(lv_arc_align_obj_to_angle),
    ESP_ELFSYM_EXPORT(lv_arc_bind_value),
    ESP_ELFSYM_EXPORT(lv_arc_class),
    ESP_ELFSYM_EXPORT(lv_arc_create),
    ESP_ELFSYM_EXPORT(lv_arc_get_angle_end),
    ESP_ELFSYM_EXPORT(lv_arc_get_angle_start),
    ESP_ELFSYM_EXPORT(lv_arc_get_bg_angle_end),
    ESP_ELFSYM_EXPORT(lv_arc_get_bg_angle_start),
    ESP_ELFSYM_EXPORT(lv_arc_get_change_rate),
    ESP_ELFSYM_EXPORT(lv_arc_get_knob_offset),
    ESP_ELFSYM_EXPORT(lv_arc_get_max_value),
    ESP_ELFSYM_EXPORT(lv_arc_get_min_value),
    ESP_ELFSYM_EXPORT(lv_arc_get_mode),
    ESP_ELFSYM_EXPORT(lv_arc_get_rotation),
    ESP_ELFSYM_EXPORT(lv_arc_get_value),
    ESP_ELFSYM_EXPORT(lv_arc_rotate_obj_to_angle),
    ESP_ELFSYM_EXPORT(lv_arc_set_angles),
    ESP_ELFSYM_EXPORT(lv_arc_set_bg_angles),
    ESP_ELFSYM_EXPORT(lv_arc_set_bg_end_angle),
    ESP_ELFSYM_EXPORT(lv_arc_set_bg_start_angle),
    ESP_ELFSYM_EXPORT(lv_arc_set_change_rate),
    ESP_ELFSYM_EXPORT(lv_arc_set_end_angle),
    ESP_ELFSYM_EXPORT(lv_arc_set_knob_offset),
    ESP_ELFSYM_EXPORT(lv_arc_set_max_value),
    ESP_ELFSYM_EXPORT(lv_arc_set_min_value),
    ESP_ELFSYM_EXPORT(lv_arc_set_mode),
    ESP_ELFSYM_EXPORT(lv_arc_set_range),
    ESP_ELFSYM_EXPORT(lv_arc_set_rotation),
    ESP_ELFSYM_EXPORT(lv_arc_set_start_angle),
    ESP_ELFSYM_EXPORT(lv_arc_set_value),
    ESP_ELFSYM_EXPORT(lv_arclabel_class),
    ESP_ELFSYM_EXPORT(lv_arclabel_create),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_angle_size),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_angle_start),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_center_offset_x),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_center_offset_y),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_dir),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_end_overlap),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_overflow),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_radius),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_recolor),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_text_angle),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_text_horizontal_align),
    ESP_ELFSYM_EXPORT(lv_arclabel_get_text_vertical_align),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_angle_size),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_angle_start),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_center_offset_x),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_center_offset_y),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_dir),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_end_overlap),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_offset),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_overflow),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_radius),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_recolor),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_text),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_text_fmt),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_text_horizontal_align),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_text_static),
    ESP_ELFSYM_EXPORT(lv_arclabel_set_text_vertical_align),
    ESP_ELFSYM_EXPORT(lv_area_align),
    ESP_ELFSYM_EXPORT(lv_area_diff),
    ESP_ELFSYM_EXPORT(lv_area_get_height),
    ESP_ELFSYM_EXPORT(lv_area_get_size),
    ESP_ELFSYM_EXPORT(lv_area_get_width),
    ESP_ELFSYM_EXPORT(lv_area_increase),
    ESP_ELFSYM_EXPORT(lv_area_intersect),
    ESP_ELFSYM_EXPORT(lv_area_is_equal),
    ESP_ELFSYM_EXPORT(lv_area_is_in),
    ESP_ELFSYM_EXPORT(lv_area_is_on),
    ESP_ELFSYM_EXPORT(lv_area_is_out),
    ESP_ELFSYM_EXPORT(lv_area_is_point_on),
    ESP_ELFSYM_EXPORT(lv_area_join),
    ESP_ELFSYM_EXPORT(lv_area_move),
    ESP_ELFSYM_EXPORT(lv_area_set),
    ESP_ELFSYM_EXPORT(lv_area_set_height),
    ESP_ELFSYM_EXPORT(lv_area_set_pos),
    ESP_ELFSYM_EXPORT(lv_area_set_width),
    ESP_ELFSYM_EXPORT(lv_array_assign),
    ESP_ELFSYM_EXPORT(lv_array_at),
    ESP_ELFSYM_EXPORT(lv_array_concat),
    ESP_ELFSYM_EXPORT(lv_array_copy),
    ESP_ELFSYM_EXPORT(lv_array_deinit),
    ESP_ELFSYM_EXPORT(lv_array_erase),
    ESP_ELFSYM_EXPORT(lv_array_init),
    ESP_ELFSYM_EXPORT(lv_array_init_from_buf),
    ESP_ELFSYM_EXPORT(lv_array_push_back),
    ESP_ELFSYM_EXPORT(lv_array_remove),
    ESP_ELFSYM_EXPORT(lv_array_remove_unordered),
    ESP_ELFSYM_EXPORT(lv_array_resize),
    ESP_ELFSYM_EXPORT(lv_array_shrink),
    ESP_ELFSYM_EXPORT(lv_async_call),
    ESP_ELFSYM_EXPORT(lv_async_call_cancel),
    ESP_ELFSYM_EXPORT(lv_atan2),
    ESP_ELFSYM_EXPORT(lv_bar_bind_value),
    ESP_ELFSYM_EXPORT(lv_bar_class),
    ESP_ELFSYM_EXPORT(lv_bar_create),
    ESP_ELFSYM_EXPORT(lv_bar_get_max_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_min_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_mode),
    ESP_ELFSYM_EXPORT(lv_bar_get_orientation),
    ESP_ELFSYM_EXPORT(lv_bar_get_start_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_value),
    ESP_ELFSYM_EXPORT(lv_bar_is_symmetrical),
    ESP_ELFSYM_EXPORT(lv_bar_set_max_value),
    ESP_ELFSYM_EXPORT(lv_bar_set_min_value),
    ESP_ELFSYM_EXPORT(lv_bar_set_mode),
    ESP_ELFSYM_EXPORT(lv_bar_set_orientation),
    ESP_ELFSYM_EXPORT(lv_bar_set_range),
    ESP_ELFSYM_EXPORT(lv_bar_set_start_value),
    ESP_ELFSYM_EXPORT(lv_bar_set_value),
    ESP_ELFSYM_EXPORT(lv_bezier3),
    ESP_ELFSYM_EXPORT(lv_bin_decoder_close),
    ESP_ELFSYM_EXPORT(lv_bin_decoder_get_area),
    ESP_ELFSYM_EXPORT(lv_bin_decoder_info),
    ESP_ELFSYM_EXPORT(lv_bin_decoder_init),
    ESP_ELFSYM_EXPORT(lv_bin_decoder_open),
    ESP_ELFSYM_EXPORT(lv_binfont_create),
    ESP_ELFSYM_EXPORT(lv_binfont_destroy),
    ESP_ELFSYM_EXPORT(lv_binfont_font_class),
    ESP_ELFSYM_EXPORT(lv_bmp_deinit),
    ESP_ELFSYM_EXPORT(lv_bmp_init),
    ESP_ELFSYM_EXPORT(lv_builtin_font_class),
    ESP_ELFSYM_EXPORT(lv_button_class),
    ESP_ELFSYM_EXPORT(lv_button_create),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_class),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_clear_button_ctrl),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_clear_button_ctrl_all),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_create),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_button_text),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_one_checked),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_selected_button),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_has_button_ctrl),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_ctrl),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_ctrl_all),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_width),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_ctrl_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_one_checked),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_selected_button),
    ESP_ELFSYM_EXPORT(lv_cache_acquire),
    ESP_ELFSYM_EXPORT(lv_cache_acquire_or_create),
    ESP_ELFSYM_EXPORT(lv_cache_add),
    ESP_ELFSYM_EXPORT(lv_cache_class_lru_ll_count),
    ESP_ELFSYM_EXPORT(lv_cache_class_lru_ll_size),
    ESP_ELFSYM_EXPORT(lv_cache_class_lru_rb_count),
    ESP_ELFSYM_EXPORT(lv_cache_class_lru_rb_size),
    ESP_ELFSYM_EXPORT(lv_cache_class_sc_da),
    ESP_ELFSYM_EXPORT(lv_cache_create),
    ESP_ELFSYM_EXPORT(lv_cache_destroy),
    ESP_ELFSYM_EXPORT(lv_cache_drop),
    ESP_ELFSYM_EXPORT(lv_cache_drop_all),
    ESP_ELFSYM_EXPORT(lv_cache_entry_acquire_data),
    ESP_ELFSYM_EXPORT(lv_cache_entry_alloc),
    ESP_ELFSYM_EXPORT(lv_cache_entry_dec_ref),
    ESP_ELFSYM_EXPORT(lv_cache_entry_delete),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_cache),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_data),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_entry),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_node_size),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_ref),
    ESP_ELFSYM_EXPORT(lv_cache_entry_get_size),
    ESP_ELFSYM_EXPORT(lv_cache_entry_has_flag),
    ESP_ELFSYM_EXPORT(lv_cache_entry_inc_ref),
    ESP_ELFSYM_EXPORT(lv_cache_entry_init),
    ESP_ELFSYM_EXPORT(lv_cache_entry_is_invalid),
    ESP_ELFSYM_EXPORT(lv_cache_entry_release_data),
    ESP_ELFSYM_EXPORT(lv_cache_entry_remove_flag),
    ESP_ELFSYM_EXPORT(lv_cache_entry_reset_ref),
    ESP_ELFSYM_EXPORT(lv_cache_entry_set_cache),
    ESP_ELFSYM_EXPORT(lv_cache_entry_set_flag),
    ESP_ELFSYM_EXPORT(lv_cache_entry_set_node_size),
    ESP_ELFSYM_EXPORT(lv_cache_evict_one),
    ESP_ELFSYM_EXPORT(lv_cache_get_free_size),
    ESP_ELFSYM_EXPORT(lv_cache_get_max_size),
    ESP_ELFSYM_EXPORT(lv_cache_get_name),
    ESP_ELFSYM_EXPORT(lv_cache_get_size),
    ESP_ELFSYM_EXPORT(lv_cache_is_enabled),
    ESP_ELFSYM_EXPORT(lv_cache_iter_create),
    ESP_ELFSYM_EXPORT(lv_cache_release),
    ESP_ELFSYM_EXPORT(lv_cache_reserve),
    ESP_ELFSYM_EXPORT(lv_cache_set_compare_cb),
    ESP_ELFSYM_EXPORT(lv_cache_set_create_cb),
    ESP_ELFSYM_EXPORT(lv_cache_set_free_cb),
    ESP_ELFSYM_EXPORT(lv_cache_set_max_size),
    ESP_ELFSYM_EXPORT(lv_cache_set_name),
    ESP_ELFSYM_EXPORT(lv_calendar_add_header_arrow),
    ESP_ELFSYM_EXPORT(lv_calendar_add_header_dropdown),
    ESP_ELFSYM_EXPORT(lv_calendar_class),
    ESP_ELFSYM_EXPORT(lv_calendar_create),
    ESP_ELFSYM_EXPORT(lv_calendar_get_btnmatrix),
    ESP_ELFSYM_EXPORT(lv_calendar_get_highlighted_dates),
    ESP_ELFSYM_EXPORT(lv_calendar_get_highlighted_dates_num),
    ESP_ELFSYM_EXPORT(lv_calendar_get_pressed_date),
    ESP_ELFSYM_EXPORT(lv_calendar_get_showed_date),
    ESP_ELFSYM_EXPORT(lv_calendar_get_today_date),
    ESP_ELFSYM_EXPORT(lv_calendar_header_arrow_class),
    ESP_ELFSYM_EXPORT(lv_calendar_header_dropdown_class),
    ESP_ELFSYM_EXPORT(lv_calendar_header_dropdown_set_year_list),
    ESP_ELFSYM_EXPORT(lv_calendar_set_day_names),
    ESP_ELFSYM_EXPORT(lv_calendar_set_highlighted_dates),
    ESP_ELFSYM_EXPORT(lv_calendar_set_month_shown),
    ESP_ELFSYM_EXPORT(lv_calendar_set_shown_month),
    ESP_ELFSYM_EXPORT(lv_calendar_set_shown_year),
    ESP_ELFSYM_EXPORT(lv_calendar_set_today_date),
    ESP_ELFSYM_EXPORT(lv_calendar_set_today_day),
    ESP_ELFSYM_EXPORT(lv_calendar_set_today_month),
    ESP_ELFSYM_EXPORT(lv_calendar_set_today_year),
    ESP_ELFSYM_EXPORT(lv_calloc),
    ESP_ELFSYM_EXPORT(lv_canvas_buf_size),
    ESP_ELFSYM_EXPORT(lv_canvas_class),
    ESP_ELFSYM_EXPORT(lv_canvas_copy_buf),
    ESP_ELFSYM_EXPORT(lv_canvas_create),
    ESP_ELFSYM_EXPORT(lv_canvas_fill_bg),
    ESP_ELFSYM_EXPORT(lv_canvas_finish_layer),
    ESP_ELFSYM_EXPORT(lv_canvas_get_buf),
    ESP_ELFSYM_EXPORT(lv_canvas_get_draw_buf),
    ESP_ELFSYM_EXPORT(lv_canvas_get_image),
    ESP_ELFSYM_EXPORT(lv_canvas_get_px),
    ESP_ELFSYM_EXPORT(lv_canvas_init_layer),
    ESP_ELFSYM_EXPORT(lv_canvas_set_buffer),
    ESP_ELFSYM_EXPORT(lv_canvas_set_draw_buf),
    ESP_ELFSYM_EXPORT(lv_canvas_set_palette),
    ESP_ELFSYM_EXPORT(lv_canvas_set_px),
    ESP_ELFSYM_EXPORT(lv_chart_add_cursor),
    ESP_ELFSYM_EXPORT(lv_chart_add_series),
    ESP_ELFSYM_EXPORT(lv_chart_class),
    ESP_ELFSYM_EXPORT(lv_chart_create),
    ESP_ELFSYM_EXPORT(lv_chart_get_cursor_point),
    ESP_ELFSYM_EXPORT(lv_chart_get_first_point_center_offset),
    ESP_ELFSYM_EXPORT(lv_chart_get_hor_div_line_count),
    ESP_ELFSYM_EXPORT(lv_chart_get_point_count),
    ESP_ELFSYM_EXPORT(lv_chart_get_point_pos_by_id),
    ESP_ELFSYM_EXPORT(lv_chart_get_pressed_point),
    ESP_ELFSYM_EXPORT(lv_chart_get_series_color),
    ESP_ELFSYM_EXPORT(lv_chart_get_series_next),
    ESP_ELFSYM_EXPORT(lv_chart_get_series_x_array),
    ESP_ELFSYM_EXPORT(lv_chart_get_series_y_array),
    ESP_ELFSYM_EXPORT(lv_chart_get_type),
    ESP_ELFSYM_EXPORT(lv_chart_get_update_mode),
    ESP_ELFSYM_EXPORT(lv_chart_get_ver_div_line_count),
    ESP_ELFSYM_EXPORT(lv_chart_get_x_start_point),
    ESP_ELFSYM_EXPORT(lv_chart_hide_series),
    ESP_ELFSYM_EXPORT(lv_chart_refresh),
    ESP_ELFSYM_EXPORT(lv_chart_remove_cursor),
    ESP_ELFSYM_EXPORT(lv_chart_remove_series),
    ESP_ELFSYM_EXPORT(lv_chart_set_all_values),
    ESP_ELFSYM_EXPORT(lv_chart_set_axis_max_value),
    ESP_ELFSYM_EXPORT(lv_chart_set_axis_min_value),
    ESP_ELFSYM_EXPORT(lv_chart_set_axis_range),
    ESP_ELFSYM_EXPORT(lv_chart_set_cursor_point),
    ESP_ELFSYM_EXPORT(lv_chart_set_cursor_pos),
    ESP_ELFSYM_EXPORT(lv_chart_set_cursor_pos_x),
    ESP_ELFSYM_EXPORT(lv_chart_set_cursor_pos_y),
    ESP_ELFSYM_EXPORT(lv_chart_set_div_line_count),
    ESP_ELFSYM_EXPORT(lv_chart_set_hor_div_line_count),
    ESP_ELFSYM_EXPORT(lv_chart_set_next_value),
    ESP_ELFSYM_EXPORT(lv_chart_set_next_value2),
    ESP_ELFSYM_EXPORT(lv_chart_set_point_count),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_color),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_ext_x_array),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_ext_y_array),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_value_by_id),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_value_by_id2),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_values),
    ESP_ELFSYM_EXPORT(lv_chart_set_series_values2),
    ESP_ELFSYM_EXPORT(lv_chart_set_type),
    ESP_ELFSYM_EXPORT(lv_chart_set_update_mode),
    ESP_ELFSYM_EXPORT(lv_chart_set_ver_div_line_count),
    ESP_ELFSYM_EXPORT(lv_chart_set_x_start_point),
    ESP_ELFSYM_EXPORT(lv_checkbox_class),
    ESP_ELFSYM_EXPORT(lv_checkbox_create),
    ESP_ELFSYM_EXPORT(lv_checkbox_get_text),
    ESP_ELFSYM_EXPORT(lv_checkbox_set_text),
    ESP_ELFSYM_EXPORT(lv_checkbox_set_text_static),
    ESP_ELFSYM_EXPORT(lv_circle_buf_capacity),
    ESP_ELFSYM_EXPORT(lv_circle_buf_create),
    ESP_ELFSYM_EXPORT(lv_circle_buf_create_from_array),
    ESP_ELFSYM_EXPORT(lv_circle_buf_create_from_buf),
    ESP_ELFSYM_EXPORT(lv_circle_buf_destroy),
    ESP_ELFSYM_EXPORT(lv_circle_buf_fill),
    ESP_ELFSYM_EXPORT(lv_circle_buf_head),
    ESP_ELFSYM_EXPORT(lv_circle_buf_is_empty),
    ESP_ELFSYM_EXPORT(lv_circle_buf_is_full),
    ESP_ELFSYM_EXPORT(lv_circle_buf_peek),
    ESP_ELFSYM_EXPORT(lv_circle_buf_peek_at),
    ESP_ELFSYM_EXPORT(lv_circle_buf_read),
    ESP_ELFSYM_EXPORT(lv_circle_buf_remain),
    ESP_ELFSYM_EXPORT(lv_circle_buf_reset),
    ESP_ELFSYM_EXPORT(lv_circle_buf_resize),
    ESP_ELFSYM_EXPORT(lv_circle_buf_size),
    ESP_ELFSYM_EXPORT(lv_circle_buf_skip),
    ESP_ELFSYM_EXPORT(lv_circle_buf_tail),
    ESP_ELFSYM_EXPORT(lv_circle_buf_write),
    ESP_ELFSYM_EXPORT(lv_clamp_height),
    ESP_ELFSYM_EXPORT(lv_clamp_width),
    ESP_ELFSYM_EXPORT(lv_color16_luminance),
    ESP_ELFSYM_EXPORT(lv_color16_premultiply),
    ESP_ELFSYM_EXPORT(lv_color24_luminance),
    ESP_ELFSYM_EXPORT(lv_color32_eq),
    ESP_ELFSYM_EXPORT(lv_color32_luminance),
    ESP_ELFSYM_EXPORT(lv_color32_make),
    ESP_ELFSYM_EXPORT(lv_color_16_16_mix),
    ESP_ELFSYM_EXPORT(lv_color_black),
    ESP_ELFSYM_EXPORT(lv_color_brightness),
    ESP_ELFSYM_EXPORT(lv_color_darken),
    ESP_ELFSYM_EXPORT(lv_color_eq),
    ESP_ELFSYM_EXPORT(lv_color_filter_dsc_init),
    ESP_ELFSYM_EXPORT(lv_color_filter_shade),
    ESP_ELFSYM_EXPORT(lv_color_format_get_bpp),
    ESP_ELFSYM_EXPORT(lv_color_format_get_size),
    ESP_ELFSYM_EXPORT(lv_color_format_has_alpha),
    ESP_ELFSYM_EXPORT(lv_color_hex),
    ESP_ELFSYM_EXPORT(lv_color_hex3),
    ESP_ELFSYM_EXPORT(lv_color_hsv_to_rgb),
    ESP_ELFSYM_EXPORT(lv_color_lighten),
    ESP_ELFSYM_EXPORT(lv_color_luminance),
    ESP_ELFSYM_EXPORT(lv_color_make),
    ESP_ELFSYM_EXPORT(lv_color_mix),
    ESP_ELFSYM_EXPORT(lv_color_mix32),
    ESP_ELFSYM_EXPORT(lv_color_mix32_premultiplied),
    ESP_ELFSYM_EXPORT(lv_color_over32),
    ESP_ELFSYM_EXPORT(lv_color_premultiply),
    ESP_ELFSYM_EXPORT(lv_color_rgb_to_hsv),
    ESP_ELFSYM_EXPORT(lv_color_to_32),
    ESP_ELFSYM_EXPORT(lv_color_to_hsv),
    ESP_ELFSYM_EXPORT(lv_color_to_int),
    ESP_ELFSYM_EXPORT(lv_color_to_u16),
    ESP_ELFSYM_EXPORT(lv_color_to_u32),
    ESP_ELFSYM_EXPORT(lv_color_white),
    ESP_ELFSYM_EXPORT(lv_cubic_bezier),
    ESP_ELFSYM_EXPORT(lv_deinit),
    ESP_ELFSYM_EXPORT(lv_delay_ms),
    ESP_ELFSYM_EXPORT(lv_delay_set_cb),
    ESP_ELFSYM_EXPORT(lv_display_add_event_cb),
    ESP_ELFSYM_EXPORT(lv_display_create),
    ESP_ELFSYM_EXPORT(lv_display_delete),
    ESP_ELFSYM_EXPORT(lv_display_delete_event),
    ESP_ELFSYM_EXPORT(lv_display_delete_refr_timer),
    ESP_ELFSYM_EXPORT(lv_display_dpx),
    ESP_ELFSYM_EXPORT(lv_display_enable_invalidation),
    ESP_ELFSYM_EXPORT(lv_display_flush_is_last),
    ESP_ELFSYM_EXPORT(lv_display_flush_ready),
    ESP_ELFSYM_EXPORT(lv_display_get_antialiasing),
    ESP_ELFSYM_EXPORT(lv_display_get_buf_active),
    ESP_ELFSYM_EXPORT(lv_display_get_color_format),
    ESP_ELFSYM_EXPORT(lv_display_get_default),
    ESP_ELFSYM_EXPORT(lv_display_get_dpi),
    ESP_ELFSYM_EXPORT(lv_display_get_draw_buf_size),
    ESP_ELFSYM_EXPORT(lv_display_get_driver_data),
    ESP_ELFSYM_EXPORT(lv_display_get_event_count),
    ESP_ELFSYM_EXPORT(lv_display_get_event_dsc),
    ESP_ELFSYM_EXPORT(lv_display_get_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_inactive_time),
    ESP_ELFSYM_EXPORT(lv_display_get_invalidated_draw_buf_size),
    ESP_ELFSYM_EXPORT(lv_display_get_layer_bottom),
    ESP_ELFSYM_EXPORT(lv_display_get_layer_sys),
    ESP_ELFSYM_EXPORT(lv_display_get_layer_top),
    ESP_ELFSYM_EXPORT(lv_display_get_matrix_rotation),
    ESP_ELFSYM_EXPORT(lv_display_get_next),
    ESP_ELFSYM_EXPORT(lv_display_get_offset_x),
    ESP_ELFSYM_EXPORT(lv_display_get_offset_y),
    ESP_ELFSYM_EXPORT(lv_display_get_original_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_original_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_physical_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_physical_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_refr_timer),
    ESP_ELFSYM_EXPORT(lv_display_get_render_mode),
    ESP_ELFSYM_EXPORT(lv_display_get_rotation),
    ESP_ELFSYM_EXPORT(lv_display_get_screen_active),
    ESP_ELFSYM_EXPORT(lv_display_get_screen_loading),
    ESP_ELFSYM_EXPORT(lv_display_get_screen_prev),
    ESP_ELFSYM_EXPORT(lv_display_get_theme),
    ESP_ELFSYM_EXPORT(lv_display_get_tile_cnt),
    ESP_ELFSYM_EXPORT(lv_display_get_user_data),
    ESP_ELFSYM_EXPORT(lv_display_get_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_is_double_buffered),
    ESP_ELFSYM_EXPORT(lv_display_is_invalidation_enabled),
    ESP_ELFSYM_EXPORT(lv_display_refr_timer),
    ESP_ELFSYM_EXPORT(lv_display_register_vsync_event),
    ESP_ELFSYM_EXPORT(lv_display_remove_event_cb_with_user_data),
    ESP_ELFSYM_EXPORT(lv_display_rotate_area),
    ESP_ELFSYM_EXPORT(lv_display_rotate_point),
    ESP_ELFSYM_EXPORT(lv_display_send_event),
    ESP_ELFSYM_EXPORT(lv_display_send_vsync_event),
    ESP_ELFSYM_EXPORT(lv_display_set_3rd_draw_buffer),
    ESP_ELFSYM_EXPORT(lv_display_set_antialiasing),
    ESP_ELFSYM_EXPORT(lv_display_set_buffers),
    ESP_ELFSYM_EXPORT(lv_display_set_buffers_with_stride),
    ESP_ELFSYM_EXPORT(lv_display_set_color_format),
    ESP_ELFSYM_EXPORT(lv_display_set_default),
    ESP_ELFSYM_EXPORT(lv_display_set_dpi),
    ESP_ELFSYM_EXPORT(lv_display_set_draw_buffers),
    ESP_ELFSYM_EXPORT(lv_display_set_driver_data),
    ESP_ELFSYM_EXPORT(lv_display_set_flush_cb),
    ESP_ELFSYM_EXPORT(lv_display_set_flush_wait_cb),
    ESP_ELFSYM_EXPORT(lv_display_set_matrix_rotation),
    ESP_ELFSYM_EXPORT(lv_display_set_offset),
    ESP_ELFSYM_EXPORT(lv_display_set_physical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_set_render_mode),
    ESP_ELFSYM_EXPORT(lv_display_set_resolution),
    ESP_ELFSYM_EXPORT(lv_display_set_rotation),
    ESP_ELFSYM_EXPORT(lv_display_set_theme),
    ESP_ELFSYM_EXPORT(lv_display_set_tile_cnt),
    ESP_ELFSYM_EXPORT(lv_display_set_user_data),
    ESP_ELFSYM_EXPORT(lv_display_trigger_activity),
    ESP_ELFSYM_EXPORT(lv_display_unregister_vsync_event),
    ESP_ELFSYM_EXPORT(lv_dpx),
    ESP_ELFSYM_EXPORT(lv_draw_add_task),
    ESP_ELFSYM_EXPORT(lv_draw_arc),
    ESP_ELFSYM_EXPORT(lv_draw_arc_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_arc_get_area),
    ESP_ELFSYM_EXPORT(lv_draw_blur),
    ESP_ELFSYM_EXPORT(lv_draw_blur_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_border),
    ESP_ELFSYM_EXPORT(lv_draw_border_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_box_shadow),
    ESP_ELFSYM_EXPORT(lv_draw_box_shadow_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_buf_adjust_stride),
    ESP_ELFSYM_EXPORT(lv_draw_buf_align),
    ESP_ELFSYM_EXPORT(lv_draw_buf_align_ex),
    ESP_ELFSYM_EXPORT(lv_draw_buf_clear),
    ESP_ELFSYM_EXPORT(lv_draw_buf_convert_premultiply),
    ESP_ELFSYM_EXPORT(lv_draw_buf_copy),
    ESP_ELFSYM_EXPORT(lv_draw_buf_create),
    ESP_ELFSYM_EXPORT(lv_draw_buf_create_ex),
    ESP_ELFSYM_EXPORT(lv_draw_buf_destroy),
    ESP_ELFSYM_EXPORT(lv_draw_buf_dup),
    ESP_ELFSYM_EXPORT(lv_draw_buf_dup_ex),
    ESP_ELFSYM_EXPORT(lv_draw_buf_flush_cache),
    ESP_ELFSYM_EXPORT(lv_draw_buf_from_image),
    ESP_ELFSYM_EXPORT(lv_draw_buf_get_font_handlers),
    ESP_ELFSYM_EXPORT(lv_draw_buf_get_handlers),
    ESP_ELFSYM_EXPORT(lv_draw_buf_get_image_handlers),
    ESP_ELFSYM_EXPORT(lv_draw_buf_goto_xy),
    ESP_ELFSYM_EXPORT(lv_draw_buf_handlers_init),
    ESP_ELFSYM_EXPORT(lv_draw_buf_init),
    ESP_ELFSYM_EXPORT(lv_draw_buf_init_handlers),
    ESP_ELFSYM_EXPORT(lv_draw_buf_init_with_default_handlers),
    ESP_ELFSYM_EXPORT(lv_draw_buf_invalidate_cache),
    ESP_ELFSYM_EXPORT(lv_draw_buf_premultiply),
    ESP_ELFSYM_EXPORT(lv_draw_buf_reshape),
    ESP_ELFSYM_EXPORT(lv_draw_buf_save_to_file),
    ESP_ELFSYM_EXPORT(lv_draw_buf_set_palette),
    ESP_ELFSYM_EXPORT(lv_draw_buf_to_image),
    ESP_ELFSYM_EXPORT(lv_draw_buf_width_to_stride),
    ESP_ELFSYM_EXPORT(lv_draw_buf_width_to_stride_ex),
    ESP_ELFSYM_EXPORT(lv_draw_character),
    ESP_ELFSYM_EXPORT(lv_draw_create_unit),
    ESP_ELFSYM_EXPORT(lv_draw_deinit),
    ESP_ELFSYM_EXPORT(lv_draw_dispatch),
    ESP_ELFSYM_EXPORT(lv_draw_dispatch_layer),
    ESP_ELFSYM_EXPORT(lv_draw_dispatch_request),
    ESP_ELFSYM_EXPORT(lv_draw_dispatch_wait_for_request),
    ESP_ELFSYM_EXPORT(lv_draw_fill),
    ESP_ELFSYM_EXPORT(lv_draw_fill_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_finalize_task_creation),
    ESP_ELFSYM_EXPORT(lv_draw_get_available_task),
    ESP_ELFSYM_EXPORT(lv_draw_get_dependent_count),
    ESP_ELFSYM_EXPORT(lv_draw_get_next_available_task),
    ESP_ELFSYM_EXPORT(lv_draw_get_unit_count),
    ESP_ELFSYM_EXPORT(lv_draw_glyph_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_image),
    ESP_ELFSYM_EXPORT(lv_draw_image_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_image_normal_helper),
    ESP_ELFSYM_EXPORT(lv_draw_image_tiled_helper),
    ESP_ELFSYM_EXPORT(lv_draw_init),
    ESP_ELFSYM_EXPORT(lv_draw_label),
    ESP_ELFSYM_EXPORT(lv_draw_label_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_label_iterate_characters),
    ESP_ELFSYM_EXPORT(lv_draw_layer),
    ESP_ELFSYM_EXPORT(lv_draw_layer_alloc_buf),
    ESP_ELFSYM_EXPORT(lv_draw_layer_create),
    ESP_ELFSYM_EXPORT(lv_draw_layer_create_drop_shadow),
    ESP_ELFSYM_EXPORT(lv_draw_layer_finish_drop_shadow),
    ESP_ELFSYM_EXPORT(lv_draw_layer_go_to_xy),
    ESP_ELFSYM_EXPORT(lv_draw_layer_init),
    ESP_ELFSYM_EXPORT(lv_draw_letter),
    ESP_ELFSYM_EXPORT(lv_draw_letter_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_line),
    ESP_ELFSYM_EXPORT(lv_draw_line_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_line_iterate),
    ESP_ELFSYM_EXPORT(lv_draw_mask_rect),
    ESP_ELFSYM_EXPORT(lv_draw_mask_rect_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_rect),
    ESP_ELFSYM_EXPORT(lv_draw_rect_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_arc),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_a8),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_al88),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_argb8888),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_argb8888_premultiplied),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_i1),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_l8),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_rgb565),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_rgb565_swapped),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_color_to_rgb888),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_a8),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_al88),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_argb8888),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_argb8888_premultiplied),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_i1),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_l8),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_rgb565),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_rgb565_swapped),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blend_image_to_rgb888),
    ESP_ELFSYM_EXPORT(lv_draw_sw_blur),
    ESP_ELFSYM_EXPORT(lv_draw_sw_border),
    ESP_ELFSYM_EXPORT(lv_draw_sw_box_shadow),
    ESP_ELFSYM_EXPORT(lv_draw_sw_deinit),
    ESP_ELFSYM_EXPORT(lv_draw_sw_fill),
    ESP_ELFSYM_EXPORT(lv_draw_sw_get_blend_handler),
    ESP_ELFSYM_EXPORT(lv_draw_sw_grad_cleanup),
    ESP_ELFSYM_EXPORT(lv_draw_sw_grad_color_calculate),
    ESP_ELFSYM_EXPORT(lv_draw_sw_grad_get),
    ESP_ELFSYM_EXPORT(lv_draw_sw_i1_convert_to_vtiled),
    ESP_ELFSYM_EXPORT(lv_draw_sw_i1_invert),
    ESP_ELFSYM_EXPORT(lv_draw_sw_i1_to_argb8888),
    ESP_ELFSYM_EXPORT(lv_draw_sw_image),
    ESP_ELFSYM_EXPORT(lv_draw_sw_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_label),
    ESP_ELFSYM_EXPORT(lv_draw_sw_layer),
    ESP_ELFSYM_EXPORT(lv_draw_sw_letter),
    ESP_ELFSYM_EXPORT(lv_draw_sw_line),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_angle_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_apply),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_cleanup),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_deinit),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_fade_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_free_param),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_line_angle_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_line_points_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_map_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_radius_init),
    ESP_ELFSYM_EXPORT(lv_draw_sw_mask_rect),
    ESP_ELFSYM_EXPORT(lv_draw_sw_register_blend_handler),
    ESP_ELFSYM_EXPORT(lv_draw_sw_rgb565_swap),
    ESP_ELFSYM_EXPORT(lv_draw_sw_rotate),
    ESP_ELFSYM_EXPORT(lv_draw_sw_transform),
    ESP_ELFSYM_EXPORT(lv_draw_sw_triangle),
    ESP_ELFSYM_EXPORT(lv_draw_sw_unregister_blend_handler),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_arc_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_area),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_blur_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_border_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_box_shadow_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_draw_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_fill_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_image_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_label_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_line_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_mask_rect_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_triangle_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_type),
    ESP_ELFSYM_EXPORT(lv_draw_triangle),
    ESP_ELFSYM_EXPORT(lv_draw_triangle_dsc_init),
    ESP_ELFSYM_EXPORT(lv_draw_unit_draw_letter),
    ESP_ELFSYM_EXPORT(lv_draw_unit_send_event),
    ESP_ELFSYM_EXPORT(lv_draw_wait_for_finish),
    ESP_ELFSYM_EXPORT(lv_dropdown_add_option),
    ESP_ELFSYM_EXPORT(lv_dropdown_bind_value),
    ESP_ELFSYM_EXPORT(lv_dropdown_class),
    ESP_ELFSYM_EXPORT(lv_dropdown_clear_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_close),
    ESP_ELFSYM_EXPORT(lv_dropdown_create),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_dir),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_list),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_option_count),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_option_index),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected_highlight),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected_str),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_symbol),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_text),
    ESP_ELFSYM_EXPORT(lv_dropdown_is_open),
    ESP_ELFSYM_EXPORT(lv_dropdown_open),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_dir),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_options_static),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_selected),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_selected_highlight),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_symbol),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_text),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_text_static),
    ESP_ELFSYM_EXPORT(lv_dropdownlist_class),
    ESP_ELFSYM_EXPORT(lv_event_add),
    ESP_ELFSYM_EXPORT(lv_event_code_get_name),
    ESP_ELFSYM_EXPORT(lv_event_dsc_get_cb),
    ESP_ELFSYM_EXPORT(lv_event_dsc_get_user_data),
    ESP_ELFSYM_EXPORT(lv_event_free_user_data_cb),
    ESP_ELFSYM_EXPORT(lv_event_get_code),
    ESP_ELFSYM_EXPORT(lv_event_get_count),
    ESP_ELFSYM_EXPORT(lv_event_get_cover_area),
    ESP_ELFSYM_EXPORT(lv_event_get_current_target),
    ESP_ELFSYM_EXPORT(lv_event_get_current_target_obj),
    ESP_ELFSYM_EXPORT(lv_event_get_draw_task),
    ESP_ELFSYM_EXPORT(lv_event_get_dsc),
    ESP_ELFSYM_EXPORT(lv_event_get_hit_test_info),
    ESP_ELFSYM_EXPORT(lv_event_get_indev),
    ESP_ELFSYM_EXPORT(lv_event_get_invalidated_area),
    ESP_ELFSYM_EXPORT(lv_event_get_key),
    ESP_ELFSYM_EXPORT(lv_event_get_layer),
    ESP_ELFSYM_EXPORT(lv_event_get_old_size),
    ESP_ELFSYM_EXPORT(lv_event_get_param),
    ESP_ELFSYM_EXPORT(lv_event_get_prev_state),
    ESP_ELFSYM_EXPORT(lv_event_get_rotary_diff),
    ESP_ELFSYM_EXPORT(lv_event_get_scroll_anim),
    ESP_ELFSYM_EXPORT(lv_event_get_self_size_info),
    ESP_ELFSYM_EXPORT(lv_event_get_target),
    ESP_ELFSYM_EXPORT(lv_event_get_target_obj),
    ESP_ELFSYM_EXPORT(lv_event_get_user_data),
    ESP_ELFSYM_EXPORT(lv_event_mark_deleted),
    ESP_ELFSYM_EXPORT(lv_event_pop),
    ESP_ELFSYM_EXPORT(lv_event_push),
    ESP_ELFSYM_EXPORT(lv_event_push_and_send),
    ESP_ELFSYM_EXPORT(lv_event_register_id),
    ESP_ELFSYM_EXPORT(lv_event_remove),
    ESP_ELFSYM_EXPORT(lv_event_remove_all),
    ESP_ELFSYM_EXPORT(lv_event_remove_dsc),
    ESP_ELFSYM_EXPORT(lv_event_send),
    ESP_ELFSYM_EXPORT(lv_event_set_cover_res),
    ESP_ELFSYM_EXPORT(lv_event_set_ext_draw_size),
    ESP_ELFSYM_EXPORT(lv_event_stop_bubbling),
    ESP_ELFSYM_EXPORT(lv_event_stop_processing),
    ESP_ELFSYM_EXPORT(lv_event_stop_trickling),
    ESP_ELFSYM_EXPORT(lv_flex_init),
    ESP_ELFSYM_EXPORT(lv_font_get_bitmap_fmt_txt),
    ESP_ELFSYM_EXPORT(lv_font_get_default),
    ESP_ELFSYM_EXPORT(lv_font_get_glyph_bitmap),
    ESP_ELFSYM_EXPORT(lv_font_get_glyph_dsc),
    ESP_ELFSYM_EXPORT(lv_font_get_glyph_dsc_fmt_txt),
    ESP_ELFSYM_EXPORT(lv_font_get_glyph_static_bitmap),
    ESP_ELFSYM_EXPORT(lv_font_get_glyph_width),
    ESP_ELFSYM_EXPORT(lv_font_get_line_height),
    ESP_ELFSYM_EXPORT(lv_font_glyph_release_draw_data),
    ESP_ELFSYM_EXPORT(lv_font_has_static_bitmap),
    ESP_ELFSYM_EXPORT(lv_font_info_is_equal),
    ESP_ELFSYM_EXPORT(lv_font_set_kerning),
    ESP_ELFSYM_EXPORT(lv_free),
    ESP_ELFSYM_EXPORT(lv_free_core),
    ESP_ELFSYM_EXPORT(lv_fs_close),
    ESP_ELFSYM_EXPORT(lv_fs_deinit),
    ESP_ELFSYM_EXPORT(lv_fs_dir_close),
    ESP_ELFSYM_EXPORT(lv_fs_dir_open),
    ESP_ELFSYM_EXPORT(lv_fs_dir_read),
    ESP_ELFSYM_EXPORT(lv_fs_drv_init),
    ESP_ELFSYM_EXPORT(lv_fs_drv_register),
    ESP_ELFSYM_EXPORT(lv_fs_get_buffer_from_path),
    ESP_ELFSYM_EXPORT(lv_fs_get_drv),
    ESP_ELFSYM_EXPORT(lv_fs_get_ext),
    ESP_ELFSYM_EXPORT(lv_fs_get_last),
    ESP_ELFSYM_EXPORT(lv_fs_get_letters),
    ESP_ELFSYM_EXPORT(lv_fs_get_size),
    ESP_ELFSYM_EXPORT(lv_fs_init),
    ESP_ELFSYM_EXPORT(lv_fs_is_ready),
    ESP_ELFSYM_EXPORT(lv_fs_load_to_buf),
    ESP_ELFSYM_EXPORT(lv_fs_load_with_alloc),
    ESP_ELFSYM_EXPORT(lv_fs_make_path_from_buffer),
    ESP_ELFSYM_EXPORT(lv_fs_open),
    ESP_ELFSYM_EXPORT(lv_fs_path_get_size),
    ESP_ELFSYM_EXPORT(lv_fs_path_join),
    ESP_ELFSYM_EXPORT(lv_fs_posix_init),
    ESP_ELFSYM_EXPORT(lv_fs_read),
    ESP_ELFSYM_EXPORT(lv_fs_remove_drive),
    ESP_ELFSYM_EXPORT(lv_fs_seek),
    ESP_ELFSYM_EXPORT(lv_fs_tell),
    ESP_ELFSYM_EXPORT(lv_fs_up),
    ESP_ELFSYM_EXPORT(lv_fs_write),
    ESP_ELFSYM_EXPORT(lv_global),
    ESP_ELFSYM_EXPORT(lv_grad_conical_init),
    ESP_ELFSYM_EXPORT(lv_grad_horizontal_init),
    ESP_ELFSYM_EXPORT(lv_grad_init_stops),
    ESP_ELFSYM_EXPORT(lv_grad_linear_init),
    ESP_ELFSYM_EXPORT(lv_grad_radial_init),
    ESP_ELFSYM_EXPORT(lv_grad_radial_set_focal),
    ESP_ELFSYM_EXPORT(lv_grad_vertical_init),
    ESP_ELFSYM_EXPORT(lv_grid_fr),
    ESP_ELFSYM_EXPORT(lv_grid_init),
    ESP_ELFSYM_EXPORT(lv_group_add_obj),
    ESP_ELFSYM_EXPORT(lv_group_by_index),
    ESP_ELFSYM_EXPORT(lv_group_create),
    ESP_ELFSYM_EXPORT(lv_group_deinit),
    ESP_ELFSYM_EXPORT(lv_group_delete),
    ESP_ELFSYM_EXPORT(lv_group_focus_freeze),
    ESP_ELFSYM_EXPORT(lv_group_focus_next),
    ESP_ELFSYM_EXPORT(lv_group_focus_obj),
    ESP_ELFSYM_EXPORT(lv_group_focus_prev),
    ESP_ELFSYM_EXPORT(lv_group_get_count),
    ESP_ELFSYM_EXPORT(lv_group_get_default),
    ESP_ELFSYM_EXPORT(lv_group_get_edge_cb),
    ESP_ELFSYM_EXPORT(lv_group_get_editing),
    ESP_ELFSYM_EXPORT(lv_group_get_focus_cb),
    ESP_ELFSYM_EXPORT(lv_group_get_focused),
    ESP_ELFSYM_EXPORT(lv_group_get_obj_by_index),
    ESP_ELFSYM_EXPORT(lv_group_get_obj_count),
    ESP_ELFSYM_EXPORT(lv_group_get_user_data),
    ESP_ELFSYM_EXPORT(lv_group_get_wrap),
    ESP_ELFSYM_EXPORT(lv_group_init),
    ESP_ELFSYM_EXPORT(lv_group_remove_all_objs),
    ESP_ELFSYM_EXPORT(lv_group_remove_obj),
    ESP_ELFSYM_EXPORT(lv_group_send_data),
    ESP_ELFSYM_EXPORT(lv_group_set_default),
    ESP_ELFSYM_EXPORT(lv_group_set_edge_cb),
    ESP_ELFSYM_EXPORT(lv_group_set_editing),
    ESP_ELFSYM_EXPORT(lv_group_set_focus_cb),
    ESP_ELFSYM_EXPORT(lv_group_set_refocus_policy),
    ESP_ELFSYM_EXPORT(lv_group_set_user_data),
    ESP_ELFSYM_EXPORT(lv_group_set_wrap),
    ESP_ELFSYM_EXPORT(lv_group_swap_obj),
    ESP_ELFSYM_EXPORT(lv_image_bind_src),
    ESP_ELFSYM_EXPORT(lv_image_buf_free),
    ESP_ELFSYM_EXPORT(lv_image_buf_get_transformed_area),
    ESP_ELFSYM_EXPORT(lv_image_buf_set_palette),
    ESP_ELFSYM_EXPORT(lv_image_cache_drop),
    ESP_ELFSYM_EXPORT(lv_image_cache_dump),
    ESP_ELFSYM_EXPORT(lv_image_cache_init),
    ESP_ELFSYM_EXPORT(lv_image_cache_is_enabled),
    ESP_ELFSYM_EXPORT(lv_image_cache_iter_create),
    ESP_ELFSYM_EXPORT(lv_image_cache_resize),
    ESP_ELFSYM_EXPORT(lv_image_class),
    ESP_ELFSYM_EXPORT(lv_image_create),
    ESP_ELFSYM_EXPORT(lv_image_decoder_add_to_cache),
    ESP_ELFSYM_EXPORT(lv_image_decoder_close),
    ESP_ELFSYM_EXPORT(lv_image_decoder_create),
    ESP_ELFSYM_EXPORT(lv_image_decoder_deinit),
    ESP_ELFSYM_EXPORT(lv_image_decoder_delete),
    ESP_ELFSYM_EXPORT(lv_image_decoder_get_area),
    ESP_ELFSYM_EXPORT(lv_image_decoder_get_info),
    ESP_ELFSYM_EXPORT(lv_image_decoder_get_next),
    ESP_ELFSYM_EXPORT(lv_image_decoder_init),
    ESP_ELFSYM_EXPORT(lv_image_decoder_open),
    ESP_ELFSYM_EXPORT(lv_image_decoder_post_process),
    ESP_ELFSYM_EXPORT(lv_image_decoder_set_close_cb),
    ESP_ELFSYM_EXPORT(lv_image_decoder_set_get_area_cb),
    ESP_ELFSYM_EXPORT(lv_image_decoder_set_info_cb),
    ESP_ELFSYM_EXPORT(lv_image_decoder_set_open_cb),
    ESP_ELFSYM_EXPORT(lv_image_get_antialias),
    ESP_ELFSYM_EXPORT(lv_image_get_bitmap_map_src),
    ESP_ELFSYM_EXPORT(lv_image_get_blend_mode),
    ESP_ELFSYM_EXPORT(lv_image_get_inner_align),
    ESP_ELFSYM_EXPORT(lv_image_get_offset_x),
    ESP_ELFSYM_EXPORT(lv_image_get_offset_y),
    ESP_ELFSYM_EXPORT(lv_image_get_pivot),
    ESP_ELFSYM_EXPORT(lv_image_get_rotation),
    ESP_ELFSYM_EXPORT(lv_image_get_scale),
    ESP_ELFSYM_EXPORT(lv_image_get_scale_x),
    ESP_ELFSYM_EXPORT(lv_image_get_scale_y),
    ESP_ELFSYM_EXPORT(lv_image_get_src),
    ESP_ELFSYM_EXPORT(lv_image_get_src_height),
    ESP_ELFSYM_EXPORT(lv_image_get_src_width),
    ESP_ELFSYM_EXPORT(lv_image_get_transformed_height),
    ESP_ELFSYM_EXPORT(lv_image_get_transformed_width),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_drop),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_dump),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_init),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_is_enabled),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_iter_create),
    ESP_ELFSYM_EXPORT(lv_image_header_cache_resize),
    ESP_ELFSYM_EXPORT(lv_image_set_antialias),
    ESP_ELFSYM_EXPORT(lv_image_set_bitmap_map_src),
    ESP_ELFSYM_EXPORT(lv_image_set_blend_mode),
    ESP_ELFSYM_EXPORT(lv_image_set_inner_align),
    ESP_ELFSYM_EXPORT(lv_image_set_offset_x),
    ESP_ELFSYM_EXPORT(lv_image_set_offset_y),
    ESP_ELFSYM_EXPORT(lv_image_set_pivot),
    ESP_ELFSYM_EXPORT(lv_image_set_pivot_x),
    ESP_ELFSYM_EXPORT(lv_image_set_pivot_y),
    ESP_ELFSYM_EXPORT(lv_image_set_rotation),
    ESP_ELFSYM_EXPORT(lv_image_set_scale),
    ESP_ELFSYM_EXPORT(lv_image_set_scale_x),
    ESP_ELFSYM_EXPORT(lv_image_set_scale_y),
    ESP_ELFSYM_EXPORT(lv_image_set_src),
    ESP_ELFSYM_EXPORT(lv_image_src_get_type),
    ESP_ELFSYM_EXPORT(lv_imagebutton_class),
    ESP_ELFSYM_EXPORT(lv_imagebutton_create),
    ESP_ELFSYM_EXPORT(lv_imagebutton_get_src_left),
    ESP_ELFSYM_EXPORT(lv_imagebutton_get_src_middle),
    ESP_ELFSYM_EXPORT(lv_imagebutton_get_src_right),
    ESP_ELFSYM_EXPORT(lv_imagebutton_set_src),
    ESP_ELFSYM_EXPORT(lv_imagebutton_set_src_left),
    ESP_ELFSYM_EXPORT(lv_imagebutton_set_src_mid),
    ESP_ELFSYM_EXPORT(lv_imagebutton_set_src_right),
    ESP_ELFSYM_EXPORT(lv_imagebutton_set_state),
    ESP_ELFSYM_EXPORT(lv_indev_active),
    ESP_ELFSYM_EXPORT(lv_indev_add_event_cb),
    ESP_ELFSYM_EXPORT(lv_indev_create),
    ESP_ELFSYM_EXPORT(lv_indev_delete),
    ESP_ELFSYM_EXPORT(lv_indev_enable),
    ESP_ELFSYM_EXPORT(lv_indev_find_scroll_obj),
    ESP_ELFSYM_EXPORT(lv_indev_get_active_obj),
    ESP_ELFSYM_EXPORT(lv_indev_get_cursor),
    ESP_ELFSYM_EXPORT(lv_indev_get_display),
    ESP_ELFSYM_EXPORT(lv_indev_get_driver_data),
    ESP_ELFSYM_EXPORT(lv_indev_get_event_count),
    ESP_ELFSYM_EXPORT(lv_indev_get_event_dsc),
    ESP_ELFSYM_EXPORT(lv_indev_get_gesture_dir),
    ESP_ELFSYM_EXPORT(lv_indev_get_group),
    ESP_ELFSYM_EXPORT(lv_indev_get_key),
    ESP_ELFSYM_EXPORT(lv_indev_get_mode),
    ESP_ELFSYM_EXPORT(lv_indev_get_next),
    ESP_ELFSYM_EXPORT(lv_indev_get_point),
    ESP_ELFSYM_EXPORT(lv_indev_get_press_moved),
    ESP_ELFSYM_EXPORT(lv_indev_get_read_cb),
    ESP_ELFSYM_EXPORT(lv_indev_get_read_timer),
    ESP_ELFSYM_EXPORT(lv_indev_get_scroll_dir),
    ESP_ELFSYM_EXPORT(lv_indev_get_scroll_obj),
    ESP_ELFSYM_EXPORT(lv_indev_get_short_click_streak),
    ESP_ELFSYM_EXPORT(lv_indev_get_state),
    ESP_ELFSYM_EXPORT(lv_indev_get_type),
    ESP_ELFSYM_EXPORT(lv_indev_get_user_data),
    ESP_ELFSYM_EXPORT(lv_indev_get_vect),
    ESP_ELFSYM_EXPORT(lv_indev_read),
    ESP_ELFSYM_EXPORT(lv_indev_read_timer_cb),
    ESP_ELFSYM_EXPORT(lv_indev_remove_event),
    ESP_ELFSYM_EXPORT(lv_indev_remove_event_cb_with_user_data),
    ESP_ELFSYM_EXPORT(lv_indev_reset),
    ESP_ELFSYM_EXPORT(lv_indev_reset_long_press),
    ESP_ELFSYM_EXPORT(lv_indev_scroll_get_snap_dist),
    ESP_ELFSYM_EXPORT(lv_indev_scroll_handler),
    ESP_ELFSYM_EXPORT(lv_indev_scroll_throw_handler),
    ESP_ELFSYM_EXPORT(lv_indev_scroll_throw_predict),
    ESP_ELFSYM_EXPORT(lv_indev_search_obj),
    ESP_ELFSYM_EXPORT(lv_indev_send_event),
    ESP_ELFSYM_EXPORT(lv_indev_set_button_points),
    ESP_ELFSYM_EXPORT(lv_indev_set_cursor),
    ESP_ELFSYM_EXPORT(lv_indev_set_display),
    ESP_ELFSYM_EXPORT(lv_indev_set_driver_data),
    ESP_ELFSYM_EXPORT(lv_indev_set_gesture_min_distance),
    ESP_ELFSYM_EXPORT(lv_indev_set_gesture_min_velocity),
    ESP_ELFSYM_EXPORT(lv_indev_set_group),
    ESP_ELFSYM_EXPORT(lv_indev_set_key_remap_cb),
    ESP_ELFSYM_EXPORT(lv_indev_set_long_press_repeat_time),
    ESP_ELFSYM_EXPORT(lv_indev_set_long_press_time),
    ESP_ELFSYM_EXPORT(lv_indev_set_mode),
    ESP_ELFSYM_EXPORT(lv_indev_set_read_cb),
    ESP_ELFSYM_EXPORT(lv_indev_set_scroll_limit),
    ESP_ELFSYM_EXPORT(lv_indev_set_scroll_throw),
    ESP_ELFSYM_EXPORT(lv_indev_set_type),
    ESP_ELFSYM_EXPORT(lv_indev_set_user_data),
    ESP_ELFSYM_EXPORT(lv_indev_stop_processing),
    ESP_ELFSYM_EXPORT(lv_indev_wait_release),
    ESP_ELFSYM_EXPORT(lv_init),
    ESP_ELFSYM_EXPORT(lv_inv_area),
    ESP_ELFSYM_EXPORT(lv_is_initialized),
    ESP_ELFSYM_EXPORT(lv_iter_create),
    ESP_ELFSYM_EXPORT(lv_iter_destroy),
    ESP_ELFSYM_EXPORT(lv_iter_get_context),
    ESP_ELFSYM_EXPORT(lv_iter_inspect),
    ESP_ELFSYM_EXPORT(lv_iter_make_peekable),
    ESP_ELFSYM_EXPORT(lv_iter_next),
    ESP_ELFSYM_EXPORT(lv_iter_peek),
    ESP_ELFSYM_EXPORT(lv_iter_peek_advance),
    ESP_ELFSYM_EXPORT(lv_iter_peek_reset),
    ESP_ELFSYM_EXPORT(lv_keyboard_class),
    ESP_ELFSYM_EXPORT(lv_keyboard_create),
    ESP_ELFSYM_EXPORT(lv_keyboard_def_event_cb),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_button_text),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_map_array),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_mode),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_popovers),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_selected_button),
    ESP_ELFSYM_EXPORT(lv_keyboard_get_textarea),
    ESP_ELFSYM_EXPORT(lv_keyboard_set_map),
    ESP_ELFSYM_EXPORT(lv_keyboard_set_mode),
    ESP_ELFSYM_EXPORT(lv_keyboard_set_popovers),
    ESP_ELFSYM_EXPORT(lv_keyboard_set_textarea),
    ESP_ELFSYM_EXPORT(lv_label_bind_text),
    ESP_ELFSYM_EXPORT(lv_label_class),
    ESP_ELFSYM_EXPORT(lv_label_create),
    ESP_ELFSYM_EXPORT(lv_label_cut_text),
    ESP_ELFSYM_EXPORT(lv_label_get_letter_on),
    ESP_ELFSYM_EXPORT(lv_label_get_letter_pos),
    ESP_ELFSYM_EXPORT(lv_label_get_long_mode),
    ESP_ELFSYM_EXPORT(lv_label_get_recolor),
    ESP_ELFSYM_EXPORT(lv_label_get_text),
    ESP_ELFSYM_EXPORT(lv_label_get_text_selection_end),
    ESP_ELFSYM_EXPORT(lv_label_get_text_selection_start),
    ESP_ELFSYM_EXPORT(lv_label_ins_text),
    ESP_ELFSYM_EXPORT(lv_label_is_char_under_pos),
    ESP_ELFSYM_EXPORT(lv_label_set_long_mode),
    ESP_ELFSYM_EXPORT(lv_label_set_recolor),
    ESP_ELFSYM_EXPORT(lv_label_set_text),
    ESP_ELFSYM_EXPORT(lv_label_set_text_fmt),
    ESP_ELFSYM_EXPORT(lv_label_set_text_selection_end),
    ESP_ELFSYM_EXPORT(lv_label_set_text_selection_start),
    ESP_ELFSYM_EXPORT(lv_label_set_text_static),
    ESP_ELFSYM_EXPORT(lv_label_set_text_vfmt),
    ESP_ELFSYM_EXPORT(lv_layer_bottom),
    ESP_ELFSYM_EXPORT(lv_layer_init),
    ESP_ELFSYM_EXPORT(lv_layer_reset),
    ESP_ELFSYM_EXPORT(lv_layer_sys),
    ESP_ELFSYM_EXPORT(lv_layer_top),
    ESP_ELFSYM_EXPORT(lv_layout_apply),
    ESP_ELFSYM_EXPORT(lv_layout_create),
    ESP_ELFSYM_EXPORT(lv_layout_deinit),
    ESP_ELFSYM_EXPORT(lv_layout_get_min_size),
    ESP_ELFSYM_EXPORT(lv_layout_init),
    ESP_ELFSYM_EXPORT(lv_layout_register),
    ESP_ELFSYM_EXPORT(lv_led_class),
    ESP_ELFSYM_EXPORT(lv_led_create),
    ESP_ELFSYM_EXPORT(lv_led_get_brightness),
    ESP_ELFSYM_EXPORT(lv_led_get_color),
    ESP_ELFSYM_EXPORT(lv_led_off),
    ESP_ELFSYM_EXPORT(lv_led_on),
    ESP_ELFSYM_EXPORT(lv_led_set_brightness),
    ESP_ELFSYM_EXPORT(lv_led_set_color),
    ESP_ELFSYM_EXPORT(lv_led_toggle),
    ESP_ELFSYM_EXPORT(lv_line_class),
    ESP_ELFSYM_EXPORT(lv_line_create),
    ESP_ELFSYM_EXPORT(lv_line_get_point_count),
    ESP_ELFSYM_EXPORT(lv_line_get_points),
    ESP_ELFSYM_EXPORT(lv_line_get_points_mutable),
    ESP_ELFSYM_EXPORT(lv_line_get_y_invert),
    ESP_ELFSYM_EXPORT(lv_line_is_point_array_mutable),
    ESP_ELFSYM_EXPORT(lv_line_set_points),
    ESP_ELFSYM_EXPORT(lv_line_set_points_mutable),
    ESP_ELFSYM_EXPORT(lv_line_set_y_invert),
    ESP_ELFSYM_EXPORT(lv_list_add_button),
    ESP_ELFSYM_EXPORT(lv_list_add_text),
    ESP_ELFSYM_EXPORT(lv_list_button_class),
    ESP_ELFSYM_EXPORT(lv_list_class),
    ESP_ELFSYM_EXPORT(lv_list_create),
    ESP_ELFSYM_EXPORT(lv_list_get_button_text),
    ESP_ELFSYM_EXPORT(lv_list_set_button_text),
    ESP_ELFSYM_EXPORT(lv_list_text_class),
    ESP_ELFSYM_EXPORT(lv_ll_chg_list),
    ESP_ELFSYM_EXPORT(lv_ll_clear),
    ESP_ELFSYM_EXPORT(lv_ll_clear_custom),
    ESP_ELFSYM_EXPORT(lv_ll_get_head),
    ESP_ELFSYM_EXPORT(lv_ll_get_len),
    ESP_ELFSYM_EXPORT(lv_ll_get_next),
    ESP_ELFSYM_EXPORT(lv_ll_get_prev),
    ESP_ELFSYM_EXPORT(lv_ll_get_tail),
    ESP_ELFSYM_EXPORT(lv_ll_init),
    ESP_ELFSYM_EXPORT(lv_ll_ins_head),
    ESP_ELFSYM_EXPORT(lv_ll_ins_prev),
    ESP_ELFSYM_EXPORT(lv_ll_ins_tail),
    ESP_ELFSYM_EXPORT(lv_ll_is_empty),
    ESP_ELFSYM_EXPORT(lv_ll_move_before),
    ESP_ELFSYM_EXPORT(lv_ll_remove),
    ESP_ELFSYM_EXPORT(lv_lock),
    ESP_ELFSYM_EXPORT(lv_lock_isr),
    ESP_ELFSYM_EXPORT(lv_lodepng_deinit),
    ESP_ELFSYM_EXPORT(lv_lodepng_init),
    ESP_ELFSYM_EXPORT(lv_lru_create),
    ESP_ELFSYM_EXPORT(lv_lru_delete),
    ESP_ELFSYM_EXPORT(lv_lru_get),
    ESP_ELFSYM_EXPORT(lv_lru_remove),
    ESP_ELFSYM_EXPORT(lv_lru_remove_lru_item),
    ESP_ELFSYM_EXPORT(lv_lru_set),
    ESP_ELFSYM_EXPORT(lv_malloc),
    ESP_ELFSYM_EXPORT(lv_malloc_core),
    ESP_ELFSYM_EXPORT(lv_malloc_zeroed),
    ESP_ELFSYM_EXPORT(lv_map),
    ESP_ELFSYM_EXPORT(lv_mem_add_pool),
    ESP_ELFSYM_EXPORT(lv_mem_deinit),
    ESP_ELFSYM_EXPORT(lv_mem_init),
    ESP_ELFSYM_EXPORT(lv_mem_monitor),
    ESP_ELFSYM_EXPORT(lv_mem_monitor_core),
    ESP_ELFSYM_EXPORT(lv_mem_remove_pool),
    ESP_ELFSYM_EXPORT(lv_mem_test),
    ESP_ELFSYM_EXPORT(lv_mem_test_core),
    ESP_ELFSYM_EXPORT(lv_memcmp),
    ESP_ELFSYM_EXPORT(lv_memcpy),
    ESP_ELFSYM_EXPORT(lv_memmove),
    ESP_ELFSYM_EXPORT(lv_memset),
    ESP_ELFSYM_EXPORT(lv_menu_back_button_is_root),
    ESP_ELFSYM_EXPORT(lv_menu_class),
    ESP_ELFSYM_EXPORT(lv_menu_clear_history),
    ESP_ELFSYM_EXPORT(lv_menu_cont_class),
    ESP_ELFSYM_EXPORT(lv_menu_cont_create),
    ESP_ELFSYM_EXPORT(lv_menu_create),
    ESP_ELFSYM_EXPORT(lv_menu_get_cur_main_page),
    ESP_ELFSYM_EXPORT(lv_menu_get_cur_sidebar_page),
    ESP_ELFSYM_EXPORT(lv_menu_get_main_header),
    ESP_ELFSYM_EXPORT(lv_menu_get_main_header_back_button),
    ESP_ELFSYM_EXPORT(lv_menu_get_mode_header),
    ESP_ELFSYM_EXPORT(lv_menu_get_mode_root_back_button),
    ESP_ELFSYM_EXPORT(lv_menu_get_sidebar_header),
    ESP_ELFSYM_EXPORT(lv_menu_get_sidebar_header_back_button),
    ESP_ELFSYM_EXPORT(lv_menu_main_cont_class),
    ESP_ELFSYM_EXPORT(lv_menu_main_header_cont_class),
    ESP_ELFSYM_EXPORT(lv_menu_page_class),
    ESP_ELFSYM_EXPORT(lv_menu_page_create),
    ESP_ELFSYM_EXPORT(lv_menu_section_class),
    ESP_ELFSYM_EXPORT(lv_menu_section_create),
    ESP_ELFSYM_EXPORT(lv_menu_separator_class),
    ESP_ELFSYM_EXPORT(lv_menu_separator_create),
    ESP_ELFSYM_EXPORT(lv_menu_set_load_page_event),
    ESP_ELFSYM_EXPORT(lv_menu_set_mode_header),
    ESP_ELFSYM_EXPORT(lv_menu_set_mode_root_back_button),
    ESP_ELFSYM_EXPORT(lv_menu_set_page),
    ESP_ELFSYM_EXPORT(lv_menu_set_page_title),
    ESP_ELFSYM_EXPORT(lv_menu_set_page_title_static),
    ESP_ELFSYM_EXPORT(lv_menu_set_sidebar_page),
    ESP_ELFSYM_EXPORT(lv_menu_sidebar_cont_class),
    ESP_ELFSYM_EXPORT(lv_menu_sidebar_header_cont_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_close_button),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_footer_button),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_header_button),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_text),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_text_fmt),
    ESP_ELFSYM_EXPORT(lv_msgbox_add_title),
    ESP_ELFSYM_EXPORT(lv_msgbox_backdrop_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_close),
    ESP_ELFSYM_EXPORT(lv_msgbox_close_async),
    ESP_ELFSYM_EXPORT(lv_msgbox_content_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_create),
    ESP_ELFSYM_EXPORT(lv_msgbox_footer_button_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_footer_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_get_content),
    ESP_ELFSYM_EXPORT(lv_msgbox_get_footer),
    ESP_ELFSYM_EXPORT(lv_msgbox_get_header),
    ESP_ELFSYM_EXPORT(lv_msgbox_get_title),
    ESP_ELFSYM_EXPORT(lv_msgbox_header_button_class),
    ESP_ELFSYM_EXPORT(lv_msgbox_header_class),
    ESP_ELFSYM_EXPORT(lv_obj_add_event_cb),
    ESP_ELFSYM_EXPORT(lv_obj_add_flag),
    ESP_ELFSYM_EXPORT(lv_obj_add_play_timeline_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_screen_create_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_screen_load_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_state),
    ESP_ELFSYM_EXPORT(lv_obj_add_style),
    ESP_ELFSYM_EXPORT(lv_obj_add_subject_increment_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_subject_set_int_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_subject_set_string_event),
    ESP_ELFSYM_EXPORT(lv_obj_add_subject_toggle_event),
    ESP_ELFSYM_EXPORT(lv_obj_align),
    ESP_ELFSYM_EXPORT(lv_obj_align_to),
    ESP_ELFSYM_EXPORT(lv_obj_allocate_spec_attr),
    ESP_ELFSYM_EXPORT(lv_obj_area_is_visible),
    ESP_ELFSYM_EXPORT(lv_obj_bind_checked),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_eq),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_ge),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_gt),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_le),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_lt),
    ESP_ELFSYM_EXPORT(lv_obj_bind_flag_if_not_eq),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_eq),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_ge),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_gt),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_le),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_lt),
    ESP_ELFSYM_EXPORT(lv_obj_bind_state_if_not_eq),
    ESP_ELFSYM_EXPORT(lv_obj_bind_style),
    ESP_ELFSYM_EXPORT(lv_obj_bind_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_calc_dynamic_height),
    ESP_ELFSYM_EXPORT(lv_obj_calc_dynamic_width),
    ESP_ELFSYM_EXPORT(lv_obj_calculate_ext_draw_size),
    ESP_ELFSYM_EXPORT(lv_obj_calculate_style_text_align),
    ESP_ELFSYM_EXPORT(lv_obj_center),
    ESP_ELFSYM_EXPORT(lv_obj_check_type),
    ESP_ELFSYM_EXPORT(lv_obj_class),
    ESP_ELFSYM_EXPORT(lv_obj_class_create_obj),
    ESP_ELFSYM_EXPORT(lv_obj_class_init_obj),
    ESP_ELFSYM_EXPORT(lv_obj_clean),
    ESP_ELFSYM_EXPORT(lv_obj_create),
    ESP_ELFSYM_EXPORT(lv_obj_delete),
    ESP_ELFSYM_EXPORT(lv_obj_delete_anim_completed_cb),
    ESP_ELFSYM_EXPORT(lv_obj_delete_async),
    ESP_ELFSYM_EXPORT(lv_obj_delete_delayed),
    ESP_ELFSYM_EXPORT(lv_obj_destruct),
    ESP_ELFSYM_EXPORT(lv_obj_dump_tree),
    ESP_ELFSYM_EXPORT(lv_obj_enable_style_refresh),
    ESP_ELFSYM_EXPORT(lv_obj_event_base),
    ESP_ELFSYM_EXPORT(lv_obj_fade_in),
    ESP_ELFSYM_EXPORT(lv_obj_fade_out),
    ESP_ELFSYM_EXPORT(lv_obj_get_child),
    ESP_ELFSYM_EXPORT(lv_obj_get_child_by_type),
    ESP_ELFSYM_EXPORT(lv_obj_get_child_count),
    ESP_ELFSYM_EXPORT(lv_obj_get_child_count_by_type),
    ESP_ELFSYM_EXPORT(lv_obj_get_class),
    ESP_ELFSYM_EXPORT(lv_obj_get_click_area),
    ESP_ELFSYM_EXPORT(lv_obj_get_content_coords),
    ESP_ELFSYM_EXPORT(lv_obj_get_content_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_content_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_coords),
    ESP_ELFSYM_EXPORT(lv_obj_get_display),
    ESP_ELFSYM_EXPORT(lv_obj_get_event_count),
    ESP_ELFSYM_EXPORT(lv_obj_get_event_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_get_ext_draw_size),
    ESP_ELFSYM_EXPORT(lv_obj_get_group),
    ESP_ELFSYM_EXPORT(lv_obj_get_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_index),
    ESP_ELFSYM_EXPORT(lv_obj_get_index_by_type),
    ESP_ELFSYM_EXPORT(lv_obj_get_layer_type),
    ESP_ELFSYM_EXPORT(lv_obj_get_local_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_get_parent),
    ESP_ELFSYM_EXPORT(lv_obj_get_screen),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_bottom),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_dir),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_end),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_left),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_right),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_snap_x),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_snap_y),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_top),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_x),
    ESP_ELFSYM_EXPORT(lv_obj_get_scroll_y),
    ESP_ELFSYM_EXPORT(lv_obj_get_scrollbar_area),
    ESP_ELFSYM_EXPORT(lv_obj_get_scrollbar_mode),
    ESP_ELFSYM_EXPORT(lv_obj_get_self_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_self_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_sibling),
    ESP_ELFSYM_EXPORT(lv_obj_get_sibling_by_type),
    ESP_ELFSYM_EXPORT(lv_obj_get_state),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_clamped_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_clamped_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_opa_recursive),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_recolor_recursive),
    ESP_ELFSYM_EXPORT(lv_obj_get_transform),
    ESP_ELFSYM_EXPORT(lv_obj_get_transformed_area),
    ESP_ELFSYM_EXPORT(lv_obj_get_user_data),
    ESP_ELFSYM_EXPORT(lv_obj_get_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_x),
    ESP_ELFSYM_EXPORT(lv_obj_get_x2),
    ESP_ELFSYM_EXPORT(lv_obj_get_x_aligned),
    ESP_ELFSYM_EXPORT(lv_obj_get_y),
    ESP_ELFSYM_EXPORT(lv_obj_get_y2),
    ESP_ELFSYM_EXPORT(lv_obj_get_y_aligned),
    ESP_ELFSYM_EXPORT(lv_obj_has_class),
    ESP_ELFSYM_EXPORT(lv_obj_has_flag),
    ESP_ELFSYM_EXPORT(lv_obj_has_flag_any),
    ESP_ELFSYM_EXPORT(lv_obj_has_state),
    ESP_ELFSYM_EXPORT(lv_obj_has_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_hit_test),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_arc_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_blur_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_image_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_label_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_line_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_init_draw_rect_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_invalidate),
    ESP_ELFSYM_EXPORT(lv_obj_invalidate_area),
    ESP_ELFSYM_EXPORT(lv_obj_is_editable),
    ESP_ELFSYM_EXPORT(lv_obj_is_group_def),
    ESP_ELFSYM_EXPORT(lv_obj_is_height_max),
    ESP_ELFSYM_EXPORT(lv_obj_is_height_min),
    ESP_ELFSYM_EXPORT(lv_obj_is_layout_positioned),
    ESP_ELFSYM_EXPORT(lv_obj_is_radio_button),
    ESP_ELFSYM_EXPORT(lv_obj_is_scrolling),
    ESP_ELFSYM_EXPORT(lv_obj_is_valid),
    ESP_ELFSYM_EXPORT(lv_obj_is_visible),
    ESP_ELFSYM_EXPORT(lv_obj_is_width_max),
    ESP_ELFSYM_EXPORT(lv_obj_is_width_min),
    ESP_ELFSYM_EXPORT(lv_obj_mark_layout_as_dirty),
    ESP_ELFSYM_EXPORT(lv_obj_move_children_by),
    ESP_ELFSYM_EXPORT(lv_obj_move_to),
    ESP_ELFSYM_EXPORT(lv_obj_move_to_index),
    ESP_ELFSYM_EXPORT(lv_obj_null_on_delete),
    ESP_ELFSYM_EXPORT(lv_obj_readjust_scroll),
    ESP_ELFSYM_EXPORT(lv_obj_redraw),
    ESP_ELFSYM_EXPORT(lv_obj_refr),
    ESP_ELFSYM_EXPORT(lv_obj_refr_pos),
    ESP_ELFSYM_EXPORT(lv_obj_refr_size),
    ESP_ELFSYM_EXPORT(lv_obj_refresh_ext_draw_size),
    ESP_ELFSYM_EXPORT(lv_obj_refresh_self_size),
    ESP_ELFSYM_EXPORT(lv_obj_refresh_style),
    ESP_ELFSYM_EXPORT(lv_obj_remove_event),
    ESP_ELFSYM_EXPORT(lv_obj_remove_event_cb),
    ESP_ELFSYM_EXPORT(lv_obj_remove_event_cb_with_user_data),
    ESP_ELFSYM_EXPORT(lv_obj_remove_event_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_remove_flag),
    ESP_ELFSYM_EXPORT(lv_obj_remove_from_subject),
    ESP_ELFSYM_EXPORT(lv_obj_remove_local_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_remove_state),
    ESP_ELFSYM_EXPORT(lv_obj_remove_style),
    ESP_ELFSYM_EXPORT(lv_obj_remove_style_all),
    ESP_ELFSYM_EXPORT(lv_obj_remove_theme),
    ESP_ELFSYM_EXPORT(lv_obj_replace_style),
    ESP_ELFSYM_EXPORT(lv_obj_report_style_change),
    ESP_ELFSYM_EXPORT(lv_obj_reset_transform),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_by),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_by_bounded),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_by_raw),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_view),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_view_recursive),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_x),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_y),
    ESP_ELFSYM_EXPORT(lv_obj_scrollbar_invalidate),
    ESP_ELFSYM_EXPORT(lv_obj_send_event),
    ESP_ELFSYM_EXPORT(lv_obj_set_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_content_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_content_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_ext_click_area),
    ESP_ELFSYM_EXPORT(lv_obj_set_flag),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_flow),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_grow),
    ESP_ELFSYM_EXPORT(lv_obj_set_grid_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_grid_cell),
    ESP_ELFSYM_EXPORT(lv_obj_set_grid_dsc_array),
    ESP_ELFSYM_EXPORT(lv_obj_set_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_layout),
    ESP_ELFSYM_EXPORT(lv_obj_set_local_style_prop),
    ESP_ELFSYM_EXPORT(lv_obj_set_parent),
    ESP_ELFSYM_EXPORT(lv_obj_set_pos),
    ESP_ELFSYM_EXPORT(lv_obj_set_radio_button),
    ESP_ELFSYM_EXPORT(lv_obj_set_scroll_dir),
    ESP_ELFSYM_EXPORT(lv_obj_set_scroll_snap_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_scroll_snap_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_scrollbar_mode),
    ESP_ELFSYM_EXPORT(lv_obj_set_size),
    ESP_ELFSYM_EXPORT(lv_obj_set_state),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_anim),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_anim_duration),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_arc_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_arc_image_src),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_arc_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_arc_rounded),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_arc_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_base_dir),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_grad),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_grad_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_grad_dir),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_grad_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_grad_stop),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_recolor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_src),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_tiled),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_main_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_main_stop),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bitmap_mask_src),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_blend_mode),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_blur_backdrop),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_blur_quality),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_blur_radius),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_post),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_side),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_clip_corner),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_color_filter_dsc),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_color_filter_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_offset_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_offset_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_quality),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_drop_shadow_radius),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_flex_cross_place),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_flex_flow),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_flex_grow),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_flex_main_place),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_flex_track_place),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_column_pos),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_column_span),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_row_pos),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_row_span),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_x_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_cell_y_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_column_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_column_dsc_array),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_row_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_grid_row_dsc_array),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_image_colorkey),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_image_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_image_recolor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_image_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_layout),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_length),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_dash_gap),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_dash_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_rounded),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_bottom),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_left),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_right),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_top),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_max_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_max_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_min_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_min_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_opa_layered),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_pad),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_bottom),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_column),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_left),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_radial),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_right),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_row),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_top),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_radial_offset),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_radius),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_recolor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_rotary_sensitivity),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_offset_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_offset_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_spread),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_shadow_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_decor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_font),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_letter_space),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_line_space),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_height),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_pivot_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_pivot_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_rotation),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_scale_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_scale_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_skew_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_skew_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transform_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_transition),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_translate_radial),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_translate_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_translate_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_subject_increment_event_max_value),
    ESP_ELFSYM_EXPORT(lv_obj_set_subject_increment_event_min_value),
    ESP_ELFSYM_EXPORT(lv_obj_set_subject_increment_event_rollover),
    ESP_ELFSYM_EXPORT(lv_obj_set_transform),
    ESP_ELFSYM_EXPORT(lv_obj_set_user_data),
    ESP_ELFSYM_EXPORT(lv_obj_set_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_y),
    ESP_ELFSYM_EXPORT(lv_obj_stop_scroll_anim),
    ESP_ELFSYM_EXPORT(lv_obj_style_apply_color_filter),
    ESP_ELFSYM_EXPORT(lv_obj_style_apply_recolor),
    ESP_ELFSYM_EXPORT(lv_obj_style_create_transition),
    ESP_ELFSYM_EXPORT(lv_obj_style_deinit),
    ESP_ELFSYM_EXPORT(lv_obj_style_get_disabled),
    ESP_ELFSYM_EXPORT(lv_obj_style_init),
    ESP_ELFSYM_EXPORT(lv_obj_style_set_disabled),
    ESP_ELFSYM_EXPORT(lv_obj_style_state_compare),
    ESP_ELFSYM_EXPORT(lv_obj_swap),
    ESP_ELFSYM_EXPORT(lv_obj_transform_point),
    ESP_ELFSYM_EXPORT(lv_obj_transform_point_array),
    ESP_ELFSYM_EXPORT(lv_obj_tree_walk),
    ESP_ELFSYM_EXPORT(lv_obj_update_layer_type),
    ESP_ELFSYM_EXPORT(lv_obj_update_layout),
    ESP_ELFSYM_EXPORT(lv_obj_update_snap),
    ESP_ELFSYM_EXPORT(lv_observer_get_target),
    ESP_ELFSYM_EXPORT(lv_observer_get_target_obj),
    ESP_ELFSYM_EXPORT(lv_observer_get_user_data),
    ESP_ELFSYM_EXPORT(lv_observer_remove),
    ESP_ELFSYM_EXPORT(lv_os_get_idle_percent),
    ESP_ELFSYM_EXPORT(lv_os_init),
    ESP_ELFSYM_EXPORT(lv_palette_darken),
    ESP_ELFSYM_EXPORT(lv_palette_lighten),
    ESP_ELFSYM_EXPORT(lv_palette_main),
    ESP_ELFSYM_EXPORT(lv_pct),
    ESP_ELFSYM_EXPORT(lv_pct_to_px),
    ESP_ELFSYM_EXPORT(lv_pending_add),
    ESP_ELFSYM_EXPORT(lv_pending_create),
    ESP_ELFSYM_EXPORT(lv_pending_destroy),
    ESP_ELFSYM_EXPORT(lv_pending_remove_all),
    ESP_ELFSYM_EXPORT(lv_pending_set_free_cb),
    ESP_ELFSYM_EXPORT(lv_pending_swap),
    ESP_ELFSYM_EXPORT(lv_point_array_transform),
    ESP_ELFSYM_EXPORT(lv_point_from_precise),
    ESP_ELFSYM_EXPORT(lv_point_precise_set),
    ESP_ELFSYM_EXPORT(lv_point_precise_swap),
    ESP_ELFSYM_EXPORT(lv_point_set),
    ESP_ELFSYM_EXPORT(lv_point_swap),
    ESP_ELFSYM_EXPORT(lv_point_to_precise),
    ESP_ELFSYM_EXPORT(lv_point_transform),
    ESP_ELFSYM_EXPORT(lv_pow),
    ESP_ELFSYM_EXPORT(lv_qrcode_class),
    ESP_ELFSYM_EXPORT(lv_qrcode_create),
    ESP_ELFSYM_EXPORT(lv_qrcode_set_dark_color),
    ESP_ELFSYM_EXPORT(lv_qrcode_set_data),
    ESP_ELFSYM_EXPORT(lv_qrcode_set_light_color),
    ESP_ELFSYM_EXPORT(lv_qrcode_set_quiet_zone),
    ESP_ELFSYM_EXPORT(lv_qrcode_set_size),
    ESP_ELFSYM_EXPORT(lv_qrcode_update),
    ESP_ELFSYM_EXPORT(lv_rand),
    ESP_ELFSYM_EXPORT(lv_rand_set_seed),
    ESP_ELFSYM_EXPORT(lv_rb_destroy),
    ESP_ELFSYM_EXPORT(lv_rb_drop),
    ESP_ELFSYM_EXPORT(lv_rb_drop_node),
    ESP_ELFSYM_EXPORT(lv_rb_find),
    ESP_ELFSYM_EXPORT(lv_rb_init),
    ESP_ELFSYM_EXPORT(lv_rb_insert),
    ESP_ELFSYM_EXPORT(lv_rb_maximum),
    ESP_ELFSYM_EXPORT(lv_rb_maximum_from),
    ESP_ELFSYM_EXPORT(lv_rb_minimum),
    ESP_ELFSYM_EXPORT(lv_rb_minimum_from),
    ESP_ELFSYM_EXPORT(lv_rb_remove),
    ESP_ELFSYM_EXPORT(lv_rb_remove_node),
    ESP_ELFSYM_EXPORT(lv_realloc),
    ESP_ELFSYM_EXPORT(lv_realloc_core),
    ESP_ELFSYM_EXPORT(lv_reallocf),
    ESP_ELFSYM_EXPORT(lv_refr_deinit),
    ESP_ELFSYM_EXPORT(lv_refr_get_disp_refreshing),
    ESP_ELFSYM_EXPORT(lv_refr_get_top_obj),
    ESP_ELFSYM_EXPORT(lv_refr_init),
    ESP_ELFSYM_EXPORT(lv_refr_now),
    ESP_ELFSYM_EXPORT(lv_refr_set_disp_refreshing),
    ESP_ELFSYM_EXPORT(lv_roller_bind_value),
    ESP_ELFSYM_EXPORT(lv_roller_class),
    ESP_ELFSYM_EXPORT(lv_roller_create),
    ESP_ELFSYM_EXPORT(lv_roller_get_option_count),
    ESP_ELFSYM_EXPORT(lv_roller_get_option_str),
    ESP_ELFSYM_EXPORT(lv_roller_get_options),
    ESP_ELFSYM_EXPORT(lv_roller_get_selected),
    ESP_ELFSYM_EXPORT(lv_roller_get_selected_str),
    ESP_ELFSYM_EXPORT(lv_roller_label_class),
    ESP_ELFSYM_EXPORT(lv_roller_set_options),
    ESP_ELFSYM_EXPORT(lv_roller_set_selected),
    ESP_ELFSYM_EXPORT(lv_roller_set_selected_str),
    ESP_ELFSYM_EXPORT(lv_roller_set_visible_row_count),
    ESP_ELFSYM_EXPORT(lv_scale_add_section),
    ESP_ELFSYM_EXPORT(lv_scale_bind_section_max_value),
    ESP_ELFSYM_EXPORT(lv_scale_bind_section_min_value),
    ESP_ELFSYM_EXPORT(lv_scale_class),
    ESP_ELFSYM_EXPORT(lv_scale_create),
    ESP_ELFSYM_EXPORT(lv_scale_get_angle_range),
    ESP_ELFSYM_EXPORT(lv_scale_get_label_show),
    ESP_ELFSYM_EXPORT(lv_scale_get_major_tick_every),
    ESP_ELFSYM_EXPORT(lv_scale_get_mode),
    ESP_ELFSYM_EXPORT(lv_scale_get_range_max_value),
    ESP_ELFSYM_EXPORT(lv_scale_get_range_min_value),
    ESP_ELFSYM_EXPORT(lv_scale_get_rotation),
    ESP_ELFSYM_EXPORT(lv_scale_get_total_tick_count),
    ESP_ELFSYM_EXPORT(lv_scale_section_set_range),
    ESP_ELFSYM_EXPORT(lv_scale_section_set_style),
    ESP_ELFSYM_EXPORT(lv_scale_set_angle_range),
    ESP_ELFSYM_EXPORT(lv_scale_set_draw_ticks_on_top),
    ESP_ELFSYM_EXPORT(lv_scale_set_image_needle_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_label_show),
    ESP_ELFSYM_EXPORT(lv_scale_set_line_needle_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_major_tick_every),
    ESP_ELFSYM_EXPORT(lv_scale_set_max_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_min_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_mode),
    ESP_ELFSYM_EXPORT(lv_scale_set_post_draw),
    ESP_ELFSYM_EXPORT(lv_scale_set_range),
    ESP_ELFSYM_EXPORT(lv_scale_set_rotation),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_max_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_min_value),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_range),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_style_indicator),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_style_items),
    ESP_ELFSYM_EXPORT(lv_scale_set_section_style_main),
    ESP_ELFSYM_EXPORT(lv_scale_set_text_src),
    ESP_ELFSYM_EXPORT(lv_scale_set_total_tick_count),
    ESP_ELFSYM_EXPORT(lv_screen_active),
    ESP_ELFSYM_EXPORT(lv_screen_load),
    ESP_ELFSYM_EXPORT(lv_screen_load_anim),
    ESP_ELFSYM_EXPORT(lv_sleep_ms),
    ESP_ELFSYM_EXPORT(lv_slider_bind_value),
    ESP_ELFSYM_EXPORT(lv_slider_class),
    ESP_ELFSYM_EXPORT(lv_slider_create),
    ESP_ELFSYM_EXPORT(lv_slider_get_left_value),
    ESP_ELFSYM_EXPORT(lv_slider_get_max_value),
    ESP_ELFSYM_EXPORT(lv_slider_get_min_value),
    ESP_ELFSYM_EXPORT(lv_slider_get_mode),
    ESP_ELFSYM_EXPORT(lv_slider_get_orientation),
    ESP_ELFSYM_EXPORT(lv_slider_get_value),
    ESP_ELFSYM_EXPORT(lv_slider_is_dragged),
    ESP_ELFSYM_EXPORT(lv_slider_is_symmetrical),
    ESP_ELFSYM_EXPORT(lv_slider_set_max_value),
    ESP_ELFSYM_EXPORT(lv_slider_set_min_value),
    ESP_ELFSYM_EXPORT(lv_slider_set_mode),
    ESP_ELFSYM_EXPORT(lv_slider_set_orientation),
    ESP_ELFSYM_EXPORT(lv_slider_set_range),
    ESP_ELFSYM_EXPORT(lv_slider_set_start_value),
    ESP_ELFSYM_EXPORT(lv_slider_set_value),
    ESP_ELFSYM_EXPORT(lv_snapshot_create_draw_buf),
    ESP_ELFSYM_EXPORT(lv_snapshot_free),
    ESP_ELFSYM_EXPORT(lv_snapshot_reshape_draw_buf),
    ESP_ELFSYM_EXPORT(lv_snapshot_take),
    ESP_ELFSYM_EXPORT(lv_snapshot_take_to_buf),
    ESP_ELFSYM_EXPORT(lv_snapshot_take_to_draw_buf),
    ESP_ELFSYM_EXPORT(lv_snprintf),
    ESP_ELFSYM_EXPORT(lv_span_get_style),
    ESP_ELFSYM_EXPORT(lv_span_get_text),
    ESP_ELFSYM_EXPORT(lv_span_set_text),
    ESP_ELFSYM_EXPORT(lv_span_set_text_fmt),
    ESP_ELFSYM_EXPORT(lv_span_set_text_static),
    ESP_ELFSYM_EXPORT(lv_span_stack_deinit),
    ESP_ELFSYM_EXPORT(lv_span_stack_init),
    ESP_ELFSYM_EXPORT(lv_spangroup_add_span),
    ESP_ELFSYM_EXPORT(lv_spangroup_bind_span_text),
    ESP_ELFSYM_EXPORT(lv_spangroup_class),
    ESP_ELFSYM_EXPORT(lv_spangroup_create),
    ESP_ELFSYM_EXPORT(lv_spangroup_delete_span),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_align),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_child),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_expand_height),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_expand_width),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_indent),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_max_line_height),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_max_lines),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_mode),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_overflow),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_span_by_point),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_span_coords),
    ESP_ELFSYM_EXPORT(lv_spangroup_get_span_count),
    ESP_ELFSYM_EXPORT(lv_spangroup_refresh),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_align),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_indent),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_max_lines),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_mode),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_overflow),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_span_style),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_span_text),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_span_text_fmt),
    ESP_ELFSYM_EXPORT(lv_spangroup_set_span_text_static),
    ESP_ELFSYM_EXPORT(lv_spinbox_bind_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_class),
    ESP_ELFSYM_EXPORT(lv_spinbox_create),
    ESP_ELFSYM_EXPORT(lv_spinbox_decrement),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_dec_point_pos),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_digit_count),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_digit_step_direction),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_max_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_min_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_rollover),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_step),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_increment),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_cursor_pos),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_dec_point_pos),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_digit_count),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_digit_format),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_digit_step_direction),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_max_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_min_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_range),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_rollover),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_step),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_step_next),
    ESP_ELFSYM_EXPORT(lv_spinbox_step_prev),
    ESP_ELFSYM_EXPORT(lv_spinner_class),
    ESP_ELFSYM_EXPORT(lv_spinner_create),
    ESP_ELFSYM_EXPORT(lv_spinner_get_anim_duration),
    ESP_ELFSYM_EXPORT(lv_spinner_get_arc_sweep),
    ESP_ELFSYM_EXPORT(lv_spinner_set_anim_duration),
    ESP_ELFSYM_EXPORT(lv_spinner_set_anim_params),
    ESP_ELFSYM_EXPORT(lv_spinner_set_arc_sweep),
    ESP_ELFSYM_EXPORT(lv_sqrt),
    ESP_ELFSYM_EXPORT(lv_sqrt32),
    ESP_ELFSYM_EXPORT(lv_strcat),
    ESP_ELFSYM_EXPORT(lv_strchr),
    ESP_ELFSYM_EXPORT(lv_strcmp),
    ESP_ELFSYM_EXPORT(lv_strcpy),
    ESP_ELFSYM_EXPORT(lv_strdup),
    ESP_ELFSYM_EXPORT(lv_strlcpy),
    ESP_ELFSYM_EXPORT(lv_strlen),
    ESP_ELFSYM_EXPORT(lv_strncat),
    ESP_ELFSYM_EXPORT(lv_strncmp),
    ESP_ELFSYM_EXPORT(lv_strncpy),
    ESP_ELFSYM_EXPORT(lv_strndup),
    ESP_ELFSYM_EXPORT(lv_strnlen),
    ESP_ELFSYM_EXPORT(lv_style_builtin_prop_flag_lookup_table),
    ESP_ELFSYM_EXPORT(lv_style_const_prop_id_inv),
    ESP_ELFSYM_EXPORT(lv_style_copy),
    ESP_ELFSYM_EXPORT(lv_style_get_num_custom_props),
    ESP_ELFSYM_EXPORT(lv_style_get_prop),
    ESP_ELFSYM_EXPORT(lv_style_init),
    ESP_ELFSYM_EXPORT(lv_style_is_empty),
    ESP_ELFSYM_EXPORT(lv_style_merge),
    ESP_ELFSYM_EXPORT(lv_style_prop_get_default),
    ESP_ELFSYM_EXPORT(lv_style_prop_lookup_flags),
    ESP_ELFSYM_EXPORT(lv_style_register_prop),
    ESP_ELFSYM_EXPORT(lv_style_remove_prop),
    ESP_ELFSYM_EXPORT(lv_style_reset),
    ESP_ELFSYM_EXPORT(lv_style_set_align),
    ESP_ELFSYM_EXPORT(lv_style_set_anim),
    ESP_ELFSYM_EXPORT(lv_style_set_anim_duration),
    ESP_ELFSYM_EXPORT(lv_style_set_arc_color),
    ESP_ELFSYM_EXPORT(lv_style_set_arc_image_src),
    ESP_ELFSYM_EXPORT(lv_style_set_arc_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_arc_rounded),
    ESP_ELFSYM_EXPORT(lv_style_set_arc_width),
    ESP_ELFSYM_EXPORT(lv_style_set_base_dir),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_color),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_grad),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_grad_color),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_grad_dir),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_grad_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_grad_stop),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_image_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_image_recolor),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_image_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_image_src),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_image_tiled),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_main_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_main_stop),
    ESP_ELFSYM_EXPORT(lv_style_set_bg_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_bitmap_mask_src),
    ESP_ELFSYM_EXPORT(lv_style_set_blend_mode),
    ESP_ELFSYM_EXPORT(lv_style_set_blur_backdrop),
    ESP_ELFSYM_EXPORT(lv_style_set_blur_quality),
    ESP_ELFSYM_EXPORT(lv_style_set_blur_radius),
    ESP_ELFSYM_EXPORT(lv_style_set_border_color),
    ESP_ELFSYM_EXPORT(lv_style_set_border_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_border_post),
    ESP_ELFSYM_EXPORT(lv_style_set_border_side),
    ESP_ELFSYM_EXPORT(lv_style_set_border_width),
    ESP_ELFSYM_EXPORT(lv_style_set_clip_corner),
    ESP_ELFSYM_EXPORT(lv_style_set_color_filter_dsc),
    ESP_ELFSYM_EXPORT(lv_style_set_color_filter_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_color),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_offset_x),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_offset_y),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_quality),
    ESP_ELFSYM_EXPORT(lv_style_set_drop_shadow_radius),
    ESP_ELFSYM_EXPORT(lv_style_set_flex_cross_place),
    ESP_ELFSYM_EXPORT(lv_style_set_flex_flow),
    ESP_ELFSYM_EXPORT(lv_style_set_flex_grow),
    ESP_ELFSYM_EXPORT(lv_style_set_flex_main_place),
    ESP_ELFSYM_EXPORT(lv_style_set_flex_track_place),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_column_pos),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_column_span),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_row_pos),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_row_span),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_x_align),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_cell_y_align),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_column_align),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_column_dsc_array),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_row_align),
    ESP_ELFSYM_EXPORT(lv_style_set_grid_row_dsc_array),
    ESP_ELFSYM_EXPORT(lv_style_set_height),
    ESP_ELFSYM_EXPORT(lv_style_set_image_colorkey),
    ESP_ELFSYM_EXPORT(lv_style_set_image_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_image_recolor),
    ESP_ELFSYM_EXPORT(lv_style_set_image_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_layout),
    ESP_ELFSYM_EXPORT(lv_style_set_length),
    ESP_ELFSYM_EXPORT(lv_style_set_line_color),
    ESP_ELFSYM_EXPORT(lv_style_set_line_dash_gap),
    ESP_ELFSYM_EXPORT(lv_style_set_line_dash_width),
    ESP_ELFSYM_EXPORT(lv_style_set_line_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_line_rounded),
    ESP_ELFSYM_EXPORT(lv_style_set_line_width),
    ESP_ELFSYM_EXPORT(lv_style_set_margin_bottom),
    ESP_ELFSYM_EXPORT(lv_style_set_margin_left),
    ESP_ELFSYM_EXPORT(lv_style_set_margin_right),
    ESP_ELFSYM_EXPORT(lv_style_set_margin_top),
    ESP_ELFSYM_EXPORT(lv_style_set_max_height),
    ESP_ELFSYM_EXPORT(lv_style_set_max_width),
    ESP_ELFSYM_EXPORT(lv_style_set_min_height),
    ESP_ELFSYM_EXPORT(lv_style_set_min_width),
    ESP_ELFSYM_EXPORT(lv_style_set_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_opa_layered),
    ESP_ELFSYM_EXPORT(lv_style_set_outline_color),
    ESP_ELFSYM_EXPORT(lv_style_set_outline_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_outline_pad),
    ESP_ELFSYM_EXPORT(lv_style_set_outline_width),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_bottom),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_column),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_left),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_radial),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_right),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_row),
    ESP_ELFSYM_EXPORT(lv_style_set_pad_top),
    ESP_ELFSYM_EXPORT(lv_style_set_prop),
    ESP_ELFSYM_EXPORT(lv_style_set_radial_offset),
    ESP_ELFSYM_EXPORT(lv_style_set_radius),
    ESP_ELFSYM_EXPORT(lv_style_set_recolor),
    ESP_ELFSYM_EXPORT(lv_style_set_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_rotary_sensitivity),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_color),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_offset_x),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_offset_y),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_spread),
    ESP_ELFSYM_EXPORT(lv_style_set_shadow_width),
    ESP_ELFSYM_EXPORT(lv_style_set_text_align),
    ESP_ELFSYM_EXPORT(lv_style_set_text_color),
    ESP_ELFSYM_EXPORT(lv_style_set_text_decor),
    ESP_ELFSYM_EXPORT(lv_style_set_text_font),
    ESP_ELFSYM_EXPORT(lv_style_set_text_letter_space),
    ESP_ELFSYM_EXPORT(lv_style_set_text_line_space),
    ESP_ELFSYM_EXPORT(lv_style_set_text_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_text_outline_stroke_color),
    ESP_ELFSYM_EXPORT(lv_style_set_text_outline_stroke_opa),
    ESP_ELFSYM_EXPORT(lv_style_set_text_outline_stroke_width),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_height),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_pivot_x),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_pivot_y),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_rotation),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_scale_x),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_scale_y),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_skew_x),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_skew_y),
    ESP_ELFSYM_EXPORT(lv_style_set_transform_width),
    ESP_ELFSYM_EXPORT(lv_style_set_transition),
    ESP_ELFSYM_EXPORT(lv_style_set_translate_radial),
    ESP_ELFSYM_EXPORT(lv_style_set_translate_x),
    ESP_ELFSYM_EXPORT(lv_style_set_translate_y),
    ESP_ELFSYM_EXPORT(lv_style_set_width),
    ESP_ELFSYM_EXPORT(lv_style_set_x),
    ESP_ELFSYM_EXPORT(lv_style_set_y),
    ESP_ELFSYM_EXPORT(lv_style_transition_dsc_init),
    ESP_ELFSYM_EXPORT(lv_subject_add_observer),
    ESP_ELFSYM_EXPORT(lv_subject_add_observer_obj),
    ESP_ELFSYM_EXPORT(lv_subject_add_observer_with_target),
    ESP_ELFSYM_EXPORT(lv_subject_copy_string),
    ESP_ELFSYM_EXPORT(lv_subject_deinit),
    ESP_ELFSYM_EXPORT(lv_subject_get_color),
    ESP_ELFSYM_EXPORT(lv_subject_get_group_element),
    ESP_ELFSYM_EXPORT(lv_subject_get_int),
    ESP_ELFSYM_EXPORT(lv_subject_get_pointer),
    ESP_ELFSYM_EXPORT(lv_subject_get_previous_color),
    ESP_ELFSYM_EXPORT(lv_subject_get_previous_int),
    ESP_ELFSYM_EXPORT(lv_subject_get_previous_pointer),
    ESP_ELFSYM_EXPORT(lv_subject_get_previous_string),
    ESP_ELFSYM_EXPORT(lv_subject_get_string),
    ESP_ELFSYM_EXPORT(lv_subject_init_color),
    ESP_ELFSYM_EXPORT(lv_subject_init_group),
    ESP_ELFSYM_EXPORT(lv_subject_init_int),
    ESP_ELFSYM_EXPORT(lv_subject_init_pointer),
    ESP_ELFSYM_EXPORT(lv_subject_init_string),
    ESP_ELFSYM_EXPORT(lv_subject_notify),
    ESP_ELFSYM_EXPORT(lv_subject_set_color),
    ESP_ELFSYM_EXPORT(lv_subject_set_int),
    ESP_ELFSYM_EXPORT(lv_subject_set_max_value_int),
    ESP_ELFSYM_EXPORT(lv_subject_set_min_value_int),
    ESP_ELFSYM_EXPORT(lv_subject_set_pointer),
    ESP_ELFSYM_EXPORT(lv_subject_snprintf),
    ESP_ELFSYM_EXPORT(lv_switch_class),
    ESP_ELFSYM_EXPORT(lv_switch_create),
    ESP_ELFSYM_EXPORT(lv_switch_get_orientation),
    ESP_ELFSYM_EXPORT(lv_switch_set_orientation),
    ESP_ELFSYM_EXPORT(lv_sysmon_builtin_deinit),
    ESP_ELFSYM_EXPORT(lv_sysmon_builtin_init),
    ESP_ELFSYM_EXPORT(lv_sysmon_create),
    ESP_ELFSYM_EXPORT(lv_table_class),
    ESP_ELFSYM_EXPORT(lv_table_clear_cell_ctrl),
    ESP_ELFSYM_EXPORT(lv_table_create),
    ESP_ELFSYM_EXPORT(lv_table_get_cell_user_data),
    ESP_ELFSYM_EXPORT(lv_table_get_cell_value),
    ESP_ELFSYM_EXPORT(lv_table_get_column_count),
    ESP_ELFSYM_EXPORT(lv_table_get_column_width),
    ESP_ELFSYM_EXPORT(lv_table_get_row_count),
    ESP_ELFSYM_EXPORT(lv_table_get_selected_cell),
    ESP_ELFSYM_EXPORT(lv_table_has_cell_ctrl),
    ESP_ELFSYM_EXPORT(lv_table_set_cell_ctrl),
    ESP_ELFSYM_EXPORT(lv_table_set_cell_user_data),
    ESP_ELFSYM_EXPORT(lv_table_set_cell_value),
    ESP_ELFSYM_EXPORT(lv_table_set_cell_value_fmt),
    ESP_ELFSYM_EXPORT(lv_table_set_column_count),
    ESP_ELFSYM_EXPORT(lv_table_set_column_width),
    ESP_ELFSYM_EXPORT(lv_table_set_row_count),
    ESP_ELFSYM_EXPORT(lv_table_set_selected_cell),
    ESP_ELFSYM_EXPORT(lv_tabview_add_tab),
    ESP_ELFSYM_EXPORT(lv_tabview_class),
    ESP_ELFSYM_EXPORT(lv_tabview_create),
    ESP_ELFSYM_EXPORT(lv_tabview_get_content),
    ESP_ELFSYM_EXPORT(lv_tabview_get_tab_active),
    ESP_ELFSYM_EXPORT(lv_tabview_get_tab_bar),
    ESP_ELFSYM_EXPORT(lv_tabview_get_tab_bar_position),
    ESP_ELFSYM_EXPORT(lv_tabview_get_tab_button),
    ESP_ELFSYM_EXPORT(lv_tabview_get_tab_count),
    ESP_ELFSYM_EXPORT(lv_tabview_set_active),
    ESP_ELFSYM_EXPORT(lv_tabview_set_tab_bar_position),
    ESP_ELFSYM_EXPORT(lv_tabview_set_tab_bar_size),
    ESP_ELFSYM_EXPORT(lv_tabview_set_tab_text),
    ESP_ELFSYM_EXPORT(lv_text_attributes_init),
    ESP_ELFSYM_EXPORT(lv_text_cut),
    ESP_ELFSYM_EXPORT(lv_text_encoded_conv_wc),
    ESP_ELFSYM_EXPORT(lv_text_encoded_get_byte_id),
    ESP_ELFSYM_EXPORT(lv_text_encoded_get_char_id),
    ESP_ELFSYM_EXPORT(lv_text_encoded_letter_next_2),
    ESP_ELFSYM_EXPORT(lv_text_encoded_next),
    ESP_ELFSYM_EXPORT(lv_text_encoded_prev),
    ESP_ELFSYM_EXPORT(lv_text_encoded_size),
    ESP_ELFSYM_EXPORT(lv_text_get_encoded_length),
    ESP_ELFSYM_EXPORT(lv_text_get_next_line),
    ESP_ELFSYM_EXPORT(lv_text_get_size),
    ESP_ELFSYM_EXPORT(lv_text_get_size_attributes),
    ESP_ELFSYM_EXPORT(lv_text_get_width),
    ESP_ELFSYM_EXPORT(lv_text_ins),
    ESP_ELFSYM_EXPORT(lv_text_is_cmd),
    ESP_ELFSYM_EXPORT(lv_text_set_text_vfmt),
    ESP_ELFSYM_EXPORT(lv_text_unicode_to_encoded),
    ESP_ELFSYM_EXPORT(lv_textarea_add_char),
    ESP_ELFSYM_EXPORT(lv_textarea_add_text),
    ESP_ELFSYM_EXPORT(lv_textarea_class),
    ESP_ELFSYM_EXPORT(lv_textarea_clear_selection),
    ESP_ELFSYM_EXPORT(lv_textarea_create),
    ESP_ELFSYM_EXPORT(lv_textarea_cursor_down),
    ESP_ELFSYM_EXPORT(lv_textarea_cursor_left),
    ESP_ELFSYM_EXPORT(lv_textarea_cursor_right),
    ESP_ELFSYM_EXPORT(lv_textarea_cursor_up),
    ESP_ELFSYM_EXPORT(lv_textarea_delete_char),
    ESP_ELFSYM_EXPORT(lv_textarea_delete_char_forward),
    ESP_ELFSYM_EXPORT(lv_textarea_get_accepted_chars),
    ESP_ELFSYM_EXPORT(lv_textarea_get_current_char),
    ESP_ELFSYM_EXPORT(lv_textarea_get_cursor_click_pos),
    ESP_ELFSYM_EXPORT(lv_textarea_get_cursor_pos),
    ESP_ELFSYM_EXPORT(lv_textarea_get_label),
    ESP_ELFSYM_EXPORT(lv_textarea_get_max_length),
    ESP_ELFSYM_EXPORT(lv_textarea_get_one_line),
    ESP_ELFSYM_EXPORT(lv_textarea_get_password_bullet),
    ESP_ELFSYM_EXPORT(lv_textarea_get_password_mode),
    ESP_ELFSYM_EXPORT(lv_textarea_get_password_show_time),
    ESP_ELFSYM_EXPORT(lv_textarea_get_placeholder_text),
    ESP_ELFSYM_EXPORT(lv_textarea_get_text),
    ESP_ELFSYM_EXPORT(lv_textarea_get_text_selection),
    ESP_ELFSYM_EXPORT(lv_textarea_set_accepted_chars),
    ESP_ELFSYM_EXPORT(lv_textarea_set_accepted_chars_static),
    ESP_ELFSYM_EXPORT(lv_textarea_set_align),
    ESP_ELFSYM_EXPORT(lv_textarea_set_cursor_click_pos),
    ESP_ELFSYM_EXPORT(lv_textarea_set_cursor_pos),
    ESP_ELFSYM_EXPORT(lv_textarea_set_insert_replace),
    ESP_ELFSYM_EXPORT(lv_textarea_set_max_length),
    ESP_ELFSYM_EXPORT(lv_textarea_set_one_line),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_bullet),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_mode),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_show_time),
    ESP_ELFSYM_EXPORT(lv_textarea_set_placeholder_text),
    ESP_ELFSYM_EXPORT(lv_textarea_set_text),
    ESP_ELFSYM_EXPORT(lv_textarea_set_text_selection),
    ESP_ELFSYM_EXPORT(lv_textarea_text_is_selected),
    ESP_ELFSYM_EXPORT(lv_theme_apply),
    ESP_ELFSYM_EXPORT(lv_theme_copy),
    ESP_ELFSYM_EXPORT(lv_theme_create),
    ESP_ELFSYM_EXPORT(lv_theme_default_deinit),
    ESP_ELFSYM_EXPORT(lv_theme_default_get),
    ESP_ELFSYM_EXPORT(lv_theme_default_init),
    ESP_ELFSYM_EXPORT(lv_theme_default_is_inited),
    ESP_ELFSYM_EXPORT(lv_theme_delete),
    ESP_ELFSYM_EXPORT(lv_theme_get_color_primary),
    ESP_ELFSYM_EXPORT(lv_theme_get_color_secondary),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_large),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_normal),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_small),
    ESP_ELFSYM_EXPORT(lv_theme_get_from_obj),
    ESP_ELFSYM_EXPORT(lv_theme_set_apply_cb),
    ESP_ELFSYM_EXPORT(lv_theme_set_parent),
    ESP_ELFSYM_EXPORT(lv_theme_simple_deinit),
    ESP_ELFSYM_EXPORT(lv_theme_simple_get),
    ESP_ELFSYM_EXPORT(lv_theme_simple_init),
    ESP_ELFSYM_EXPORT(lv_theme_simple_is_inited),
    ESP_ELFSYM_EXPORT(lv_tick_diff),
    ESP_ELFSYM_EXPORT(lv_tick_elaps),
    ESP_ELFSYM_EXPORT(lv_tick_get),
    ESP_ELFSYM_EXPORT(lv_tick_get_cb),
    ESP_ELFSYM_EXPORT(lv_tick_inc),
    ESP_ELFSYM_EXPORT(lv_tick_set_cb),
    ESP_ELFSYM_EXPORT(lv_tileview_add_tile),
    ESP_ELFSYM_EXPORT(lv_tileview_class),
    ESP_ELFSYM_EXPORT(lv_tileview_create),
    ESP_ELFSYM_EXPORT(lv_tileview_get_tile_active),
    ESP_ELFSYM_EXPORT(lv_tileview_set_tile),
    ESP_ELFSYM_EXPORT(lv_tileview_set_tile_by_index),
    ESP_ELFSYM_EXPORT(lv_tileview_tile_class),
    ESP_ELFSYM_EXPORT(lv_timer_core_deinit),
    ESP_ELFSYM_EXPORT(lv_timer_core_init),
    ESP_ELFSYM_EXPORT(lv_timer_create),
    ESP_ELFSYM_EXPORT(lv_timer_create_basic),
    ESP_ELFSYM_EXPORT(lv_timer_delete),
    ESP_ELFSYM_EXPORT(lv_timer_enable),
    ESP_ELFSYM_EXPORT(lv_timer_get_idle),
    ESP_ELFSYM_EXPORT(lv_timer_get_next),
    ESP_ELFSYM_EXPORT(lv_timer_get_paused),
    ESP_ELFSYM_EXPORT(lv_timer_get_time_until_next),
    ESP_ELFSYM_EXPORT(lv_timer_get_user_data),
    ESP_ELFSYM_EXPORT(lv_timer_handler),
    ESP_ELFSYM_EXPORT(lv_timer_handler_run_in_period),
    ESP_ELFSYM_EXPORT(lv_timer_handler_set_resume_cb),
    ESP_ELFSYM_EXPORT(lv_timer_pause),
    ESP_ELFSYM_EXPORT(lv_timer_periodic_handler),
    ESP_ELFSYM_EXPORT(lv_timer_ready),
    ESP_ELFSYM_EXPORT(lv_timer_reset),
    ESP_ELFSYM_EXPORT(lv_timer_resume),
    ESP_ELFSYM_EXPORT(lv_timer_set_auto_delete),
    ESP_ELFSYM_EXPORT(lv_timer_set_cb),
    ESP_ELFSYM_EXPORT(lv_timer_set_period),
    ESP_ELFSYM_EXPORT(lv_timer_set_repeat_count),
    ESP_ELFSYM_EXPORT(lv_timer_set_user_data),
    ESP_ELFSYM_EXPORT(lv_tjpgd_deinit),
    ESP_ELFSYM_EXPORT(lv_tjpgd_init),
    ESP_ELFSYM_EXPORT(lv_tree_node_class),
    ESP_ELFSYM_EXPORT(lv_tree_node_create),
    ESP_ELFSYM_EXPORT(lv_tree_node_delete),
    ESP_ELFSYM_EXPORT(lv_tree_walk),
    ESP_ELFSYM_EXPORT(lv_trigo_cos),
    ESP_ELFSYM_EXPORT(lv_trigo_sin),
    ESP_ELFSYM_EXPORT(lv_unlock),
    ESP_ELFSYM_EXPORT(lv_utils_bsearch),
    ESP_ELFSYM_EXPORT(lv_vsnprintf),
    ESP_ELFSYM_EXPORT(lv_win_add_button),
    ESP_ELFSYM_EXPORT(lv_win_add_title),
    ESP_ELFSYM_EXPORT(lv_win_class),
    ESP_ELFSYM_EXPORT(lv_win_create),
    ESP_ELFSYM_EXPORT(lv_win_get_content),
    ESP_ELFSYM_EXPORT(lv_win_get_header),
    ESP_ELFSYM_EXPORT(lv_zalloc),
    ESP_ELFSYM_EXPORT(lvgl_port_add_disp),
    ESP_ELFSYM_EXPORT(lvgl_port_add_disp_dsi),
    ESP_ELFSYM_EXPORT(lvgl_port_add_disp_rgb),
    ESP_ELFSYM_EXPORT(lvgl_port_add_touch),
    ESP_ELFSYM_EXPORT(lvgl_port_deinit),
    ESP_ELFSYM_EXPORT(lvgl_port_flush_ready),
    ESP_ELFSYM_EXPORT(lvgl_port_init),
    ESP_ELFSYM_EXPORT(lvgl_port_lock),
    ESP_ELFSYM_EXPORT(lvgl_port_remove_disp),
    ESP_ELFSYM_EXPORT(lvgl_port_remove_touch),
    ESP_ELFSYM_EXPORT(lvgl_port_resume),
    ESP_ELFSYM_EXPORT(lvgl_port_rotate_area),
    ESP_ELFSYM_EXPORT(lvgl_port_stop),
    ESP_ELFSYM_EXPORT(lvgl_port_task_notify),
    ESP_ELFSYM_EXPORT(lvgl_port_task_wake),
    ESP_ELFSYM_EXPORT(lvgl_port_unlock),
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(memcmp),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(memmove),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(mkdir),
    ESP_ELFSYM_EXPORT(mktime),
    ESP_ELFSYM_EXPORT(opendir),
    ESP_ELFSYM_EXPORT(powf),
    ESP_ELFSYM_EXPORT(putchar),
    ESP_ELFSYM_EXPORT(puts),
    ESP_ELFSYM_EXPORT(qrcodegen_calcSegmentBufferSize),
    ESP_ELFSYM_EXPORT(qrcodegen_encodeBinary),
    ESP_ELFSYM_EXPORT(qrcodegen_encodeSegments),
    ESP_ELFSYM_EXPORT(qrcodegen_encodeSegmentsAdvanced),
    ESP_ELFSYM_EXPORT(qrcodegen_encodeText),
    ESP_ELFSYM_EXPORT(qrcodegen_getMinFitVersion),
    ESP_ELFSYM_EXPORT(qrcodegen_getModule),
    ESP_ELFSYM_EXPORT(qrcodegen_getSize),
    ESP_ELFSYM_EXPORT(qrcodegen_isAlphanumeric),
    ESP_ELFSYM_EXPORT(qrcodegen_isNumeric),
    ESP_ELFSYM_EXPORT(qrcodegen_makeAlphanumeric),
    ESP_ELFSYM_EXPORT(qrcodegen_makeBytes),
    ESP_ELFSYM_EXPORT(qrcodegen_makeEci),
    ESP_ELFSYM_EXPORT(qrcodegen_makeNumeric),
    ESP_ELFSYM_EXPORT(qrcodegen_version2size),
    ESP_ELFSYM_EXPORT(qsort),
    ESP_ELFSYM_EXPORT(readdir),
    ESP_ELFSYM_EXPORT(realloc),
    ESP_ELFSYM_EXPORT(remove),
    ESP_ELFSYM_EXPORT(rename),
    ESP_ELFSYM_EXPORT(rewind),
    ESP_ELFSYM_EXPORT(roundf),
    ESP_ELFSYM_EXPORT(sinf),
    ESP_ELFSYM_EXPORT(snprintf),
    ESP_ELFSYM_EXPORT(sprintf),
    ESP_ELFSYM_EXPORT(sqrtf),
    ESP_ELFSYM_EXPORT(sscanf),
    ESP_ELFSYM_EXPORT(stat),
    ESP_ELFSYM_EXPORT(strcasecmp),
    ESP_ELFSYM_EXPORT(strcat),
    ESP_ELFSYM_EXPORT(strchr),
    ESP_ELFSYM_EXPORT(strcmp),
    ESP_ELFSYM_EXPORT(strcpy),
    ESP_ELFSYM_EXPORT(strlen),
    ESP_ELFSYM_EXPORT(strncmp),
    ESP_ELFSYM_EXPORT(strncpy),
    ESP_ELFSYM_EXPORT(strnlen),
    ESP_ELFSYM_EXPORT(strrchr),
    ESP_ELFSYM_EXPORT(strstr),
    ESP_ELFSYM_EXPORT(strtod),
    ESP_ELFSYM_EXPORT(strtof),
    ESP_ELFSYM_EXPORT(strtol),
    ESP_ELFSYM_EXPORT(strtoul),
    ESP_ELFSYM_EXPORT(tanf),
    ESP_ELFSYM_EXPORT(time),
    ESP_ELFSYM_EXPORT(unlink),
    ESP_ELFSYM_EXPORT(vsnprintf),
    ESP_ELFSYM_END,
};
