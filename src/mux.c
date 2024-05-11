/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file libavformat muxing API usage example
 * @example mux.c
 *
 * Generate a synthetic audio and video signal and mux them to a media file in
 * any supported libavformat format. The default codecs are used.
 */
#include <mux.h>
#include <glad/glad.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define STREAM_FRAME_RATE 62.5 
#define STREAM_SAMPLE_RATE 64000
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

#define BITRATE 80000000

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    AVPacket *tmp_pkt;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static OutputStream video_st = { 0 }, audio_st = { 0 };
static const AVOutputFormat *fmt;
static AVFormatContext *oc;
static const AVCodec *audio_codec, *video_codec;
static int have_video = 0, have_audio = 0, encode_video = 0, encode_audio = 0, width_ = 0, height_ = 0;
static AVDictionary *opt = NULL;
static uint8_t* fliparr;
static float** audiobuf;

/*static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}*/

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c,
                       AVStream *st, AVFrame *frame, AVPacket *pkt)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        return 1;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            return 1;
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;

        /* Write the compressed frame to the media file. */
        //log_packet(fmt_ctx, pkt);
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            return 1;
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       const AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        return;
    }

    ost->tmp_pkt = av_packet_alloc();
    if (!ost->tmp_pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        return;
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        return;
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        return;
    }
    ost->enc = c;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 64000;
        c->sample_rate = STREAM_SAMPLE_RATE;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == STREAM_SAMPLE_RATE)
                    c->sample_rate = STREAM_SAMPLE_RATE;
            }
        }
        av_channel_layout_copy(&c->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        c->bit_rate = BITRATE;
        /* Resolution must be a multiple of two. */
        c->width    = width_;
        c->height   = height_;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 2, 125 };
        c->time_base       = ost->st->time_base;

        c->gop_size      = 30; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = STREAM_PIX_FMT;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
        break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  const AVChannelLayout *channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        return NULL;
    }

    frame->format = sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, channel_layout);
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            return NULL;
        }
    }

    return frame;
}

static void open_audio(AVFormatContext *oc, const AVCodec *codec,
                       OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    c = ost->enc;
	
    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        return;
    }

    /* init signal generator */
    ost->t     = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, &c->ch_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout,
                                       c->sample_rate, nb_samples);
	
    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        return;
    }

    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        return;
    }

    /* set options */
    av_opt_set_chlayout  (ost->swr_ctx, "in_chlayout",       &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_chlayout  (ost->swr_ctx, "out_chlayout",      &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        return;
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    AVFrame *frame = ost->tmp_frame;

	SwrContext *swr_ctx = swr_alloc();
	
	// Set the input audio parameters
	av_opt_set_int(swr_ctx, "in_channel_layout", AV_CH_LAYOUT_MONO, 0);
	av_opt_set_int(swr_ctx, "in_sample_rate", 30000, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

	// Set the output audio parameters
	av_opt_set_int(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
	av_opt_set_int(swr_ctx, "out_sample_rate", STREAM_SAMPLE_RATE, 0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	
	// Create resampler context
	swr_init(swr_ctx);
	
	// Resample the input audio data into the output buffer
	swr_convert(swr_ctx,frame->data,frame->nb_samples,(const uint8_t **)audiobuf,(int)(1/STREAM_FRAME_RATE*30000));
	
	// Free the resampler context
	swr_free(&swr_ctx);

    frame->pts = ost->next_pts;
    ost->next_pts  += frame->nb_samples;

    return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    AVFrame *frame;
    int ret;
    int dst_nb_samples;

    c = ost->enc;

    frame = get_audio_frame(ost);

    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
        /* compute destination number of samples */
        dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
                                        c->sample_rate, c->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            return 1;

        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx,
                          ost->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            return 1;
        }
        frame = ost->frame;

        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, c->sample_rate}, c->time_base);
        ost->samples_count += dst_nb_samples;
    }

    return write_frame(oc, c, ost->st, frame, ost->tmp_pkt);
}

/**************************************************************/
/* video output */

static AVFrame *alloc_frame(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *frame;
    int ret;

    frame = av_frame_alloc();
    if (!frame)
        return NULL;

    frame->format = pix_fmt;
    frame->width  = width;
    frame->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        return NULL;
    }

    return frame;
}

static void open_video(AVFormatContext *oc, const AVCodec *codec,
                       OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        return;
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_frame(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        return;
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_frame(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary video frame\n");
            return;
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        return;
    }
}

static void fill_yuv_image(AVFrame *pict, int frame_index,
                           int width, int height)
{
	struct SwsContext* ctx = sws_getContext(width, height,
								     AV_PIX_FMT_RGB24,
								     width, height,
								     AV_PIX_FMT_YUV420P,
								     0, 0, 0, 0);
	
	if (!ctx) {
		fprintf(stderr,
				"Could not initialize the conversion context\n");
		return;
	}
	uint8_t* data = malloc(sizeof(uint8_t)*width*height*3);
	uint8_t* indata[1] = {fliparr};
	int linesize[1] = { width*3 };
	//printf("%d %d\n",width,height);
	
	glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,data);
	
	for(int i = 0; i < height; i++)
		memcpy(fliparr+width*3*i,data+width*3*(height-i-1),sizeof(uint8_t)*width*3);
	
	sws_scale(ctx,(const uint8_t * const *)indata,linesize,0,height,pict->data,pict->linesize);
	
	sws_freeContext(ctx);
	free(data);
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(ost->frame) < 0)
        return NULL;

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                return NULL;
            }
        }
        fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
        sws_scale(ost->sws_ctx, (const uint8_t * const *) ost->tmp_frame->data,
                  ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
                  ost->frame->linesize);
    } else {
        fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
    }

    ost->frame->pts = ost->next_pts++;

    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    return write_frame(oc, ost->enc, ost->st, get_video_frame(ost), ost->tmp_pkt);
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    av_packet_free(&ost->tmp_pkt);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

