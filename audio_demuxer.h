#include <filesystem>
#include <vector>
#include <memory>
#include <system_error>

extern "C" {
//decoder
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#define __STDC_CONSTANT_MACROS

#include "audio_resampler.h"

// error code

enum class audio_demuxer_errc {
    SUCCESS = 0x00,
    OPEN_SRC_FILE_ERR,
    GET_STREAM_INFO_ERR,
    FIND_INPUT_STREAM_ERR,
    FIND_DECODER_ERR,
    ALLOC_CODEC_ERR,
    COPY_CODEC_PARAMS_ERR,
    INIT_DECODER_ERR,
    OPEN_OUTPUT_FSTREAM_ERR,
    ALLOC_IN_FRAME_ERR,
    ALLOC_PACKET_ERR,
    WRONG_INIT_DATA_FOR_RESAMPLER,
    INIT_RESAMPLER_ERR,
    CONVERT_SAMPLES_ERR,
    SEND_PACKET_TO_DECODER_ERR,
    RECEIVE_PACKET_FROM_DECODER_ERR,

};

template<>
struct std::is_error_code_enum<audio_demuxer_errc> : std::true_type {};

std::error_code make_error_code(audio_demuxer_errc);

struct audio_demuxer_err_category : std::error_category {
    const char * name() const noexcept override;
    std::string message(int ev) const override;
};

// audio demuxer

class audio_demuxer_obj final {
public:

    audio_demuxer_obj(std::filesystem::path &source_filename,
                      int result_sample_rate_hz,
                      AVSampleFormat result_format,
                      int64_t result_ch_layout);
    ~audio_demuxer_obj();

    std::error_code convert(const std::filesystem::path &output_file);

private:

    std::filesystem::path   src_filename;
    int                     out_sample_rate_hz;
    AVSampleFormat          out_format;
    std::int64_t            out_ch_layout;
    int                     audio_stream_index;

    AVFormatContext         *in_fmt_ctx;
    AVCodecContext          *audio_decoder_ctx;
    AVFrame                 *in_frame;
    AVPacket                *packet;

    std::unique_ptr<audio_resampler_obj> resampler;

    void clean_up_resources();
    std::error_code open_codec_context(enum AVMediaType type = AVMEDIA_TYPE_AUDIO);
    std::error_code get_input_file_info();
    std::error_code init_resampler();
    std::error_code decode_packet(const AVPacket *current_packet, std::fstream &out_stream);
    static void output_data(std::fstream &out_fs,
                            std::vector<std::vector<uint8_t> > &data_storage,
                            int data_size);

};
