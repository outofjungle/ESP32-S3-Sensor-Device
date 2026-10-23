#include "input_validate.h"

bool iv_u32_in_range(uint32_t value, uint32_t min, uint32_t max)
{
    return value >= min && value <= max;
}

bool iv_i32_in_range(int32_t value, int32_t min, int32_t max)
{
    return value >= min && value <= max;
}

bool iv_bool_in_range(int32_t value)
{
    return value == 0 || value == 1;
}

bool iv_enum_in_range(int32_t value, size_t enum_count)
{
    return value >= 0 && (size_t)value < enum_count;
}

void iv_force_terminate(char *buf, size_t bufsz)
{
    if (buf == NULL || bufsz == 0) {
        return;
    }
    buf[bufsz - 1] = '\0';
}
