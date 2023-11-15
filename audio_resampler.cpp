#include "audio_resampler.h"

// public methods

std::unique_ptr<audio_resampler_obj> audio_resampler_obj::create_audio_resampler_obj(int64_t input_ch_layout,
                                                                                     int input_rate,
                                                                                     AVSampleFormat input_sample_fmt,
                                                                                     int64_t output_ch_layout,
                                                                                     int output_rate,
                                                                                     AVSampleFormat output_sample_fmt) {
    auto tmp_resampler = audio_resampler_obj(input_ch_layout,
                                             input_rate,
                                             input_sample_fmt,
                                             output_ch_layout,
                                             output_rate,
                                             output_sample_fmt);

    auto result = tmp_resampler.init_audio_resampler();
    if (result != audio_resampler_err::SUCCESS) {
        return nullptr;
    }

    return std::make_unique<audio_resampler_obj>(std::move(tmp_resampler));
}

audio_resampler_obj::~audio_resampler_obj() {
    if (src_data != nullptr) {
        av_freep(&src_data[0]);
        av_freep(&src_data);
    }
    if (dst_data != nullptr) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }
    if (swr_ctx != nullptr){
        swr_free(&swr_ctx);
    }
}

audio_resampler_obj::audio_resampler_obj(audio_resampler_obj &&other) noexcept :
        src_ch_layout(other.src_ch_layout),
        src_rate(other.src_rate),
        src_sample_fmt(other.src_sample_fmt),
        dst_ch_layout(other.src_ch_layout),
        dst_rate(other.dst_rate),
        dst_sample_fmt(other.dst_sample_fmt),
        src_nb_channels(other.src_nb_channels),
        dst_nb_channels(other.dst_nb_channels),
        output_nb_samples(other.output_nb_samples),
        max_dst_nb_samples(other.max_dst_nb_samples) {

    swr_ctx = other.swr_ctx;
    other.swr_ctx = nullptr;

    src_data = other.src_data;
    other.src_data = nullptr;

    dst_data = other.dst_data;
    other.dst_data = nullptr;
}

audio_resampler_obj &audio_resampler_obj::operator=(audio_resampler_obj &&other) noexcept {
    if (&other == this) {
        return *this;
    }

    if (swr_ctx != nullptr) {
        swr_free(&swr_ctx);
    }
    swr_ctx = other.swr_ctx;
    other.swr_ctx = nullptr;

    src_ch_layout = other.src_ch_layout;
    src_rate = other.src_rate;
    src_sample_fmt = other.src_sample_fmt;

    dst_ch_layout = other.src_ch_layout;
    dst_rate = other.dst_rate;
    dst_sample_fmt = other.dst_sample_fmt;

    if (src_data != nullptr) {
        av_freep(&src_data[0]);
        av_freep(&src_data);
    }
    src_data = other.src_data;
    other.src_data = nullptr;

    if (dst_data != nullptr) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }
    dst_data = other.dst_data;
    other.dst_data = nullptr;

    src_linesize = other.src_linesize;
    dst_linesize = other.dst_linesize;

    src_nb_channels = other.src_nb_channels;
    dst_nb_channels = other.dst_nb_channels;

    output_nb_samples = other.output_nb_samples;
    max_dst_nb_samples = other.max_dst_nb_samples;

    return *this;
}

