#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "scraping.h"

struct ApiResponse {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t chunk_size = size * nmemb;
    struct ApiResponse *resp = (struct ApiResponse *)userp;
 
    char *new_data = realloc(resp->data, resp->size + chunk_size + 1);
    if (!new_data) {
        fprintf(stderr, "Out of memory while receiving response\n");
        return 0;
    }
 
    resp->data = new_data;
    memcpy(&(resp->data[resp->size]), contents, chunk_size);
    resp->size += chunk_size;
    resp->data[resp->size] = '\0';
 
    return chunk_size;
}

char *api_fetch_bearer_auth(const char *url, const char *bearer_token, long *out_status) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        return NULL;
    }
 
    struct ApiResponse resp;
    resp.data = malloc(1);
    resp.size = 0;
    if (!resp.data) {
        fprintf(stderr, "Out of memory\n");
        curl_easy_cleanup(curl);
        return NULL;
    }
    resp.data[0] = '\0';
 
    char auth_header[2048];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", bearer_token);
 
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/json");
 
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&resp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "c-api-client/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
 
    CURLcode res = curl_easy_perform(curl);
 
    if (res != CURLE_OK) {
        fprintf(stderr, "Request failed: %s\n", curl_easy_strerror(res));
        free(resp.data);
        resp.data = NULL;
    } else if (out_status) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, out_status);
    }
 
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
 
    return resp.data;
}
 
void get_api_data() {
    const char *api_url = "https://api.example.com/v1/resource";
    const char *token   = getenv("API_BEARER_TOKEN");
 
    if (!token) {
        fprintf(stderr, "Set the API_BEARER_TOKEN environment variable first.\n");
        return 1;
    }
 
    curl_global_init(CURL_GLOBAL_ALL);
 
    long status = 0;
    char *body = api_fetch_bearer_auth(api_url, token, &status);
 
    if (body) {
        printf("HTTP status: %ld\n", status);
        printf("Response body:\n%s\n", body);
        free(body);
    } else {
        fprintf(stderr, "Request failed.\n");
    }
 
    curl_global_cleanup();
    return 0;
}
