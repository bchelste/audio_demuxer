#include "audio_demuxer.h"

#include <fstream>

// error code for audio_demuxer

const audio_demuxer_err_category the_audio_demuxer_err_category {};

std::error_code make_error_code(audio_demuxer_errc e) {
    return {static_cast<int>(e), the_audio_demuxer_err_category};
}

const char* audio_demuxer_err_category::name() const noexcept {
    return "audio_demuxer";
}

std::string audio_demuxer_err_category::message(int ev) const {
    switch (static_cast<audio_demuxer_errc>(ev)) {
        case audio_demuxer_errc::SUCCESS:
            return "OK";
        case audio_demuxer_errc::OPEN_SRC_FILE_ERR:
            return "Could not open the source file!";
        case audio_demuxer_errc::GET_STREAM_INFO_ERR:
            return "Could not find stream information!";
        case audio_demuxer_errc::FIND_INPUT_STREAM_ERR:
            return "Could not find the best audio stream in the input file!";
        case audio_demuxer_errc::FIND_DECODER_ERR:
            return "Could not find the codec type!";
        case audio_demuxer_errc::ALLOC_CODEC_ERR:
            return "Could not allocate the codec context!";
        case audio_demuxer_errc::COPY_CODEC_PARAMS_ERR:
            return "Could not copy current codec parameters to the decoder context!";
        case audio_demuxer_errc::INIT_DECODER_ERR:
            return "Could not open the codec!";
        case audio_demuxer_errc::OPEN_OUTPUT_FSTREAM_ERR:
            return "Could not open the destination file stream!";
        case audio_demuxer_errc::ALLOC_IN_FRAME_ERR:
            return "Could not allocate the input frame!";
        case audio_demuxer_errc::ALLOC_PACKET_ERR:
            return "Could not allocate a packet!";
        case audio_demuxer_errc::WRONG_INIT_DATA_FOR_RESAMPLER:
            return "Wrong init data values for resampler!";
        case audio_demuxer_errc::INIT_RESAMPLER_ERR:
            return "Could not init the resampler!";
        case audio_demuxer_errc::CONVERT_SAMPLES_ERR:
            return "Error while converting samples!";
        case audio_demuxer_errc::SEND_PACKET_TO_DECODER_ERR:
            return "Send packet to decoder error!";
        case audio_demuxer_errc::RECEIVE_PACKET_FROM_DECODER_ERR:
            return "Receive packet from decoder error!";
        default:
            return "(unrecognized error)";
    }
}

// audio demuxer class

// public methods

audio_demuxer_obj::audio_demuxer_obj(std::filesystem::path &source_filename,
                                     int result_sample_rate_hz,
                                     AVSampleFormat result_format,
                                     int64_t result_ch_layout) :
        src_filename(source_filename),
        out_sample_rate_hz(result_sample_rate_hz),
        out_format(result_format),
        out_ch_layout(result_ch_layout),
        audio_stream_index(-1),
        in_fmt_ctx(nullptr),
        audio_decoder_ctx(nullptr),
        in_frame(nullptr),
        packet(nullptr),
        resampler(nullptr) {

}

audio_demuxer_obj::~audio_demuxer_obj() {
    clean_up_resources();
}

std::error_code audio_demuxer_obj::convert(const std::filesystem::path &output_file) {
    auto result = get_input_file_info();
    if (result != audio_demuxer_errc::SUCCESS) {
        return result;
    }

    std::fstream fs_out;
    fs_out.open(output_file, std::ios::out | std::ios::trunc);
    if (!fs_out.is_open()) {
        fs_out.clear();
        return audio_demuxer_errc::OPEN_OUTPUT_FSTREAM_ERR;
    }

    in_frame = av_frame_alloc();
    if (in_frame == nullptr) {
        return audio_demuxer_errc::ALLOC_IN_FRAME_ERR;
    }

    packet = av_packet_alloc();
    if (packet == nullptr) {
        return audio_demuxer_errc::ALLOC_PACKET_ERR;
    }

    result = init_resampler();
    if (result != audio_demuxer_errc::SUCCESS) {
        return result;
    }

    while (av_read_frame(in_fmt_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            result = decode_packet(packet, fs_out);
        }
        av_packet_unref(packet);
        if (result != audio_demuxer_errc::SUCCESS ) {
            break ;
        }
    }

    auto flash_result = decode_packet(nullptr, fs_out);
    if (flash_result != audio_demuxer_errc::SUCCESS) {
        return flash_result;
    }

    fs_out.clear();
    fs_out.close();

    return audio_demuxer_errc::SUCCESS;
}

// private methods

