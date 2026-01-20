#include "DSPEngine.hpp"

DSPEngine::DSPEngine() 
    : Thread("DSPEngine", 4096, 8, 1)  // 绑定到 Core 1
{
}

void DSPEngine::processAndShow(float* data, int length)
{
    // 执行 FFT
    dsps_fft2r_fc32(data, length);
    // 位反转
    dsps_bit_rev_fc32(data, length);
    // 转换复数向量
    dsps_cplx2reC_fc32(data, length);

    // 计算功率谱 (dB)
    for (int i = 0; i < length / 2; i++) {
        float real = data[i * 2 + 0];
        float imag = data[i * 2 + 1];
        data[i] = 10 * log10f((real * real + imag * imag) / N);
    }

    // 显示功率谱 (64x10 窗口, -120 到 40 dB)
    dsps_view(data, length / 2, 64, 10, -120, 40, '|');
}

void DSPEngine::run()
{
    ESP_LOGI(TAG, "*** DSPEngine Task Started on Core 1 ***");
    
    // 初始化 FFT
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, N_SAMPLES);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT initialization failed. Error = %i", ret);
        return;
    }
    fft_initialized_ = true;
    ESP_LOGI(TAG, "FFT initialized successfully");

    // 生成汉宁窗（只需一次）
    dsps_wind_hann_f32(wind_, N);

    // 主循环
    while (true) {
        // === 示例：生成测试信号并处理 ===
        // TODO: 后续替换为从 BNO055 接收真实数据
        
        // 生成测试正弦波信号 (频率 = 0.2 * 采样率)
        dsps_tone_gen_f32(x1_, N, 1.0f, 0.2f, 0);
        
        // 加窗处理：将输入信号与汉宁窗相乘，存入复数数组的实部
        for (int i = 0; i < N; i++) {
            y_cf_[i * 2 + 0] = x1_[i] * wind_[i];  // 实部
            y_cf_[i * 2 + 1] = 0;                   // 虚部置零
        }
        
        // 执行 FFT 并显示频谱
        processAndShow(y_cf_, N);
        
        // 延时等待下一次处理（后续可由数据就绪事件触发）
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
