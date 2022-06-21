#include <iostream>
#include <sstream>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

enum Code {
    SUCCESS = 0,
    ERROR   = -1,
};

struct SwsContext_Deleter {
    void operator() (SwsContext* ptr) {
        if (ptr != nullptr) {
            sws_freeContext(ptr);
        }
    }
};

static
int decode_packet(AVPacket* pPacket, AVCodecContext *pCodecContext,
        AVFrame* pFrame) {
    int res = -1;
    int cvLinesizes[1] = {0};
    std::ostringstream os("frame");
    AVPixelFormat src_pix_fmt = AV_PIX_FMT_YUV420P;
    AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGB24;
    std::unique_ptr<SwsContext, SwsContext_Deleter> sws_ctx(nullptr, SwsContext_Deleter());
    cv::Mat image;

    /* Supply raw packet data as input to a decoder. */
    res = avcodec_send_packet(pCodecContext, pPacket);
    if (res != SUCCESS) {
        std::cout << "Failed to send packet to decoder" << std::endl;

        return res;
    }

    /* Return decoded output data from a decoder. */
    res = avcodec_receive_frame(pCodecContext, pFrame);
    if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
        return SUCCESS;
    } else if (res != SUCCESS) {
        std::cout << "Failed to receive frame from decoder" << std::endl;

        return res;
    }

    std::cout << "Frame " << pCodecContext->frame_number <<
                " (type=" << av_get_picture_type_char(pFrame->pict_type) <<
                ", size=" << pFrame->pkt_size <<
                " bytes, format=" << pFrame->format <<
                ") pts " << pFrame->pts << " " << pFrame->width <<
                " x " << pFrame->height <<
                " key_frame " << pFrame->key_frame <<
                " [DTS " << pFrame->coded_picture_number << "]" << std::endl;

    /* create scaling context */
    sws_ctx.reset(sws_getContext(pFrame->width, pFrame->height, src_pix_fmt,
                                 pFrame->width, pFrame->height, dst_pix_fmt,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr));
    image = cv::Mat(pFrame->height, pFrame->width, CV_8UC3);

    cvLinesizes[0] = image.step1();

    /* convert to destination format */
    sws_scale(sws_ctx.get(), pFrame->data, pFrame->linesize, 0, pFrame->height,
              &image.data, cvLinesizes);

    os << pCodecContext->frame_number << ".jpg";

    cv::imwrite(os.str(), image);

    return res;
}

int main(int argv, char** argc) {

    int res = SUCCESS;
    int video_stream_index = -1;
    int response = 0;
    int count_of_packets = 8;
    char* filename = nullptr;

    AVPacket* pPacket = nullptr;
    AVFrame* pFrame = nullptr;
    AVFormatContext* pFormatContext = nullptr;
    AVCodecContext* pCodecContext = nullptr;

    AVCodec* pCodec = nullptr;
    AVCodecParameters* pCodecParameters = nullptr;

    if (argv > 1) {
        filename = argc[1];
    } else {
        std::cout << "Usage: " << argc[0] << " <path_to_file>" << std::endl;

        return ERROR;
    }

    /* Allocate memory for context
     * AVFormatContext holds the header information from the format (Container)
     * */
    pFormatContext = avformat_alloc_context();
    if (pFormatContext == nullptr) {
        std::cout << "Failed with allocate memory for context" << std::endl;

        return ERROR;
    }

    /* Open an input stream and read the header. */
    res = avformat_open_input(&pFormatContext, filename, nullptr, nullptr);
    if (res != SUCCESS) {
        std::cout << "Failed with receiving format context of file" << std::endl;

        goto free_context;
    }

    std::cout << "Format: " << pFormatContext->iformat->name << " Duration: " << pFormatContext->duration << " us" << std::endl;

    /* Read packets of a media file to get stream information. */
    res = avformat_find_stream_info(pFormatContext, nullptr);
    if (res != SUCCESS) {
        std::cout << "Failed with find stream in file" << std::endl;

        goto close_input;
    }

    std::cout << "Count of Stream: " << pFormatContext->nb_streams << std::endl;

    for(int i = 0; i < pFormatContext->nb_streams; i++) {
        /* Receive Codec parameters */
        AVCodecParameters* pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        /* Receive decoder */
        AVCodec* pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        if (pLocalCodec == nullptr) {
            std::cout << "Unsupported codec" << std::endl;

            continue;
        }

        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {
                video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }

            std::cout << "Video Codec: resolution " << pLocalCodecParameters->width << " x " << pLocalCodecParameters->height << std::endl;
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            std::cout << "Audio Codec: " << pLocalCodecParameters->channels << " channels, sample rate " << pLocalCodecParameters->sample_rate << std::endl;
        }

        std::cout << "Codec " << pLocalCodec->name << " ID " << pLocalCodec->id << " bit_rate " << pLocalCodecParameters->bit_rate << std::endl;
    }

    if (video_stream_index == -1) {
        std::cout << "File " << filename << " does not contain a video stream!" << std::endl;
        res = ERROR;

        goto close_input;
    }

    /* Allocate an AVCodecContext and set its fields to default values. */
    pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext) {
        std::cout << "Failed to allocated memory for AVCodecContext" << std::endl;
        res = ERROR;

        goto close_input;
    }

    /* Fill the codec context based on the values from the supplied codec parameters. */
    res = avcodec_parameters_to_context(pCodecContext, pCodecParameters);
    if (res != SUCCESS) {
        std::cout << "Failed to fill codec context" << std::endl;

        goto free_codec_context;
    }

    /* Initialize the AVCodecContext to use the given AVCodec. */
    res = avcodec_open2(pCodecContext, pCodec, nullptr);
    if (res != SUCCESS) {
        std::cout << "Failed to initialize context to use the given codec" << std::endl;

        goto free_codec_context;
    }

    /* Allocate an AVPacket and set its fields to default values. */
    pPacket = av_packet_alloc();
    if (pPacket == nullptr) {
        std::cout << "Failed to allocate memory for packet" << std::endl;

        goto free_codec_context;
    }

    pFrame = av_frame_alloc();
    if (pFrame == nullptr) {
        std::cout << "Failed to allocate memory for frame" << std::endl;

        goto free_packet;
    }

    while(av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == video_stream_index) {

            res = decode_packet(pPacket, pCodecContext, pFrame);
            if (res != SUCCESS) {
                av_packet_unref(pPacket);
                break;
            }

            if (--count_of_packets <= 0) break;
        }

        av_packet_unref(pPacket);
    }

free_frame:
    av_frame_free(&pFrame);

free_packet:
    av_packet_free(&pPacket);

free_codec_context:
     avcodec_free_context(&pCodecContext);

close_input:
    avformat_close_input(&pFormatContext);

free_context:
    avformat_free_context(pFormatContext);

    return res;
}
