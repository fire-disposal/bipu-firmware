/**
 * @file kalman_health_demo.c
 * @brief 卡尔曼滤波在健康监测中的应用 (ESP-IDF)
 *        展示算法本身结构, 避免冗长演示代码
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "KALMAN";

/*============================== 卡尔曼滤波器核心 =============================*/

typedef struct {
    float x;  // 最优估计
    float P;  // 估计误差协方差
    float Q;  // 过程噪声 (模型信任度)
    float R;  // 观测噪声 (传感器信任度)
    float K;  // 卡尔曼增益
} kalman_t;

static void kalman_init(kalman_t *k, float x0, float P0, float Q, float R) {
    k->x = x0; k->P = P0; k->Q = Q; k->R = R;
}

/**
 * @brief 卡尔曼滤波一步更新 (预测+修正)
 *        算法本质就这三行:
 *          P_pred = P + Q                 ① 预测: 不确定性增大
 *          K = P_pred / (P_pred + R)     ② 增益: 权衡预测vs观测
 *          x = x + K * (z - x)           ③ 修正: 加权残差
 */
static float kalman_step(kalman_t *k, float z) {
    float P_pred = k->P + k->Q;                     // ① 预测协方差
    k->K         = P_pred / (P_pred + k->R);        // ② 卡尔曼增益
    k->x         = k->x + k->K * (z - k->x);        // ③ 状态更新
    k->P         = (1.0f - k->K) * P_pred;           // 更新协方差
    return k->x;
}

/*======================== 健康监测场景: 模拟含噪信号 ========================*/

// 模拟传感器读数: 真实值 + 噪声, 以及偶尔的脉冲干扰 (运动伪迹/接触不良)
static float sensor(float truth, float noise_amp, int pulse_period, float pulse_amp, int tick) {
    float noise = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * noise_amp;
    float pulse = (pulse_period && (tick % pulse_period == 0))
                  ? ((float)rand() / RAND_MAX - 0.5f) * 2.0f * pulse_amp : 0;
    return truth + noise + pulse;
}

/*============================== 主任务 ======================================*/

static void task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "卡尔曼滤波 · 健康监测算法演示");
    ESP_LOGI(TAG, "──────────────────────────────────────────────────────────");
    ESP_LOGI(TAG, "信号    真实值   Q(过程)  R(观测)  含义");
    ESP_LOGI(TAG, "──────────────────────────────────────────────────────────");
    ESP_LOGI(TAG, "心率      72   0.10     3.0     快变, 传感器噪声大");
    ESP_LOGI(TAG, "血氧      98   0.02     1.2     缓变, 中等噪声");
    ESP_LOGI(TAG, "体温    36.5   0.005    0.15    慢变, 高精度传感器");
    ESP_LOGI(TAG, "──────────────────────────────────────────────────────────");

    /* 三个独立滤波器, 参数针对不同生理信号特性调优 */
    kalman_t kf[] = {
        { .x=72,  .P=1,    .Q=0.10,  .R=3.0  },   // 心率:  Q大 → 快速跟踪
        { .x=98,  .P=1,    .Q=0.02,  .R=1.2  },   // 血氧:  Q中 → 中等平滑
        { .x=36.5,.P=1,    .Q=0.005, .R=0.15 },   // 体温:  Q小 → 强平滑
    };
    const float truth[] = { 72, 98, 36.5 };
    const char *name[]  = { "HR", "SpO2", "TEMP" };

    int tick = 0;
    while (1) {
        float hr_r   = sensor(72,   6, 50,  10, tick);    // ±6,  每50步±10脉冲
        float spo2_r = sensor(98,   2, 80,   5, tick);    // ±2,  每80步±5  脉冲
        float temp_r = sensor(36.5, 0.3, 0,   0, tick);   // ±0.3, 无脉冲

        float hr_f   = kalman_step(&kf[0], hr_r);
        float spo2_f = kalman_step(&kf[1], spo2_r);
        float temp_f = kalman_step(&kf[2], temp_r);

        if (tick % 5 == 0) {
            ESP_LOGI(TAG, "%4d  HR:%5.1f→%5.1f  SpO2:%5.1f→%5.1f  TEMP:%5.2f→%5.2f",
                     tick, hr_r, hr_f, spo2_r, spo2_f, temp_r, temp_f);
        }

        // 健康告警 (基于滤波值, 更可靠)
        if (tick % 20 == 0) {
            if (hr_f > 100)   ESP_LOGW(TAG, "↑ 心率 %.1f", hr_f);
            if (hr_f < 50)    ESP_LOGW(TAG, "↓ 心率 %.1f", hr_f);
            if (spo2_f < 94)  ESP_LOGE(TAG, "⚠ 血氧 %.1f%%", spo2_f);
            if (temp_f > 37.5) ESP_LOGW(TAG, "↑ 体温 %.2f", temp_f);
            if (temp_f < 35.5) ESP_LOGW(TAG, "↓ 体温 %.2f", temp_f);
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    xTaskCreatePinnedToCore(task, "kalman", 4096, NULL, 5, NULL, 0);
}
