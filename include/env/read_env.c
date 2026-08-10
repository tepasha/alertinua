#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN 256

void load_env(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening .env file");
        return;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), file)) {
        // Strip trailing newline characters
        line[strcspn(line, "\r\n")] = 0;

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        // Find the delimiter
        char *delimiter = strchr(line, '=');
        if (!delimiter) {
            continue; 
        }

        // Split line into key and value
        *delimiter = '\0';
        char *key = line;
        char *value = delimiter + 1;

        // Load into environment variables (overwrite if exists)
        #ifdef _WIN32
            _putenv_s(key, value);
        #else
            setenv(key, value, 1);
        #endif
    }

    fclose(file);
}

char *read_env_var(const char *var_name) {
    load_env("../../.env");

    const char *token = getenv("TOKEN");
    
    if (token) {
        printf("TOKEN: %s\n", token);
        return strdup(token);
    } else {
        printf("TOKEN is not set.\n");
    }
    return NULL;
}
