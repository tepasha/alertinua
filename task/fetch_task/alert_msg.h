#pragma once
#include <stdbool.h>
#include "alerts_parser.h"

/* Повідомлення fetch_task -> render_task через alert_queue. */
typedef struct {
    bool fetch_ok;
    alerts_result_t parsed;
} alert_msg_t;
