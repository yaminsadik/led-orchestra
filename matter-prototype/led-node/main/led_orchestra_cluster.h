#pragma once

#include <esp_err.h>
#include <esp_matter.h>

esp_err_t led_orchestra_cluster_create(esp_matter::endpoint_t *endpoint);
