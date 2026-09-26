#pragma once

/*
 * monitor_task: раз на 30с друкує розподіл часу CPU між задачами та
 * метрики останнього циклу HTTP+JSON.
 */
void monitor_task_start(void);
