#include <memory>
#include <vector>
#include <cstdint>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

enum class audio_resampler_err : std::uint64_t {
    SUCCESS = 0x00,
    ALLOC_RESAMPLER_CTX_ERR,
    INIT_SWR_CTX_ERR,
    ALLOC_SRC_SAMPLES_ERR,
    ALLOC_DST_SAMPLES_ERR,
    CONVERTING_ERR,
    DST_SAMPLE_BUF_SIZE_ERR,
    OUTPUT_NB_SAMPLES_ERR,
    amount,
};

class audio_resampler_obj final {
public:
    static std::unique_ptr<audio_resampler_obj> create_audio_resampler_obj(int64_t input_ch_layout,
                                                                           int input_rate,
                                                                           AVSampleFormat input_sample_fmt,
                                                                           int64_t output_ch_layout,
                                                                           int output_rate,
                                                                           AVSampleFormat output_sample_fmt);
    ~audio_resampler_obj();
    // Disallow copying
    audio_resampler_obj(audio_resampler_obj &other) = delete;
    audio_resampler_obj &operator=(audio_resampler_obj &other) = delete;

    audio_resampler_obj(audio_resampler_obj &&other) noexcept ;
    audio_resampler_obj &operator=(audio_resampler_obj &&other) noexcept;

    audio_resampler_err convert(AVFrame *frame, std::vector<std::vector<uint8_t> > &data_storage);
    int get_output_buf_size() const;

private:

    SwrContext *swr_ctx;

    int64_t src_ch_layout;
    int src_rate;
    AVSampleFormat src_sample_fmt;

    int64_t dst_ch_layout;
    int dst_rate;
    AVSampleFormat dst_sample_fmt;

    int src_nb_channels = 0;
    int dst_nb_channels = 0;
    int output_buffsize = 0;

    explicit audio_resampler_obj(int64_t input_ch_layout,
                                 int input_rate,
                                 AVSampleFormat input_sample_fmt,
                                 int64_t output_ch_layout,
                                 int output_rate,
                                 AVSampleFormat output_sample_fmt);

    audio_resampler_err init_audio_resampler();
};
