// Unit tests for components/device_proto/src/channel_registry.c.
// The .c file is included directly so each test can reset its static state.
#include "tinytest.h"
#include "../../components/device_proto/src/channel_registry.c"

static esp_err_t dummy_get(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    (void)enc;
    return ESP_OK;
}

static void reset(void)
{
    s_count = 0;
    memset(s_channels, 0, sizeof(s_channels));
}

static esp_err_t reg(const char *name, const char *access)
{
    return channel_registry_register(name, "uint", access, NULL, dummy_get, NULL);
}

TEST(valid_access_strings_accepted)
{
    reset();
    CHECK_EQ(reg("a", "RO"), ESP_OK);
    CHECK_EQ(reg("b", "WO"), ESP_OK);
    CHECK_EQ(reg("c", "RW"), ESP_OK);
    CHECK_EQ(channel_registry_count(), 3u);
}

TEST(invalid_access_strings_rejected)
{
    reset();
    const char *bad[] = { "ro", "R", "", "RWX", "rw", "XX" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(reg("n", bad[i]), ESP_ERR_INVALID_ARG);
    }
    CHECK_EQ(channel_registry_count(), 0u);
}

TEST(registry_full_at_33rd)
{
    reset();
    char name[16];
    for (int i = 0; i < CHANNEL_REGISTRY_MAX; i++) {
        snprintf(name, sizeof(name), "ch%d", i);
        CHECK_EQ(reg(name, "RO"), ESP_OK);
    }
    CHECK_EQ(reg("overflow", "RO"), ESP_ERR_NO_MEM);
    CHECK_EQ(channel_registry_count(), (size_t)CHANNEL_REGISTRY_MAX);
}

TEST(name_of_39_chars_kept_whole)
{
    reset();
    char n[PROTO_NAME_MAXLEN];
    memset(n, 'a', PROTO_NAME_MAXLEN - 1);
    n[PROTO_NAME_MAXLEN - 1] = '\0';
    CHECK_EQ(reg(n, "RO"), ESP_OK);
    CHECK(channel_registry_find(n) != NULL);
}

TEST(name_too_long_is_rejected_not_truncated)
{
    reset();
    char n[PROTO_NAME_MAXLEN + 10];
    memset(n, 'a', sizeof(n) - 1);
    n[sizeof(n) - 1] = '\0';
    CHECK_EQ(reg(n, "RO"), ESP_ERR_INVALID_ARG);
    CHECK_EQ(channel_registry_count(), 0u);
}

TEST(type_name_too_long_is_rejected)
{
    reset();
    CHECK_EQ(channel_registry_register("x", "this_is_far_too_long", "RO", NULL, dummy_get, NULL), ESP_ERR_INVALID_ARG);
}

TEST(duplicate_name_is_rejected)
{
    reset();
    CHECK_EQ(reg("tof0.dist", "RO"), ESP_OK);
    CHECK_EQ(reg("tof0.dist", "RO"), ESP_ERR_INVALID_STATE);
    CHECK_EQ(channel_registry_count(), 1u);
}

TEST(null_arguments_are_rejected)
{
    reset();
    CHECK_EQ(channel_registry_register(NULL, "uint", "RO", NULL, dummy_get, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(channel_registry_register("x", NULL, "RO", NULL, dummy_get, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(channel_registry_register("x", "uint", NULL, NULL, dummy_get, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(channel_registry_count(), 0u);
}

TEST(find_exact_and_not_prefix)
{
    reset();
    reg("tof0.dist", "RO");
    reg("tof0", "RO");
    const channel_entry_t *e = channel_registry_find("tof0");
    CHECK(e != NULL);
    CHECK_STREQ(e->name, "tof0");
    CHECK(channel_registry_find("tof") == NULL);
    CHECK(channel_registry_find("tof0.dis") == NULL);
    CHECK(channel_registry_find("") == NULL);
    CHECK(channel_registry_find("tof0.dist ") == NULL);
}

TEST(at_returns_registration_order_and_fields)
{
    reset();
    int ctx = 7;
    channel_registry_register("first", "bool", "RW", &ctx, dummy_get, NULL);
    reg("second", "RO");
    const channel_entry_t *e = channel_registry_at(0);
    CHECK_STREQ(e->name, "first");
    CHECK_STREQ(e->type_name, "bool");
    CHECK_STREQ(e->access, "RW");
    CHECK(e->ctx == &ctx);
    CHECK(e->get == dummy_get);
    CHECK(e->set == NULL);
    CHECK_STREQ(channel_registry_at(1)->name, "second");
}

int main(void)
{
    RUN_TEST(valid_access_strings_accepted);
    RUN_TEST(invalid_access_strings_rejected);
    RUN_TEST(registry_full_at_33rd);
    RUN_TEST(name_of_39_chars_kept_whole);
    RUN_TEST(name_too_long_is_rejected_not_truncated);
    RUN_TEST(type_name_too_long_is_rejected);
    RUN_TEST(duplicate_name_is_rejected);
    RUN_TEST(null_arguments_are_rejected);
    RUN_TEST(find_exact_and_not_prefix);
    RUN_TEST(at_returns_registration_order_and_fields);
    return tinytest_summary();
}
