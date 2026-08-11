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
        
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        char *delimiter = strchr(line, '=');
        if (!delimiter) {
            continue; 
        }

        *delimiter = '\0';
        char *key = line;
        char *value = delimiter + 1;

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