struct audio_data* load_mp3(char* filename)
{
	struct audio_data* ad = NULL;
	AVFormatContext *formatContext = NULL;
	if (avformat_open_input(&formatContext,filename,NULL,NULL) != 0) {
		printf("Unable to open audio file: %s\n",filename);
		return NULL;
	}
	if (avformat_find_stream_info(formatContext, NULL) < 0) {
		printf("Unable to fine stream info\n");
		return NULL;
	}
	
	int audioStreamIndex = -1;
	for (int i = 0; i < formatContext->nb_streams; i++) {
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			break;
		}
	}
	if (audioStreamIndex == -1) {
		printf("No audio stream found\n");
		return NULL;	
	}
	
	AVCodec *audioCodec = avcodec_find_decoder(formatContext->streams[audioStreamIndex]->codecpar->codec_id);
	if (audioCodec == NULL) {
		printf("No decoder found\n");
		return NULL;
	}
	AVCodecContext *codecContext = avcodec_alloc_context3(audioCodec);
	if (codecContext == NULL) {
		printf("Couldn't allocate context\n");
		return NULL;
	}
	if (avcodec_parameters_to_context(codecContext, formatContext->streams[audioStreamIndex]->codecpar) < 0) {
		printf("avcodec_parameters_to_context fail\n");
		return NULL;
	}
	if (avcodec_open2(codecContext, audioCodec, NULL) < 0) {
		printf("Couldn't open context\n");
		return NULL;
	}
		
	AVPacket packet;
	AVFrame *frame = av_frame_alloc();
	if (frame == NULL) {
		printf("Couldn't allocate frame\n");
		return NULL;
	}
	
	SwrContext *swr_ctx = swr_alloc();
	if (swr_ctx == NULL) {
		printf("Couldn't allocate conversion context\n");
		return NULL;
	}
	
	av_opt_set_int(swr_ctx, "out_channel_layout",AV_CH_LAYOUT_MONO,0);
    av_opt_set_int(swr_ctx,"out_sample_rate",30000,0);
    av_opt_set_sample_fmt(swr_ctx,"out_sample_fmt",AV_SAMPLE_FMT_FLT,0);
	
	av_opt_set_chlayout(swr_ctx,"in_chlayout",&codecContext->ch_layout,0);
    av_opt_set_int(swr_ctx,"in_sample_rate",codecContext->sample_rate,0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt",codecContext->sample_fmt, 0);
	
	if(swr_init(swr_ctx) < 0) {
		printf("Couldn't initialize conversion context\n");
		return NULL;
	}

	int64_t total_samples = 1024, count = 0;
	ad = malloc(sizeof(struct audio_data));
	ad->data = (float*)malloc(sizeof(float)*total_samples);
	while (av_read_frame(formatContext, &packet) >= 0) {
		if (packet.stream_index == audioStreamIndex) {
			if (avcodec_send_packet(codecContext, &packet) < 0) {
				free(ad->data);
				free(ad);
				printf("Couldn't send packet\n");
				return NULL;
			}

			while (avcodec_receive_frame(codecContext, frame) == 0) {
				//Convert audio samples to float
				swr_convert_frame(swr_ctx, frame, NULL);
				
				// Process audio frame samples
				for (int i = 0; i < frame->nb_samples; i++) {
					// Access float samples
					if(count >= total_samples)
					{
						total_samples += 1024;
						ad->data = (float*)realloc(ad->data,sizeof(float)*total_samples);
					}
					ad->data[count++] = ((float **)frame->data)[0][i];
					ad->samples = count;
				}
			}
		}
		av_packet_unref(&packet);
	}
	
	swr_free(&swr_ctx);
	av_frame_free(&frame);
	
	avcodec_close(codecContext);
	avformat_close_input(&formatContext);
	avformat_free_context(formatContext);
	avcodec_free_context(&codecContext);
	
	return ad;
}

/**************************************************************/
/* media file output */
int init_record(char* filename, int w, int h, float** audio)
{
    int ret;
	width_ = w;
	height_ = h;
	audiobuf = audio;
	
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc)
        return -1;

    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec,  AV_CODEC_ID_H264);
        have_video = 1;
        encode_video = 1;
    }
	
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }
	
    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, video_codec, &video_st, opt);

    if (have_audio)
        open_audio(oc, audio_codec, &audio_st, opt);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return -1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return -1;
    }
	
	fliparr = (uint8_t*)malloc(sizeof(uint8_t)*width_*height_*3);
	
	return 0;
}

int add_frame()
{
	write_video_frame(oc, &video_st);
	write_audio_frame(oc, &audio_st);
	/*if (encode_video || encode_audio) {
        if (encode_video &&
            (!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
                                            audio_st.next_pts, audio_st.enc->time_base) <= 0)) {
            encode_video = !write_video_frame(oc, &video_st);
        } else {
            encode_audio = !write_audio_frame(oc, &audio_st);
        }
    }*/
	
	return 0;
}

void stop_record()
{
	av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);
	free(fliparr);
}







