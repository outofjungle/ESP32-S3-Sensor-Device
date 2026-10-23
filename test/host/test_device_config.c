// Unit tests for components/device_proto/src/device_config.c (the whole
// file, built against stub NVS/FreeRTOS headers) and device_config_logic.c.
#include "tinytest.h"
#include "../../components/device_proto/src/channel_registry.c"
#include "../../components/device_proto/src/device_config.c"
#include <stdint.h>

static esp_err_t dummy_get(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    (void)enc;
    return ESP_OK;
}

static void reset_all(void)
{
    s_count = 0; // channel_registry.c is included above, so its state is reachable
    g_nvs_has_blob = 0;
    g_nvs_blob_len = 0;
    g_nvs_fail_open = 0;
    g_nvs_fail_commit = 0;
    s_hook_count = 0;
    s_led_mode_hook = NULL;
    dc_set_defaults(&s_cfg);
    for (int i = 0; i < 10; i++) {
        char n[8];
        snprintf(n, sizeof(n), "c%d", i);
        channel_registry_register(n, "uint", "RO", NULL, dummy_get, NULL);
    }
}

/* ---- sanitize (device_config_logic.c) ------------------------------- */

TEST(defaults)
{
    config_state_t c;
    dc_set_defaults(&c);
    CHECK_EQ(c.version, DC_CFG_VERSION);
    CHECK_EQ(c.period_us, 100000u);
    CHECK_STREQ(c.mode, "PERIODIC");
    CHECK_EQ(c.batch, 1);
    CHECK_STREQ(c.led_mode, "STATUS");
    CHECK_EQ(c.sub_count, 0u);
    CHECK(!c.streaming);
}

TEST(sanitize_leaves_a_good_config_alone)
{
    config_state_t c;
    dc_set_defaults(&c);
    c.period_us = 5000;
    c.batch = 4;
    strcpy(c.mode, "BOTH");
    strcpy(c.subs[0], "c1");
    c.sub_count = 1;
    CHECK_EQ(dc_sanitize_loaded(&c), 0);
    CHECK_EQ(c.period_us, 5000u);
    CHECK_EQ(c.batch, 4);
    CHECK_STREQ(c.mode, "BOTH");
    CHECK_STREQ(c.subs[0], "c1");
}

TEST(sanitize_clamps_sub_count)
{
    config_state_t c;
    dc_set_defaults(&c);
    c.sub_count = 9;
    CHECK_EQ(dc_sanitize_loaded(&c), 1);
    CHECK_EQ(c.sub_count, 8u);
    c.sub_count = UINT32_MAX;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.sub_count, 8u);
    c.sub_count = 8;
    CHECK_EQ(dc_sanitize_loaded(&c), 0);
}

TEST(sanitize_period_bounds)
{
    config_state_t c;
    dc_set_defaults(&c);
    c.period_us = 999;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, 100000u);
    c.period_us = 1000;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, 1000u);
    c.period_us = DC_PERIOD_US_MAX;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, DC_PERIOD_US_MAX);
    c.period_us = DC_PERIOD_US_MAX + 1;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, 100000u);
    c.period_us = UINT32_MAX;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, 100000u);
    c.period_us = 0;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.period_us, 100000u);
}

TEST(sanitize_batch_bounds)
{
    config_state_t c;
    dc_set_defaults(&c);
    c.batch = 0;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.batch, 1);
    c.batch = 33;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.batch, 1);
    c.batch = 32;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.batch, 32);
    c.batch = 1;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.batch, 1);
    c.batch = 65535;
    dc_sanitize_loaded(&c);
    CHECK_EQ(c.batch, 1);
}

TEST(sanitize_terminates_and_validates_strings)
{
    config_state_t c;
    dc_set_defaults(&c);
    memset(c.mode, 'A', sizeof(c.mode));
    memset(c.led_mode, 'B', sizeof(c.led_mode));
    c.sub_count = 1;
    memset(c.subs[0], 'x', DC_CHANNEL_NAME_MAXLEN);
    dc_sanitize_loaded(&c);
    CHECK_STREQ(c.mode, "PERIODIC");
    CHECK_STREQ(c.led_mode, "STATUS");
    CHECK_EQ(strlen(c.subs[0]), (size_t)DC_CHANNEL_NAME_MAXLEN - 1);
    strcpy(c.mode, "periodic");
    dc_sanitize_loaded(&c);
    CHECK_STREQ(c.mode, "PERIODIC");
    strcpy(c.led_mode, "OFF");
    dc_sanitize_loaded(&c);
    CHECK_STREQ(c.led_mode, "STATUS");
}

