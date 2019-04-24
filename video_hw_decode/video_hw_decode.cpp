/**
 * @file
 * HW-Accelerated decoding example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <stdio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
}

static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
static FILE *output_file = NULL;

// 初始化硬加速设备类型，并把硬加速上下文装配到编解码上下文中
static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type) {
    int err = 0;

    if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    return err;
}

//static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
//                                        const enum AVPixelFormat *pix_fmts) {
//    const enum AVPixelFormat *p;
//
//    for (p = pix_fmts; *p != -1; p++) {
//        if (*p == hw_pix_fmt)
//            return *p;
//    }
//
//    fprintf(stderr, "Failed to get HW surface format.\n");
//    return AV_PIX_FMT_NONE;
//}

// 把解码压缩包，并把帧数据写入到output_file文件中
static int decode_write(AVCodecContext *avctx, AVPacket *packet) {
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;

    // 向编解码上下文发送压缩包
    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (true) {
        // 创建帧
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        // 尝试从编解码上下文中获取解码帧
        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        // 如果返回的帧格式是硬件加速解码格式，则还需要到对应的硬件设备上取回数据
        if (frame->format == hw_pix_fmt) {
            // 从硬加速设备上把数据提取到CPU
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
        } else
            tmp_frame = frame;

        /*
         * 获取解码帧的原图尺寸。
         * 刚解码出来的帧数据尺寸可能并不是真正原图的尺寸，比如在编码过程中采用了YUV4:2:0的帧取样格式，
         * 这样编码所得到的数据帧尺寸与原图的尺寸是不同的，av_image_get_buffer_size() 函数是按照数据帧格式还原原图的尺寸。
         */
        size = av_image_get_buffer_size(static_cast<AVPixelFormat>(tmp_frame->format), tmp_frame->width,
                                        tmp_frame->height, 1);
        // 申请保存原图数据的内存空间
        buffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(size)));
        if (!buffer) {
            fprintf(stderr, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        // 根据数据帧取样格式还原原图的像素数据
        ret = av_image_copy_to_buffer(buffer, size,
                                      (const uint8_t *const *) tmp_frame->data,
                                      (const int *) tmp_frame->linesize, static_cast<AVPixelFormat>(tmp_frame->format),
                                      tmp_frame->width, tmp_frame->height, 1);
        if (ret < 0) {
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }
        // 把原图的像素数据写入到文件中
        if ((ret = fwrite(buffer, 1, size, output_file)) < 0) {
            fprintf(stderr, "Failed to dump raw data.\n");
            goto fail;
        }

        fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    }
}

int main(int argc, char *argv[]) {
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVPacket packet;
    enum AVHWDeviceType type;
    int i;

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device type> <input file> <output file>\n", argv[0]);
        return -1;
    }

    // 通过名称获取硬件加速设备类型
    type = av_hwdevice_find_type_by_name(argv[1]);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", argv[1]);
        fprintf(stderr, "Available device types:");
        // 遍历本地支持的所有硬件加速设备类型
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    // 打开视频文件，读取文件头部信息
    if (avformat_open_input(&input_ctx, argv[2], NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", argv[2]);
        return -1;
    }

    // 通过读取数据流的第一个压缩包识别视频流信息，对于某些没有头部的编码格式（如MPEG），用这个方法识别流信息非常好用
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    /*
     * 查询视频流在 input_ctx->streams 中的下标，并获取对应的解码器。
     * 一个视频文件中通常会有多个流同时存在，包括视频流、音频流、字幕等。
     * 这个方法就是在这么多的流中获取指定类型的流
     */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;

    /*
     * 获取硬加速配置。
     * 通过 avcodec_get_hw_config() 函数遍历传入一个个下标去遍历本地机器支持的硬加速类型，找到进程参数指定的硬加速类型。
     * 注意：若传入 avcodec_get_hw_config() 函数的下标值（第二个参数）已经大于本地机器支持的硬加速类型总数时，会返回NULL。
     */
    for (i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            // 记录硬件加速解码的帧格式。
            // 解码时，通过帧格式判断解码帧是否由硬件加速解码，若遇到硬加速的帧需要从硬件中获取解码数据。
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    // 通过解码器初始化解码上下文（AVCodecContext）
    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    // 通过流数据检测到的配置信息直接赋值给解码上下文
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    // 初始化硬加速设备类型，并把硬加速上下文装配到编解码上下文中
    if (hw_decoder_init(decoder_ctx, type) < 0)
        return -1;

    // 打开编解码上下文
    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }

    // 打开输出文件流用于保存解码数据
    output_file = fopen(argv[3], "w+");

    // 在这一步真正开始解码，并把解码后的数据存入输出文件中。
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream == packet.stream_index)
            ret = decode_write(decoder_ctx, &packet);

        av_packet_unref(&packet);
    }

    // 清空解码器
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet);
    av_packet_unref(&packet);

    if (output_file)
        fclose(output_file);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);
    av_buffer_unref(&hw_device_ctx);

    return 0;
}
