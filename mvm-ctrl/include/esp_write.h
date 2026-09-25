

#pragma once

#include <cstddef>

enum EspResult
{
    ESP_OK             = 0,
    ESP_NOT_ADMIN      = 1,
    ESP_MOUNT_FAILED   = 2,
    ESP_FILE_MISSING   = 3,
    ESP_COPY_FAILED    = 4,
    ESP_INTERNAL_ERROR = 5,
};

const char* esp_result_str(EspResult r);

EspResult esp_write_file(const wchar_t* esp_rel_path, const void* data, size_t len);

EspResult esp_read_file(const wchar_t* esp_rel_path, void** out_data, size_t* out_len);

void esp_free(void* p);
