/**
 * @file
 * video encoding with libavcodec API example
 *
 * @example video_encode.cpp
 */

#include <stdio.h>
#include <stdlib.h>

/* C++编译时要添加 extern "C" */
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile) {
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %I64d\n", frame->pts);

    // 发送帧到编码器
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        /*
         * 当函数返回 0，表示编码成功，此时pkt中存储了压缩帧数据，过后通过fwrite把数据写入文件。
         * 当函数返回 AVERROR(EAGAIN)，表示编码器需要接收更多的输入帧以填满缓冲区为止，再对缓冲区进行压缩
         * 当函数返回 AVERROR_EOF，表示到达了数据流末尾，没有可编码的数据了
         */
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %I64d (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv) {
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *c = NULL;
    int i, ret, x, y;
    FILE *f;
    AVFrame *frame;
    AVPacket *pkt;
    uint8_t endcode[] = {0, 0, 1, 0xb7};

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    /* 根据名称查询编码器 */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    // 通过编码器创建编码上下文
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* 设置比特率 */
    c->bit_rate = 400000;
    /* 设置分辨率 */
    c->width = 352;
    c->height = 288;
    // 设置fps，time_base 是 framerate 的倒数
    c->time_base = (AVRational) {1, 25};
    c->framerate = (AVRational) {25, 1};
    // GOP大小
    c->gop_size = 10;
    // 两个非B帧之间的B帧最大数目（设为0表示不会有B帧）
    c->max_b_frames = 0;
    // 帧采样格式
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    // H264编码时还可以调节编码速度从而调整压缩质量，这里把编码速度设置为slow
    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    // 打开编码上下文
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    // 打开文件流
    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    // 创建维护缓冲区数据的AVPacket对象
    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    // 创建维护帧元数据的AVFrame对象
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    // 设置帧采样格式
    frame->format = c->pix_fmt;
    // 设置帧的分辨率
    frame->width = c->width;
    frame->height = c->height;

    // 根据AVFrame的设置分配对应大小的缓存空间
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    /* 编码一秒钟的视频(25fps) */
    for (i = 0; i < 25; i++) {
        /* prepare a dummy image */
        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height / 2; y++) {
            for (x = 0; x < c->width / 2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        // 帧位置，该帧的播放时间为：pts * time_base
        frame->pts = i;

        // 编码本帧图片
        encode(c, frame, pkt, f);
    }

    // 最后一帧设为NULL，表示到达流末端。这个操作会让编码上下文把剩余的缓存数据编码到文件中。
    encode(c, NULL, pkt, f);

    // 按照MPEG标准，需要在文件末尾添加结束序列码
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    // 释放编码上下文
    avcodec_free_context(&c);
    // 释放帧对象
    av_frame_free(&frame);
    // 释放包
    av_packet_free(&pkt);

    return 0;
}