void audio_demuxer_obj::clean_up_resources() {
    if (audio_decoder_ctx != nullptr) {
        avcodec_free_context(&audio_decoder_ctx);
    }
    if (in_fmt_ctx != nullptr) {
        avformat_close_input(&in_fmt_ctx);
    }
    if (in_frame != nullptr) {
        av_frame_free(&in_frame);
    }
    if (packet != nullptr) {
        av_packet_free(&packet);
    }
}

std::error_code audio_demuxer_obj::open_codec_context(enum AVMediaType type) {

    audio_stream_index = av_find_best_stream(in_fmt_ctx, type, -1, -1, nullptr, 0);
    if (audio_stream_index < 0) {
        return audio_demuxer_errc::FIND_INPUT_STREAM_ERR;
    } else {
        if (in_fmt_ctx->streams[audio_stream_index] == nullptr) {
            return audio_demuxer_errc::FIND_INPUT_STREAM_ERR;
        }
    }

    const AVCodec *decoder = avcodec_find_decoder(in_fmt_ctx->streams[audio_stream_index]->codecpar->codec_id);
    if (decoder == nullptr) {
        return audio_demuxer_errc::FIND_DECODER_ERR;
    }

    audio_decoder_ctx = avcodec_alloc_context3(decoder);
    if (audio_decoder_ctx == nullptr) {
        return audio_demuxer_errc::ALLOC_CODEC_ERR;
    }

    if (avcodec_parameters_to_context(audio_decoder_ctx, in_fmt_ctx->streams[audio_stream_index]->codecpar) < 0) {
        return audio_demuxer_errc::COPY_CODEC_PARAMS_ERR;
    }

    if (avcodec_open2(audio_decoder_ctx, decoder, nullptr) < 0) {
        return audio_demuxer_errc::INIT_DECODER_ERR;
    }

    return audio_demuxer_errc::SUCCESS;

}

std::error_code audio_demuxer_obj::get_input_file_info() {

    if (avformat_open_input(&in_fmt_ctx, src_filename.c_str(), nullptr, nullptr) < 0) {
        clean_up_resources();
        return audio_demuxer_errc::OPEN_SRC_FILE_ERR;
    }

    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) {
        clean_up_resources();
        return audio_demuxer_errc::GET_STREAM_INFO_ERR;
    }

    auto result = open_codec_context();
    if (result != audio_demuxer_errc::SUCCESS) {
        return result;
    }

    return audio_demuxer_errc::SUCCESS;
}

std::error_code audio_demuxer_obj::init_resampler() {

    int64_t tmp = 0;
    if (audio_decoder_ctx->channel_layout == 0) {
        tmp = av_get_default_channel_layout(audio_decoder_ctx->channels);
    } else {
        if (audio_decoder_ctx->channel_layout < INT64_MAX) {
            tmp = static_cast<int64_t>(audio_decoder_ctx->channel_layout);
        }
    }
    if (tmp == 0) {
        return audio_demuxer_errc::WRONG_INIT_DATA_FOR_RESAMPLER;
    }

    auto tmp_resampler = audio_resampler_obj::create_audio_resampler_obj(tmp,
                                                                         audio_decoder_ctx->sample_rate,
                                                                         audio_decoder_ctx->sample_fmt,
                                                                         out_ch_layout,
                                                                         out_sample_rate_hz,
                                                                         out_format);
    if (tmp_resampler == nullptr) {
        return audio_demuxer_errc::INIT_RESAMPLER_ERR;
    }

    resampler = std::move(tmp_resampler);

    return audio_demuxer_errc::SUCCESS;
}

std::error_code audio_demuxer_obj::decode_packet(const AVPacket *current_packet, std::fstream &out_stream) {
    auto result = avcodec_send_packet(audio_decoder_ctx, current_packet);
    if (result < 0) {
        return audio_demuxer_errc::SEND_PACKET_TO_DECODER_ERR;
    }

    while (true) {
        result = avcodec_receive_frame(audio_decoder_ctx, in_frame);
        if (result < 0) {
            if (result == AVERROR_EOF || result == AVERROR(EAGAIN)) {
                break ;
            }
            return audio_demuxer_errc::RECEIVE_PACKET_FROM_DECODER_ERR;
        }

        std::vector<std::vector<uint8_t> > tmp_storage;
        auto convert_result = resampler->convert(in_frame, tmp_storage);
        av_frame_unref(in_frame);
        if (convert_result != audio_resampler_err::SUCCESS) {
            return audio_demuxer_errc::CONVERT_SAMPLES_ERR;
        }

        output_data(out_stream, tmp_storage, resampler->get_output_buf_size());

    }

    return audio_demuxer_errc::SUCCESS;
}

void audio_demuxer_obj::output_data(std::fstream &out_fs,
                                    std::vector<std::vector<uint8_t>> &data_storage,
                                    int data_size) {
    for (auto & item : data_storage) {
        out_fs.write(reinterpret_cast<const char *>(item.data()), data_size);
    }

}
