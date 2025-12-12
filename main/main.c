#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "driver/i2s_pdm.h"

// ESP-SR 関連
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"      // WakeNet を使うなら
// #include "esp_mn_iface.h"   // コマンド認識(MultiNet)までやるなら


static const char *TAG = "APP";

// ===== PDM マイク設定 =====
#define I2S_SAMPLE_RATE      16000
#define I2S_BUFFER_SAMPLES   512

// AtomS3 + 外付け PDM マイク（例: Atom MIC）
// 必要に応じてここを変更
#define I2S_PIN_PDMCLK       1
#define I2S_PIN_PDMDIN       2

static i2s_chan_handle_t rx_chan;

// ===== ESP-SR (AFE + WakeNet) =====
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t  *afe_data   = NULL;

// ===== PDM マイク初期化 =====
static void pdm_mic_init(void)
{
    // I2S チャンネル設定
    i2s_chan_config_t chan_cfg = {
        .id           = I2S_NUM_0,
        .role         = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num= I2S_BUFFER_SAMPLES,
        .auto_clear   = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    // PDM RX 設定
    i2s_pdm_rx_config_t rx_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_PIN_PDMCLK,
            .din = I2S_PIN_PDMDIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    ESP_LOGI(TAG, "PDM mic init done (CLK=%d, DATA=%d)",
             I2S_PIN_PDMCLK, I2S_PIN_PDMDIN);
}

// ===== PDM マイクの簡易デバッグタスク（任意） =====
static void mic_debug_task(void *arg)
{
    int16_t buf[I2S_BUFFER_SAMPLES];
    size_t n_bytes = 0;

    while (1) {
        esp_err_t ret = i2s_channel_read(rx_chan, buf,
                                         sizeof(buf), &n_bytes,
                                         portMAX_DELAY);
        if (ret != ESP_OK || n_bytes == 0) {
            ESP_LOGW(TAG, "i2s read error: %d", ret);
            continue;
        }
        int n = n_bytes / sizeof(int16_t);
        long acc = 0;
        for (int i = 0; i < n; i++) {
            acc += labs(buf[i]);
        }
        long level = acc / n;
        ESP_LOGI("MIC", "level=%ld", level);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ===== ESP-SR (AFE + WakeNet) 初期化 =====
static void sr_init(void)
{
    // "model" パーティションから利用可能な SR モデルを列挙
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL) {
        ESP_LOGE(TAG, "esp_srmodel_init failed");
        return;
    }

    // input_format: "M" = モノラル
    // AFE_TYPE_SR: Speech Recognition 用
    // AFE_MODE_LOW_COST: AtomS3 のようなリソース少なめ向け
    afe_config_t *afe_config = afe_config_init("M", models,
                                               AFE_TYPE_SR,
                                               AFE_MODE_LOW_COST);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "afe_config_init failed");
        return;
    }

    // WakeNet は menuconfig で有効にしたモデルが使われる前提
    // ここでは個別に wakenet_init() は呼ばず、AFE 内に統合されている構成。

    // AFE インターフェース＆インスタンス生成
    afe_handle = esp_afe_handle_from_config(afe_config);
    if (afe_handle == NULL) {
        ESP_LOGE(TAG, "esp_afe_handle_from_config failed");
        return;
    }

    afe_data = afe_handle->create_from_config(afe_config);
    if (afe_data == NULL) {
        ESP_LOGE(TAG, "afe_handle->create_from_config failed");
        return;
    }

    ESP_LOGI(TAG, "ESP-SR AFE + WakeNet init done");
}

// ===== PDM → AFE に流し込むタスク =====
static void audio_feed_task(void *arg)
{
    if (afe_handle == NULL || afe_data == NULL) {
        ESP_LOGE(TAG, "AFE not initialized");
        vTaskDelete(NULL);
        return;
    }

    int feed_ch     = afe_handle->get_feed_channel_num(afe_data);
    int feed_length = afe_handle->get_feed_chunksize(afe_data);

    ESP_LOGI(TAG, "AFE feed_ch=%d, feed_length=%d",
             feed_ch, feed_length);

    // フィード用バッファ（SPIRAM 推奨）
    int16_t *buf = heap_caps_malloc(
        feed_ch * feed_length * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!buf) {
        ESP_LOGE(TAG, "Failed to malloc feed buffer");
        vTaskDelete(NULL);
        return;
    }

    size_t n_bytes;

    while (1) {
        // PDM から 16kHz PCM 読み取り
        esp_err_t ret = i2s_channel_read(
            rx_chan, buf,
            feed_length * sizeof(int16_t),
            &n_bytes,
            portMAX_DELAY
        );
        if (ret != ESP_OK || n_bytes == 0) {
            ESP_LOGW(TAG, "i2s read error: %d", ret);
            continue;
        }

        // AFE に渡す
        afe_handle->feed(afe_data, buf);
    }
}

// ===== WakeNet 結果をチェックするタスク =====
static void detect_task(void *arg)
{
    if (afe_handle == NULL || afe_data == NULL) {
        ESP_LOGE(TAG, "AFE not initialized");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // fetch() で AFE 出力 & WakeNet 状態を取得
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);

        if (res == NULL) {
            ESP_LOGW(TAG, "AFE fetch returned NULL");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            // 検出中の状態（オプション）
        } else if (res->wakeup_state == WAKENET_DETECTED) {
            // ウェイクワード検出！
            ESP_LOGI(TAG, "Wake word detected!");

            // TODO: 好きな処理に置き換える（LED 点灯、別タスク通知など）
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start AtomS3 + PDM mic WakeNet demo");

    // 1. PDM マイク初期化
    pdm_mic_init();

    // 2. SR (AFE + WakeNet) 初期化
    sr_init();

    // 3. マイクデバッグをしたい場合は下を有効に
    // xTaskCreate(mic_debug_task, "mic_debug", 4096, NULL, 5, NULL);

    // 4. AFE へのフィードタスク
    xTaskCreate(audio_feed_task, "audio_feed", 4096, NULL, 5, NULL);

    // 5. WakeNet 結果を監視するタスク
    xTaskCreate(detect_task, "detect_task", 4096, NULL, 5, NULL);
}
