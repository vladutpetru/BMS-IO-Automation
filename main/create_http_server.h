#ifndef CREATE_HTTP_SERVER_H
#define CREATE_HTTP_SERVER_H

/* Mounts SPIFFS and starts the HTTP server on port 80.
 * Call after the Wi-Fi AP is up and app_state_init() has run. */
void http_server(void);

#endif /* CREATE_HTTP_SERVER_H */
