#pragma once
#include "Thread.hpp"
#include "APPConfig.h"
#include "esp_log.h"
#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bno055driver.hpp"
#include <memory>
#include <array>
#include <cmath>

#define N_SAMPLES 512

class DSPEngine : public Thread {
public:
    DSPEngine(std::shared_ptr<Bno055Driver> bno055) : 
            Thread("DSPEngine", 1024 * 10, PRIO_FFT, 1), 
            bno055(std::move(bno055)) { };
    ~DSPEngine() = default;
    
    void run() override;

private:
    std::shared_ptr<Bno055Driver> bno055;

    static constexpr auto TAG = "DSPEngine";
    static constexpr int N = N_SAMPLES;
    
    alignas(16) std::array<std::array<float, N>, 2> input_buffers_{};  
    int write_buffer_idx_ = 0;  // 当前正在写入哪个 buffer (0 或 1)
    int write_sample_idx_ = 0;  // 当前写到了第几个点
    alignas(16) std::array<float, N_SAMPLES> wind_{};         // 窗函数系数
    alignas(16) std::array<float, N_SAMPLES * 2> y_cf_{};     // 复数工作数组
    
    bool fft_initialized_ = false;
    
    // FFT 处理并显示频谱
    void processAndShow(float* data, int length);
};