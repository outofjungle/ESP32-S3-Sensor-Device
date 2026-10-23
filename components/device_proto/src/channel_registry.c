#include "channel_registry.h"
#include <string.h>

static channel_entry_t s_channels[CHANNEL_REGISTRY_MAX];
static size_t s_count = 0;

esp_err_t channel_registry_register(const char *name, const char *type_name, const char *access, void *ctx,
                                    channel_get_fn get, channel_set_fn set)
{
    if (name == NULL || type_name == NULL || access == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Reject, rather than silently truncate: two long names that differ only
    // after the cut would otherwise collide and the second would be unreachable.
    if (strlen(name) >= sizeof(s_channels[0].name) || strlen(type_name) >= sizeof(s_channels[0].type_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(access, "RO") != 0 && strcmp(access, "WO") != 0 && strcmp(access, "RW") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_count >= CHANNEL_REGISTRY_MAX) {
        return ESP_ERR_NO_MEM;
    }
    if (channel_registry_find(name) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    channel_entry_t *e = &s_channels[s_count];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    strncpy(e->type_name, type_name, sizeof(e->type_name) - 1);
    e->type_name[sizeof(e->type_name) - 1] = '\0';
    strncpy(e->access, access, sizeof(e->access) - 1);
    e->access[sizeof(e->access) - 1] = '\0';
    e->ctx = ctx;
    e->get = get;
    e->set = set;
    s_count++;
    return ESP_OK;
}

size_t channel_registry_count(void)
{
    return s_count;
}

const channel_entry_t *channel_registry_at(size_t index)
{
    return &s_channels[index];
}

const channel_entry_t *channel_registry_find(const char *name)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_channels[i].name, name) == 0) {
            return &s_channels[i];
        }
    }
    return NULL;
}
