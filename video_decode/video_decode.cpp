/**
 * @file
 * video decoding with libavcodec API example
 *
 * @example video_decode.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C++编译时要添加 extern "C" */
extern "C" {
#include <libavcodec/avcodec.h>
}

// 文件流缓存大小
#define INBUF_SIZE 4096

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename) {
    FILE *f;
    int i;

    f = fopen(filename, "w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                   const char *filename) {
    char buf[1024];
    int ret;

    // 向解码器发送压缩包
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        /*
         * 当函数返回 0，表示解码成功，此时frame中存储了帧数据。
         * 当函数返回 AVERROR(EAGAIN)，表示解码器需要接收更多的压缩包才能进行下一帧解码。
         * 当函数返回 AVERROR_EOF，表示解码器缓冲区已经被清空，没有任何数据可以解码成帧。
         * 其余返回值都是解码异常。
         */
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to free it */
        snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}

int main(int argc, char **argv) {
    if (argc <= 3) {
        fprintf(stderr, "Usage: %s <input file> <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    const char *filename = argv[1];
    const char *outfilename = argv[2];
    const char *codec_name = argv[3];

    /* 根据名称查询解码器 */
    AVCodec *codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    // 初始化AVCodecParserContext
    AVCodecParserContext *parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }

    /*
     * 根据解码器创建解码上下文
     * 某些解码器（如：msmpeg4和mpeg4）必须初始化图像分辨率信息，
     * 因为它们的数据流中没有保存分辨率信息
     */
    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    // 初始化AVFrame，解码时，不需要配置分辨率，也不需要申请帧数据内存空间
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    // 初始化AVPacket
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    // 打开解码上下文
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    // 打开待解码视频的文件流
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    // 输入缓存，除了指定缓冲区大小，还要多加一个PADDING_SIZE的大小（av_parser_parse2()函数的输入数组需要腾出这个空间）
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    // 对 PADDING 部分的缓存值都置为0，避免数据干扰
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    // 作为inbuf数组上的游标指针使用
    uint8_t *data;
    // 用于记录fread()函数返回值
    size_t data_size;
    // 用于记录av_parser_parse2()函数返回值
    int ret;

    // 开始对视频进行解码
    while (!feof(f)) {
        // 从视频文件流中读取裸数据到缓存中
        data_size = fread(inbuf, 1, INBUF_SIZE, f);
        if (!data_size)
            break;

        data = inbuf;
        while (data_size > 0) {
            /*
             * 使用parser将缓存中的裸数据切分成若干压缩包。
             * pkt->size为0时，说明当前解码器缓冲区中的压缩编码数据不足以形成一个压缩包进行解码，还需要读更多的压缩编码数据。
             * 与编码过程相反，编码时需要足够多的帧填满缓冲区再压缩成压缩包，这里需要足够多的压缩编码数据形成一个压缩包。
             */
            ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            data += ret;
            data_size -= ret;

            /*
             * 若足够形成压缩包，则对压缩包中的压缩编码数据进行解码。
             * 把解码后的每帧数据输出到 "outfilename-{idx}" 文件中，idx 指帧位置下标。
             */
            if (pkt->size)
                decode(c, frame, pkt, outfilename);
        }
    }

    // 向解码器发送一个 NULL 压缩包，表示告知解码器要清空缓冲区，把还未解码的压缩包一并解码返回，然后发送EOS信号。
    decode(c, frame, NULL, outfilename);

    // 关闭视频文件流
    fclose(f);

    // 释放资源
    av_parser_close(parser);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
