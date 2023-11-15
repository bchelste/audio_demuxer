#include <iostream>

#include "audio_demuxer.h"

int main() {
    std::cout << "___audio_transcoder___" << std::endl;

    std::filesystem::path src_filename = "tmam_proxy.mp4";
    std::filesystem::path out_filename = "result";

    int output_sample_rate_hz = 16000;
    AVSampleFormat output_format  = AV_SAMPLE_FMT_S16;
    int64_t output_ch_layout = AV_CH_LAYOUT_MONO;

    auto transcoder = audio_demuxer_obj(src_filename,
                                        output_sample_rate_hz,
                                        output_format,
                                        output_ch_layout);
    auto result = transcoder.convert(out_filename);
    std::cout << "error code: " << result.value() << " - " << result.message() << std::endl;

    return 0;
}