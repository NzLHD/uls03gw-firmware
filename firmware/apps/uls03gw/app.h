/* app.h - glue between the setup web server and the application (app.c). */
#ifndef ULS_APP_H
#define ULS_APP_H

#include <stddef.h>

/* JSON bodies for the HTTP API (spec §6). Each returns the length written, or -1. */
int  app_status_json(char *buf, size_t cap);
int  app_read_json(char *buf, size_t cap);
int  app_config_post(const char *body, size_t len, char *out, size_t cap);
int  app_test_post(const char *body, size_t len, char *out, size_t cap);
void app_factory_reset(void);       /* erase settings (caller reboots) */

#endif
