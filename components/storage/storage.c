#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "storage.h"

static const char *TAG = "storage";

#define PACKS_PARTITION "packs"

esp_err_t storage_init(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_PACKS_PATH,
        .partition_label = PACKS_PARTITION,
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mounting %s failed",
                        PACKS_PARTITION);

    size_t total = 0, free = 0;
    storage_get_usage(&total, &free);
    ESP_LOGI(TAG, "%s mounted: %u kB total, %u kB free", STORAGE_PACKS_PATH,
             (unsigned)(total / 1024), (unsigned)(free / 1024));
    return ESP_OK;
}

esp_err_t storage_get_usage(size_t *total, size_t *free)
{
    size_t used = 0;
    ESP_RETURN_ON_ERROR(esp_littlefs_info(PACKS_PARTITION, total, &used), TAG, "info failed");
    *free = *total - used;
    return ESP_OK;
}
