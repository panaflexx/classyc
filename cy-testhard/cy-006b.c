/* Verify dict value unwrap in various consumption contexts */
#include <stdio.h>
#include <string.h>

dict cfg = {
    "server": { "host": "localhost", "port": 8080 },
    "name": "ada",
    "count": 7
};

void show(char *s) { printf("show: %s\n", s); }

int main() {
    /* chained string value as printf %s */
    printf("host=%s port=%d\n", cfg.server.host, cfg.server.port);
    /* single-level string + int */
    printf("name=%s count=%d\n", cfg.name, cfg.count);
    /* bracket subscript string value */
    printf("bracket=%s\n", cfg["name"]);
    /* pass dict string value to a char* parameter */
    show(cfg.name);
    /* pass whole nested dict (box) to a dict parameter via json() */
    printf("server json=%s\n", json(cfg.server));
    return 0;
}
