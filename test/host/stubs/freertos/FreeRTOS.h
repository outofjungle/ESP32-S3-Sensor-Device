#pragma once

// Host-test stand-ins for FreeRTOS. A host test runs on one thread, so a
// critical section has nothing to protect.
typedef struct {
    int unused;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
