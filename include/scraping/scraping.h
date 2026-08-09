#ifndef SCRAPING_H
#define SCRAPING_H

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);
char *api_fetch_bearer_auth(const char *url, const char *bearer_token, long *out_status);
void get_api_data();

#endif // SCRAPING_H
