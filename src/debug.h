#pragma once

#include <stdbool.h>

typedef enum
{
    DEBUG_LEVEL_OFF,
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_WARNING,
    DEBUG_LEVEL_ERROR
} debug_level_t;

void debug_set_level(debug_level_t level);

debug_level_t debug_get_level(void);

bool debug_is_enabled(debug_level_t level);

void debug_info(char const * fmt, ...);

void debug_warning(char const * fmt, ...);

void debug_error_int(char const * func, int line, char const * fmt, ...);
#define debug_error(fmt, ...) debug_error_int(__func__, __LINE__, (fmt), ##__VA_ARGS__)
