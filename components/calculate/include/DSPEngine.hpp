#pragma once
#include "Thread.hpp"
#include "esp_log.h"
#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define N_SAMPLES 1024

class DSPEngine : public Thread {
public:
    DSPEngine();
    ~DSPEngine() = default;
    
    void run() override;

private:
    static constexpr auto TAG = "DSPEngine";
    static constexpr int N = N_SAMPLES;
    
    // 16字节对齐的数组（作为类成员）
    alignas(16) float x1_[N_SAMPLES];           // 输入信号
    alignas(16) float wind_[N_SAMPLES];         // 窗函数系数
    alignas(16) float y_cf_[N_SAMPLES * 2];     // 复数工作数组
    
    bool fft_initialized_ = false;
    
    // FFT 处理并显示频谱
    void processAndShow(float* data, int length);
};