TEST(sanitize_clears_streaming_and_unused_names)
{
    config_state_t c;
    dc_set_defaults(&c);
    c.streaming = true;
    c.sub_count = 1;
    memset(c.subs[5], 'z', DC_CHANNEL_NAME_MAXLEN);
    dc_sanitize_loaded(&c);
    CHECK(!c.streaming);
    CHECK_EQ(c.subs[5][0], '\0');
}

TEST(mode_validators)
{
    CHECK(dc_mode_valid("PERIODIC") && dc_mode_valid("ONCHANGE") && dc_mode_valid("BOTH"));
    CHECK(!dc_mode_valid("") && !dc_mode_valid("PERIODICX") && !dc_mode_valid("both"));
    CHECK(dc_led_mode_valid("STATUS") && dc_led_mode_valid("MANUAL"));
    CHECK(!dc_led_mode_valid("") && !dc_led_mode_valid("status"));
}

/* ---- setters ---------------------------------------------------------- */

TEST(period_setter_bounds)
{
    reset_all();
    CHECK_EQ(device_config_set_period_us(999), ERR_RANGE);
    CHECK_EQ(device_config_set_period_us(1000), 0);
    CHECK_EQ(device_config_get_period_us(), 1000u);
    CHECK_EQ(device_config_set_period_us(DC_PERIOD_US_MAX), 0);
    CHECK_EQ(device_config_set_period_us(DC_PERIOD_US_MAX + 1), ERR_RANGE);
    CHECK_EQ(device_config_set_period_us(UINT32_MAX), ERR_RANGE);
    CHECK_EQ(device_config_get_period_us(), DC_PERIOD_US_MAX);
}

TEST(batch_setter_bounds)
{
    reset_all();
    CHECK_EQ(device_config_set_batch(0), ERR_RANGE);
    CHECK_EQ(device_config_set_batch(33), ERR_RANGE);
    CHECK_EQ(device_config_set_batch(1), 0);
    CHECK_EQ(device_config_set_batch(32), 0);
    CHECK_EQ(device_config_get_batch(), 32);
}

TEST(mode_setters)
{
    reset_all();
    CHECK_EQ(device_config_set_mode("BOTH"), 0);
    CHECK_STREQ(device_config_get_mode(), "BOTH");
    CHECK_EQ(device_config_set_mode("PERIODICX"), ERR_RANGE);
    CHECK_EQ(device_config_set_mode(""), ERR_RANGE);
    CHECK_STREQ(device_config_get_mode(), "BOTH");
    CHECK_STREQ(device_config_get_encoding(), "CBOR");
}

static char g_hook_seen[16];
static int g_hook_calls;
static void led_hook(const char *m)
{
    strncpy(g_hook_seen, m, sizeof(g_hook_seen) - 1);
    g_hook_calls++;
}

TEST(led_mode_setter_runs_the_hook_only_on_success)
{
    reset_all();
    g_hook_calls = 0;
    g_hook_seen[0] = '\0';
    device_config_register_led_mode_hook(led_hook);
    CHECK_EQ(device_config_set_led_mode("BAD"), ERR_RANGE);
    CHECK_EQ(g_hook_calls, 0);
    CHECK_EQ(device_config_set_led_mode("MANUAL"), 0);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_STREQ(g_hook_seen, "MANUAL");
    CHECK_STREQ(device_config_get_led_mode(), "MANUAL");
    device_config_note_led_mode_changed_externally("STATUS"); // no hook call
    CHECK_EQ(g_hook_calls, 1);
    CHECK_STREQ(device_config_get_led_mode(), "STATUS");
}

/* ---- subscriptions ------------------------------------------------------ */

TEST(subscribe_unknown_channel_is_nosuch)
{
    reset_all();
    CHECK_EQ(device_config_subscribe("nope"), ERR_NOCH);
    CHECK_EQ(device_config_sub_count(), 0u);
}

TEST(subscribe_duplicate_adds_nothing)
{
    reset_all();
    CHECK_EQ(device_config_subscribe("c1"), 0);
    CHECK_EQ(device_config_subscribe("c1"), 0);
    CHECK_EQ(device_config_sub_count(), 1u);
    CHECK_EQ(device_config_sub_count(), 1u);
}

TEST(subscribe_limit_is_eight)
{
    reset_all();
    char n[8];
    for (int i = 0; i < 8; i++) {
        snprintf(n, sizeof(n), "c%d", i);
        CHECK_EQ(device_config_subscribe(n), 0);
    }
    CHECK_EQ(device_config_subscribe("c8"), ERR_RANGE);
    CHECK_EQ(device_config_sub_count(), 8u);
    CHECK_EQ(device_config_subscribe("c3"), 0); // already there: still fine when full
}

