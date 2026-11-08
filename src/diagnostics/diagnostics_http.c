#include "diagnostics_http.h"
#include "diagnostics_json.h"
#include "../app/app_control.h"
#include "esp_http_server.h"

// ---------------------------------------------------------
// READ-ONLY LOCAL HTTP ENDPOINTS
// ---------------------------------------------------------
static httpd_handle_t server;
static char response[2048]; // Handlers run serially in the HTTP server task.

static esp_err_t state_get(httpd_req_t *request)
{
    app_state_t state;
    if (app_state_get_snapshot(&state) != ESP_OK ||
        !diagnostics_state_json(response, sizeof(response), &state))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t diagnostics_get(httpd_req_t *request)
{
    diagnostics_snapshot_t snapshot;
    diagnostics_get_snapshot(&snapshot);
    if (!diagnostics_json(response, sizeof(response), &snapshot))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

esp_err_t diagnostics_http_start(void)
{
    if (server) return ESP_ERR_INVALID_STATE;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 2;
    config.stack_size = 4096;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t state_uri = { .uri = "/api/state", .method = HTTP_GET, .handler = state_get };
    const httpd_uri_t diag_uri = { .uri = "/api/diagnostics", .method = HTTP_GET, .handler = diagnostics_get };
    err = httpd_register_uri_handler(server, &state_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &diag_uri);
    if (err != ESP_OK) {
        httpd_stop(server);
        server = NULL;
    }
    return err;
}
