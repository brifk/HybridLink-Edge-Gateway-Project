#include "DSPEngine.hpp"

void DSPEngine::processAndShow(float* data, int length)
{
    // 1. FFT 运算
    dsps_fft2r_fc32(data, length);

    // 2. 位反转 (必要步骤，让频率顺序正常)
    dsps_bit_rev_fc32(data, length);

    // 3. 计算功率谱 (dB)并存回 data 数组的前半部分
    for (int i = 0; i < length / 2; i++) {
        float real = data[i * 2 + 0];
        float imag = data[i * 2 + 1];

        // 功率 = 实部平方 + 虚部平方
        // 除以 N 是归一化
        float power = (real * real + imag * imag) / length;

        if (power < 1e-10)
            power = 1e-10;

        power_data[i] = 10 * log10f(power + 1e-9);
        // ESP_LOGI(TAG, "Power at bin %d: %f dB", i, power_data[i]);
    }

    double output_scores[2];
    Model model;
    model.score(power_data, output_scores);
    // ESP_LOGI(TAG, "Output Scores: %f, %f", output_scores[0], output_scores[1]);
    if (output_scores[1] > output_scores[0]) {
        ESP_LOGE("AI", "检测到异常！");
    } else {
        // ESP_LOGI("AI", "系统正常");
    }
    // TODO: 这里后面可以改成MQTT发送结果
}

void DSPEngine::run()
{
    // 1. 初始化
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, N_SAMPLES);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT Init Failed: %d", ret);
        return;
    }
    dsps_wind_hann_f32(wind_, N); // 生成窗函数
    fft_initialized_ = true;
    double linear_accel_z = 0;

    while (true) {
        // 这样如果没有数据，任务会挂起，不占用 CPU，比非阻塞好
        if (xQueueReceive(bno055->get_linear_accel_z_queue_handle(), &linear_accel_z, portMAX_DELAY)) {

            // 3. 填入乒乓缓存
            input_buffers_[write_buffer_idx_][write_sample_idx_] = static_cast<float>(linear_accel_z);
            write_sample_idx_++;

            if (write_sample_idx_ >= N) {
                // A. 获取刚刚填满的 buffer 指针
                auto process_ptr = input_buffers_[write_buffer_idx_];

                // B. 切换到另一个 buffer 继续接收 (让下一轮循环使用)
                write_buffer_idx_ = !write_buffer_idx_; // 0 -> 1, 1 -> 0
                write_sample_idx_ = 0;

                // C. 准备 FFT 输入数据 (加窗 + 构造复数)
                for (int i = 0; i < N; i++) {
                    // 实部 = 原始数据 * 窗函数
                    y_cf_[i * 2 + 0] = process_ptr[i] * wind_[i];
                    // 虚部 = 0
                    y_cf_[i * 2 + 1] = 0;
                }

                // D. 执行 FFT 计算
                processAndShow(y_cf_, N);
            }
        }
    }
}