TEST(unsubscribe_first_middle_last_keeps_order)
{
    reset_all();
    for (int i = 0; i < 5; i++) {
        char n[8];
        snprintf(n, sizeof(n), "c%d", i);
        device_config_subscribe(n);
    }
    CHECK_EQ(device_config_unsubscribe("c0"), 0); // first
    CHECK_STREQ(device_config_sub_at(0), "c1");
    CHECK_EQ(device_config_unsubscribe("c3"), 0); // middle
    CHECK_EQ(device_config_unsubscribe("c4"), 0); // last
    CHECK_EQ(device_config_sub_count(), 2u);
    CHECK_STREQ(device_config_sub_at(0), "c1");
    CHECK_STREQ(device_config_sub_at(1), "c2");
    CHECK_EQ(device_config_unsubscribe("c9"), ERR_STATE);
}

TEST(unsubscribe_from_empty_list)
{
    reset_all();
    CHECK_EQ(device_config_unsubscribe("c1"), ERR_STATE);
}

TEST(sub_at_out_of_range_is_empty_not_garbage)
{
    reset_all();
    device_config_subscribe("c1");
    CHECK_STREQ(device_config_sub_at(0), "c1");
    CHECK_STREQ(device_config_sub_at(1), "");
    CHECK_STREQ(device_config_sub_at(100000), "");
}

/* ---- streaming state ---------------------------------------------------- */

TEST(streaming_state_rules)
{
    reset_all();
    CHECK_EQ(device_config_start_streaming(), ERR_STATE); // no subscription
    CHECK_EQ(device_config_stop_streaming(), ERR_STATE);  // not streaming
    device_config_subscribe("c1");
    CHECK_EQ(device_config_start_streaming(), 0);
    CHECK(device_config_is_streaming());
    CHECK_EQ(device_config_start_streaming(), ERR_STATE); // already streaming
    CHECK_EQ(device_config_stop_streaming(), 0);
    CHECK(!device_config_is_streaming());
}

/* ---- persistence --------------------------------------------------------- */

static int g_save_calls, g_load_calls;
static esp_err_t g_hook_save_rc, g_hook_load_rc;
static esp_err_t hook_save(void)
{
    g_save_calls++;
    return g_hook_save_rc;
}
static esp_err_t hook_load(void)
{
    g_load_calls++;
    return g_hook_load_rc;
}

TEST(save_then_load_round_trips)
{
    reset_all();
    device_config_set_period_us(20000);
    device_config_set_batch(8);
    device_config_set_mode("ONCHANGE");
    device_config_subscribe("c2");
    device_config_subscribe("c5");
    CHECK_EQ(device_config_save(), ESP_OK);
    dc_set_defaults(&s_cfg);
    CHECK_EQ(device_config_load(), ESP_OK);
    CHECK_EQ(device_config_get_period_us(), 20000u);
    CHECK_EQ(device_config_get_batch(), 8);
    CHECK_STREQ(device_config_get_mode(), "ONCHANGE");
    CHECK_EQ(device_config_sub_count(), 2u);
    CHECK_STREQ(device_config_sub_at(1), "c5");
}

TEST(load_with_nothing_saved_reports_not_found_and_keeps_defaults)
{
    reset_all();
    CHECK_EQ(device_config_load(), ESP_ERR_NVS_NOT_FOUND);
    CHECK_EQ(device_config_get_period_us(), 100000u);
}

TEST(streaming_is_never_restored)
{
    reset_all();
    device_config_subscribe("c1");
    device_config_start_streaming();
    device_config_save();
    dc_set_defaults(&s_cfg);
    device_config_load();
    CHECK(!device_config_is_streaming());
    CHECK_EQ(device_config_sub_count(), 1u);
}

TEST(load_rejects_wrong_version_and_wrong_size)
{
    reset_all();
    config_state_t bad;
    dc_set_defaults(&bad);
    bad.version = 99;
    bad.period_us = 5000;
    memcpy(g_nvs_blob, &bad, sizeof(bad));
    g_nvs_blob_len = sizeof(bad);
    g_nvs_has_blob = 1;
    device_config_load();
    CHECK_EQ(device_config_get_period_us(), 100000u);

    bad.version = DC_CFG_VERSION;
    g_nvs_blob_len = sizeof(bad) - 1;
    device_config_load();
    CHECK_EQ(device_config_get_period_us(), 100000u);
}

TEST(load_repairs_a_corrupt_blob)
{
    reset_all();
    config_state_t bad;
    dc_set_defaults(&bad);
    bad.period_us = 5;
    bad.batch = 200;
    bad.sub_count = 0xFFFFFFFFu;
    memset(bad.mode, 'Q', sizeof(bad.mode));
    memcpy(g_nvs_blob, &bad, sizeof(bad));
    g_nvs_blob_len = sizeof(bad);
    g_nvs_has_blob = 1;
    CHECK_EQ(device_config_load(), ESP_OK);
    CHECK_EQ(device_config_get_period_us(), 100000u);
    CHECK_EQ(device_config_get_batch(), 1);
    CHECK_STREQ(device_config_get_mode(), "PERIODIC");
    CHECK(device_config_sub_count() <= 8u);
}

