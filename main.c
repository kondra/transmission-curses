#include <stdio.h>

#include "remote.c"

int main()
{
    const char *json_query = "{\"arguments\": {\"fields\":[\"id\",\"name\",\"totalSize\"]},\"method\":\"torrent-get\",\"tag\":39693}";

    char *json_data = json_exchange(json_query);

    printf("\n");
    puts(json_data);

    return 0;
}