audio_resampler_err audio_resampler_obj::convert(AVFrame *frame, std::vector<std::vector<uint8_t> > &data_storage) {

    auto result = av_samples_alloc_array_and_samples(&src_data,
                                                     &src_linesize,
                                                     src_nb_channels,
                                                     frame->nb_samples,
                                                     src_sample_fmt,
                                                     0);
    if (result < 0) {
        return audio_resampler_err::ALLOC_SRC_SAMPLES_ERR;
    }

    auto tmp_nb_samples = av_rescale_rnd(frame->nb_samples,
                                         dst_rate,
                                         src_rate,
                                         AV_ROUND_UP);
    if ((tmp_nb_samples < INT_MAX) && (tmp_nb_samples > INT_MIN)) {
        max_dst_nb_samples = output_nb_samples = static_cast<int>(tmp_nb_samples);
    } else {
        return audio_resampler_err::OUTPUT_NB_SAMPLES_ERR;
    }

    result = av_samples_alloc_array_and_samples(&dst_data,
                                                &dst_linesize,
                                                dst_nb_channels,
                                                output_nb_samples,
                                                dst_sample_fmt,
                                                0);
    if (result < 0) {
        return audio_resampler_err::ALLOC_DST_SAMPLES_ERR;
    }

    tmp_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
            dst_rate,
            frame->sample_rate,
            AV_ROUND_UP);

    if ((tmp_nb_samples < INT_MAX) && (tmp_nb_samples > INT_MIN)) {
        output_nb_samples = static_cast<int>(tmp_nb_samples);
    } else {
        return audio_resampler_err::OUTPUT_NB_SAMPLES_ERR;
    }

    if (output_nb_samples > max_dst_nb_samples) {
        av_freep(&dst_data[0]);

        result = av_samples_alloc(dst_data,
                                  &dst_linesize,
                                  dst_nb_channels,
                                  output_nb_samples,
                                  dst_sample_fmt,
                                  1);
        if (result < 0) {
            return audio_resampler_err::ALLOC_DST_SAMPLES_ERR;
        }

        max_dst_nb_samples = output_nb_samples;
    }

    auto st_line_size = static_cast<size_t>(src_linesize);
    for (int i = 0; i < src_nb_channels; ++i) {
        memcpy(src_data[i], frame->extended_data[i], st_line_size);
    }

    auto current_samples_amount = swr_convert(swr_ctx,
                                              dst_data,
                                              output_nb_samples,
                                              (const uint8_t **)src_data,
                                              frame->nb_samples);
    if (current_samples_amount < 0) {
        return audio_resampler_err::CONVERTING_ERR;
    }

    auto dst_bufsize = av_samples_get_buffer_size(&dst_linesize,
                                                  dst_nb_channels,
                                                  current_samples_amount,
                                                  dst_sample_fmt,
                                                  1);
    if (dst_bufsize < 0) {
        return audio_resampler_err::DST_SAMPLE_BUF_SIZE_ERR;
    }

    for (int i = 0; i < dst_nb_channels; ++i) {
        data_storage.emplace_back(dst_data[i], dst_data[i] + dst_bufsize);
    }

    return audio_resampler_err::SUCCESS;
}

int audio_resampler_obj::get_output_buf_size() const {
    return dst_linesize;
}

// private methods

audio_resampler_obj::audio_resampler_obj(int64_t input_ch_layout,
                                         int input_rate,
                                         AVSampleFormat input_sample_fmt,
                                         int64_t output_ch_layout,
                                         int output_rate,
                                         AVSampleFormat output_sample_fmt) :
        swr_ctx(nullptr),
        src_ch_layout(input_ch_layout),
        src_rate(input_rate),
        src_sample_fmt(input_sample_fmt),
        dst_ch_layout(output_ch_layout),
        dst_rate(output_rate),
        dst_sample_fmt(output_sample_fmt) {

}

audio_resampler_err audio_resampler_obj::init_audio_resampler() {
    swr_ctx = swr_alloc();
    if (swr_ctx == nullptr) {
        return audio_resampler_err::ALLOC_RESAMPLER_CTX_ERR;
    }

    av_opt_set_int(swr_ctx, "in_channel_layout",src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

    av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    if ((swr_init(swr_ctx)) < 0) {
        return audio_resampler_err::INIT_SWR_CTX_ERR;
    }

    src_nb_channels = av_get_channel_layout_nb_channels(static_cast<std::uint64_t>(src_ch_layout));
    dst_nb_channels = av_get_channel_layout_nb_channels(static_cast<std::uint64_t>(dst_ch_layout));

    return audio_resampler_err::SUCCESS;
}