TEST(init_starts_from_defaults_then_loads_what_was_saved)
{
    reset_all();
    CHECK_EQ(device_config_init(), ESP_OK); // nothing saved: not an error
    CHECK_EQ(device_config_get_period_us(), 100000u);
    device_config_set_period_us(40000);
    device_config_save();
    s_cfg.period_us = 1;
    CHECK_EQ(device_config_init(), ESP_OK);
    CHECK_EQ(device_config_get_period_us(), 40000u);
}

TEST(init_survives_a_storage_failure)
{
    reset_all();
    g_nvs_fail_open = 1;
    CHECK_EQ(device_config_init(), ESP_OK); // logs and keeps defaults
    CHECK_EQ(device_config_get_period_us(), 100000u);
}

TEST(hooks_run_and_limit_is_four)
{
    reset_all();
    g_save_calls = g_load_calls = 0;
    g_hook_save_rc = g_hook_load_rc = ESP_OK;
    for (int i = 0; i < 4; i++)
        CHECK_EQ(device_config_register_persist_hook(hook_save, hook_load), ESP_OK);
    CHECK_EQ(device_config_register_persist_hook(hook_save, hook_load), ESP_ERR_NO_MEM);
    device_config_save();
    CHECK_EQ(g_save_calls, 4);
    device_config_load();
    CHECK_EQ(g_load_calls, 4);
}

TEST(save_reports_the_first_error_and_still_runs_hooks)
{
    reset_all();
    g_save_calls = 0;
    g_hook_save_rc = ESP_FAIL;
    g_hook_load_rc = ESP_OK;
    device_config_register_persist_hook(hook_save, hook_load);
    CHECK_EQ(device_config_save(), ESP_FAIL); // hook error surfaces
    CHECK_EQ(g_save_calls, 1);
    g_hook_save_rc = ESP_OK;
    g_nvs_fail_commit = 1;
    CHECK_EQ(device_config_save(), ESP_FAIL); // NVS error surfaces
    CHECK_EQ(g_save_calls, 2);                // hooks still ran
    g_nvs_fail_commit = 0;
    g_nvs_fail_open = 1;
    CHECK_EQ(device_config_save(), ESP_FAIL);
}

TEST(load_ignores_a_hook_that_has_nothing_saved)
{
    reset_all();
    g_hook_save_rc = ESP_OK;
    g_hook_load_rc = ESP_ERR_NVS_NOT_FOUND;
    device_config_register_persist_hook(hook_save, hook_load);
    device_config_save();
    CHECK_EQ(device_config_load(), ESP_OK);
    g_hook_load_rc = ESP_FAIL;
    CHECK_EQ(device_config_load(), ESP_FAIL);
}

int main(void)
{
    RUN_TEST(defaults);
    RUN_TEST(sanitize_leaves_a_good_config_alone);
    RUN_TEST(sanitize_clamps_sub_count);
    RUN_TEST(sanitize_period_bounds);
    RUN_TEST(sanitize_batch_bounds);
    RUN_TEST(sanitize_terminates_and_validates_strings);
    RUN_TEST(sanitize_clears_streaming_and_unused_names);
    RUN_TEST(mode_validators);
    RUN_TEST(period_setter_bounds);
    RUN_TEST(batch_setter_bounds);
    RUN_TEST(mode_setters);
    RUN_TEST(led_mode_setter_runs_the_hook_only_on_success);
    RUN_TEST(subscribe_unknown_channel_is_nosuch);
    RUN_TEST(subscribe_duplicate_adds_nothing);
    RUN_TEST(subscribe_limit_is_eight);
    RUN_TEST(unsubscribe_first_middle_last_keeps_order);
    RUN_TEST(unsubscribe_from_empty_list);
    RUN_TEST(sub_at_out_of_range_is_empty_not_garbage);
    RUN_TEST(streaming_state_rules);
    RUN_TEST(save_then_load_round_trips);
    RUN_TEST(load_with_nothing_saved_reports_not_found_and_keeps_defaults);
    RUN_TEST(streaming_is_never_restored);
    RUN_TEST(load_rejects_wrong_version_and_wrong_size);
    RUN_TEST(load_repairs_a_corrupt_blob);
    RUN_TEST(init_starts_from_defaults_then_loads_what_was_saved);
    RUN_TEST(init_survives_a_storage_failure);
    RUN_TEST(hooks_run_and_limit_is_four);
    RUN_TEST(save_reports_the_first_error_and_still_runs_hooks);
    RUN_TEST(load_ignores_a_hook_that_has_nothing_saved);
    return tinytest_summary();
}
