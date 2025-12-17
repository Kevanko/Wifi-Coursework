#include "webserver.h"
#include "thermostat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/semphr.h"

static const char *TAG = "WEB";

/* Symbols from EMBED_TXTFILES (CMake) */
extern const uint8_t _binary_page_html_gz_start[];
extern const uint8_t _binary_page_html_gz_end[];

extern const uint8_t _binary_style_css_gz_start[];
extern const uint8_t _binary_style_css_gz_end[];

extern const uint8_t _binary_script_js_gz_start[];
extern const uint8_t _binary_script_js_gz_end[];


static esp_err_t send_gzip_asset(httpd_req_t *req, const uint8_t *start, const uint8_t *end, const char *content_type)
{
    size_t size = (size_t)(end - start);
    if (size == 0) {
        ESP_LOGE(TAG, "Empty asset");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    if (strstr(content_type, "text/html") || strstr(content_type, "json")) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000");
    }

    esp_err_t err = httpd_resp_send(req, (const char *)start, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_send failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t root_handler(httpd_req_t* req) {
  const uint8_t* data = _binary_page_html_gz_start;
  size_t size = _binary_page_html_gz_end - _binary_page_html_gz_start;
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control",
                     "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");
  httpd_resp_send(req, (const char*)data, size);
  return ESP_OK;
}

static esp_err_t api_data_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        cJSON_AddNumberToObject(root, "temp", current_temperature);
        cJSON *limits = cJSON_CreateArray();
        for (int i = 0; i < 5; ++i) {
            cJSON_AddItemToArray(limits, cJSON_CreateNumber(g_settings.thresholds[i]));
        }
        cJSON_AddItemToObject(root, "limits", limits);
        xSemaphoreGive(settings_mutex);
    } else {
        // fallback: still try to return something
        cJSON_AddNumberToObject(root, "temp", current_temperature);
        cJSON *limits = cJSON_CreateArray();
        for (int i = 0; i < 5; ++i) {
            cJSON_AddItemToArray(limits, cJSON_CreateNumber(g_settings.thresholds[i]));
        }
        cJSON_AddItemToObject(root, "limits", limits);
        ESP_LOGW(TAG, "api_data: mutex timeout");
    }

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        free(out);
    } else {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_settings_post_handler(httpd_req_t *req)
{
    int remaining = req->content_len;
    if (remaining <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }
    const int MAX_PAYLOAD = 1024;
    if (remaining > MAX_PAYLOAD) {
        ESP_LOGW(TAG, "Payload too large: %d", remaining);
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Payload too large");
        return ESP_FAIL;
    }

    char *buf = malloc(remaining + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int to_read = remaining;
    int total = 0;
    while (to_read > 0) {
        int r = httpd_req_recv(req, buf + total, to_read);
        if (r <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        total += r;
        to_read -= r;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    cJSON *limits = cJSON_GetObjectItem(root, "limits");
    if (!limits || !cJSON_IsArray(limits) || cJSON_GetArraySize(limits) != 5) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        for (int i = 0; i < 5; ++i) {
            cJSON *it = cJSON_GetArrayItem(limits, i);
            if (it && cJSON_IsNumber(it)) {
                g_settings.thresholds[i] = (float)it->valuedouble;
            }
        }
        xSemaphoreGive(settings_mutex);
    } else {
        ESP_LOGW(TAG, "settings_post: mutex timeout; ignoring update");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
static esp_err_t css_handler(httpd_req_t *req)
{
    return send_gzip_asset(req,
                           _binary_style_css_gz_start,
                           _binary_style_css_gz_end,
                           "text/css");
}

static esp_err_t js_handler(httpd_req_t *req)
{
    return send_gzip_asset(req,
                           _binary_script_js_gz_start,
                           _binary_script_js_gz_end,
                           "application/javascript");
}


void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;

    httpd_handle_t server = NULL;
    esp_err_t rc = httpd_start(&server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(rc));
        return;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root);

    httpd_uri_t css = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &css);

    httpd_uri_t js = {
        .uri = "/script.js",
        .method = HTTP_GET,
        .handler = js_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js);

    httpd_uri_t api_data = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = api_data_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_data);

    httpd_uri_t api_set = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = api_settings_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_set);

    ESP_LOGI(TAG, "HTTP server started");
}
