/**
 *  Support for MOV and mp4 files in vooya
 *  Copyright (c) 2018  Arion Neddens
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include <assert.h>
#include <ctype.h>

#ifdef _WIN32
	#include <Windows.h>
	#define dlopen(l,x) LoadLibrary(L##l)
	#define RTLD_NOW 0
	#define RTLD_LOCAL
	#define SHARED_LIB_EXT ".dll"
	#define dlsym GetProcAddress
	int dlclose(void *p_handle) {
		return !FreeLibrary((HMODULE)p_handle);
	}
	#define AVCODEC_VER "-57"
	#define AVUTIL_VER "-55"
	#define AVFORMAT_VER "-57"
#else
	#define SHARED_LIB_EXT ".so"
	#define AVCODEC_VER ""
	#define AVUTIL_VER ""
	#define AVFORMAT_VER ""
#endif


#include "voo_plugin.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>


void message( void *_, const char *what ){
	fprintf(stderr, "%s", what);
}
void av_mute_log_callback( void *avclass, int level, const char *format, va_list args ){/* mute */}

typedef void(*pav_log_set_callback_t)(void(*callback)(void*, int, const char*, va_list));
typedef void(*pav_register_all_t)(void);
typedef void(*pavcodec_register_all_t)(void);
typedef void(*pav_init_packet_t)(AVPacket *pkt);
typedef AVFrame *(*pav_frame_alloc_t)(void);
typedef AVFormatContext *(*pavformat_alloc_context_t)(void);
typedef int(*pavformat_open_input_t)(AVFormatContext **ps, const char *url, AVInputFormat *fmt, AVDictionary **options);
typedef int(*pav_strerror_t)(int errnum, char *errbuf, size_t errbuf_size);
typedef int(*pav_find_best_stream_t)(AVFormatContext *ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, AVCodec **decoder_ret, int flags);
typedef void(*pavcodec_free_context_t)(AVCodecContext **avctx);
typedef void(*pav_frame_free_t)(AVFrame **frame);
typedef void(*pavformat_close_input_t)(AVFormatContext **s);
typedef AVCodecContext *(*pavcodec_alloc_context3_t)(const AVCodec *codec);
typedef int(*pavcodec_parameters_to_context_t)(AVCodecContext *codec, const AVCodecParameters *par);
typedef void (*pavformat_free_context_t)(AVFormatContext *s);
typedef int (*pav_seek_frame_t)(AVFormatContext *s, int stream_index, int64_t timestamp, int flags);
typedef int64_t (*pav_rescale_q_t)(int64_t a, AVRational bq, AVRational cq) av_const;
typedef int (*pavcodec_receive_frame_t)(AVCodecContext *avctx, AVFrame *frame);
typedef int (*pavcodec_send_packet_t)(AVCodecContext *avctx, const AVPacket *avpkt);
typedef int (*pav_read_frame_t)(AVFormatContext *s, AVPacket *pkt);
typedef void (*pavcodec_flush_buffers_t)(AVCodecContext *avctx);
typedef int(*pavcodec_open2_t)(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);
typedef AVCodec *(*pavcodec_find_decoder_t)(enum AVCodecID id);
typedef int(*pavcodec_version_t)(void);

typedef struct {
	pav_log_set_callback_t av_log_set_callback;
	pav_register_all_t av_register_all;
	pavcodec_register_all_t avcodec_register_all;
	pav_init_packet_t av_init_packet;
	pav_frame_alloc_t av_frame_alloc;
	pavformat_alloc_context_t avformat_alloc_context;
	pavformat_open_input_t avformat_open_input;
	pav_strerror_t av_strerror;
	pav_find_best_stream_t av_find_best_stream;
	pavcodec_free_context_t avcodec_free_context;
	pav_frame_free_t av_frame_free;
	pavformat_close_input_t avformat_close_input;
	pavcodec_alloc_context3_t avcodec_alloc_context3;
	pavcodec_parameters_to_context_t avcodec_parameters_to_context;
	pavformat_free_context_t avformat_free_context;
	pav_seek_frame_t av_seek_frame;
	pav_rescale_q_t av_rescale_q;
	pavcodec_receive_frame_t avcodec_receive_frame;
	pavcodec_send_packet_t avcodec_send_packet;
	pav_read_frame_t av_read_frame;
	pavcodec_flush_buffers_t avcodec_flush_buffers;
	pavcodec_open2_t avcodec_open2;
	pavcodec_find_decoder_t avcodec_find_decoder;
	pavcodec_version_t avcodec_version;
} av_api_t;

typedef struct  
{
	voo_sequence_t properties;

	AVPacket avpkt;
	AVFormatContext *format_ctx;
	AVCodecContext *codec_ctx;
	AVStream *stream;
	AVFrame *picture;
	AVCodec *codec;

	int64_t expected_seek_tgt;
	BOOL b_eof;
#define ERRBUFF_LEN 2048
	char last_err[ERRBUFF_LEN];
	
	void *p_msg_cargo;
	void (*message)(void *,const char*);

	av_api_t api;
	void *h1, *h2, *h3;

} ffmpeg_reader_t;


BOOL loadLibaries(ffmpeg_reader_t *ctx) {
	ctx->h1 = dlopen("avcodec" AVCODEC_VER SHARED_LIB_EXT, RTLD_NOW | RTLD_LOCAL);
	if (ctx->h1) {
		ctx->api.avcodec_register_all = (pavcodec_register_all_t)dlsym(ctx->h1, "avcodec_register_all");
		ctx->api.av_init_packet = (pav_init_packet_t)dlsym(ctx->h1, "av_init_packet");
		ctx->api.avcodec_free_context = (pavcodec_free_context_t)dlsym(ctx->h1, "avcodec_free_context");
		ctx->api.avcodec_alloc_context3 = (pavcodec_alloc_context3_t)dlsym(ctx->h1, "avcodec_alloc_context3");
		ctx->api.avcodec_parameters_to_context = (pavcodec_parameters_to_context_t)dlsym(ctx->h1, "avcodec_parameters_to_context");
		ctx->api.avcodec_send_packet = (pavcodec_send_packet_t)dlsym(ctx->h1, "avcodec_send_packet");
		ctx->api.avcodec_flush_buffers = (pavcodec_flush_buffers_t)dlsym(ctx->h1, "avcodec_flush_buffers");
		ctx->api.avcodec_open2 = (pavcodec_open2_t)dlsym(ctx->h1, "avcodec_open2");
		ctx->api.avcodec_find_decoder = (pavcodec_find_decoder_t)dlsym(ctx->h1, "avcodec_find_decoder");
		ctx->api.avcodec_receive_frame = (pavcodec_receive_frame_t)dlsym(ctx->h1, "avcodec_receive_frame");
		ctx->api.avcodec_version = (pavcodec_version_t)dlsym(ctx->h1, "avcodec_version");
		ctx->h2 = dlopen("avutil" AVUTIL_VER SHARED_LIB_EXT, RTLD_NOW | RTLD_LOCAL);
		if (ctx->h2) {
			ctx->api.av_log_set_callback = (pav_log_set_callback_t)dlsym(ctx->h2, "av_log_set_callback");
			ctx->api.av_frame_alloc = (pav_frame_alloc_t)dlsym(ctx->h2, "av_frame_alloc");
			ctx->api.av_strerror = (pav_strerror_t)dlsym(ctx->h2, "av_strerror");
			ctx->api.av_frame_free = (pav_frame_free_t)dlsym(ctx->h2, "av_frame_free");
			ctx->api.av_rescale_q = (pav_rescale_q_t)dlsym(ctx->h2, "av_rescale_q");
			ctx->h3 = dlopen("avformat" AVFORMAT_VER SHARED_LIB_EXT, RTLD_NOW | RTLD_LOCAL);
			if (ctx->h3) {
				ctx->api.av_register_all = (pav_register_all_t)dlsym(ctx->h3, "av_register_all");
				ctx->api.avformat_alloc_context = (pavformat_alloc_context_t)dlsym(ctx->h3, "avformat_alloc_context");
				ctx->api.avformat_open_input = (pavformat_open_input_t)dlsym(ctx->h3, "avformat_open_input");
				ctx->api.av_find_best_stream = (pav_find_best_stream_t)dlsym(ctx->h3, "av_find_best_stream");
				ctx->api.avformat_close_input = (pavformat_close_input_t)dlsym(ctx->h3, "avformat_close_input");
				ctx->api.avformat_free_context = (pavformat_free_context_t)dlsym(ctx->h3, "avformat_free_context");
				ctx->api.av_seek_frame = (pav_seek_frame_t)dlsym(ctx->h3, "av_seek_frame");
				ctx->api.av_read_frame = (pav_read_frame_t)dlsym(ctx->h3, "av_read_frame");
				return TRUE;
			}
		}
	}
	return FALSE;
}



VP_API BOOL in_open( const vooChar_t *filename, voo_app_info_t *p_app_info, void **pp_user ){
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)malloc(sizeof(ffmpeg_reader_t));
	memset( p_reader, 0x0, sizeof(ffmpeg_reader_t) );
	if (!loadLibaries(p_reader)) {
		return FALSE;
	}

	*pp_user = p_reader;
	
	av_api_t api = p_reader->api;

	int32_t version = api.avcodec_version(); // ((a) << 16 | (b) << 8 | (c))	
	int32_t maj = version >> 16;
	int32_t min = (version >> 8) & 0xFF;
	int32_t rel = version & 0xFF;
	if (maj < 50) {
		return FALSE;
	}
	
	#ifdef WIN32
	char c_filename[ 256 ];
	sprintf( c_filename, "%ws", filename );
	#else
	const vooChar_t *c_filename = filename;
	#endif

	api.av_log_set_callback( av_mute_log_callback );

	p_reader->message = message;
	if( p_app_info->pf_console_message ){
		p_reader->p_msg_cargo = p_app_info->p_message_cargo;
		p_reader->message = p_app_info->pf_console_message;
	}

	if( !strcmp(c_filename,"-") ){
		sprintf( p_reader->last_err, "stdin is not supported by the Quicktime Movie/Mp4 Input Plugin." );
		p_reader->message( p_reader->p_msg_cargo, p_reader->last_err );
		return FALSE;
	}

	api.av_register_all();
	api.avcodec_register_all();

	api.av_init_packet( &p_reader->avpkt );
	p_reader->picture = api.av_frame_alloc();
	p_reader->format_ctx = api.avformat_alloc_context();

	int ret;
	
	AVInputFormat *pFmt = NULL;
	ret = api.avformat_open_input( &p_reader->format_ctx, c_filename, pFmt, NULL );

	if( p_reader->format_ctx == NULL )
	{
		api.av_strerror( ret, p_reader->last_err, ERRBUFF_LEN );
		return FALSE;
	}

	int video_stream_index = api.av_find_best_stream( p_reader->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &p_reader->codec, 0x0 );
	if( video_stream_index == AVERROR_STREAM_NOT_FOUND )
	{
		sprintf( p_reader->last_err, "No Video Stream found" );
		p_reader->message( p_reader->p_msg_cargo, p_reader->last_err );
		api.avcodec_free_context( &p_reader->codec_ctx );
		return FALSE;
	}
	if( video_stream_index == AVERROR_DECODER_NOT_FOUND )
	{
		sprintf( p_reader->last_err, "No Decoder found" );
		p_reader->message( p_reader->p_msg_cargo, p_reader->last_err );
		api.avcodec_free_context( &p_reader->codec_ctx );
		return FALSE;
	}

	p_reader->stream = p_reader->format_ctx->streams[ video_stream_index ];

	if( p_reader->stream->codecpar->codec_id != AV_CODEC_ID_PRORES
	 && p_reader->stream->codecpar->codec_id != AV_CODEC_ID_H264
	 && p_reader->stream->codecpar->codec_id != AV_CODEC_ID_HEVC
	 && p_reader->stream->codecpar->codec_id != AV_CODEC_ID_V210
	 && p_reader->stream->codecpar->codec_id != AV_CODEC_ID_MJPEG )
	{
		api.av_frame_free( &p_reader->picture );
		api.avformat_close_input( &p_reader->format_ctx );
		api.avformat_free_context( p_reader->format_ctx );
		sprintf( p_reader->last_err, "This is not a supported file." );
		p_reader->message( p_reader->p_msg_cargo, p_reader->last_err );
		return FALSE;
	}

	if( !p_reader->codec )
		p_reader->codec = api.avcodec_find_decoder( p_reader->stream->codecpar->codec_id );

	if( !p_reader->codec )
	{
		sprintf( p_reader->last_err, "Cannot find a decoder." );
		p_reader->message( p_reader->p_msg_cargo, p_reader->last_err );
		api.av_frame_free( &p_reader->picture );
		api.avformat_close_input( &p_reader->format_ctx );
		api. avformat_free_context( p_reader->format_ctx );
		return FALSE;
	}

	p_reader->codec_ctx = api.avcodec_alloc_context3( p_reader->codec );
	api.avcodec_parameters_to_context( p_reader->codec_ctx, p_reader->stream->codecpar );
	if( p_reader->codec->capabilities & CODEC_CAP_TRUNCATED )
		p_reader->codec_ctx->flags |= CODEC_FLAG_TRUNCATED; /* We may send incomplete frames */
	if( p_reader->codec->capabilities & CODEC_FLAG2_CHUNKS )
		p_reader->codec_ctx->flags |= CODEC_FLAG2_CHUNKS;
	ret = api.avcodec_open2( p_reader->codec_ctx, p_reader->codec, NULL );
	

	if( ret != 0 ) {
		api.av_frame_free( &p_reader->picture );
		api.avformat_close_input( &p_reader->format_ctx );
		api.avcodec_free_context( &p_reader->codec_ctx );
		api.av_strerror( ret, p_reader->last_err, ERRBUFF_LEN );
		return FALSE;
	}

	memset( &p_reader->properties, 0, sizeof(voo_sequence_t) );

	p_reader->properties.bits_per_channel = p_reader->stream->codecpar->bits_per_raw_sample;
	if( p_reader->properties.bits_per_channel == 0 )
		p_reader->properties.bits_per_channel = p_reader->stream->codecpar->bits_per_coded_sample / 3;
	
	switch( p_reader->codec_ctx->pix_fmt ){
	case AV_PIX_FMT_YUV420P10LE:
		p_reader->properties.arrangement = vooDA_planar_420;
		p_reader->properties.bits_per_channel = 10;
		break;
	case AV_PIX_FMT_YUV422P10LE:
		p_reader->properties.arrangement = vooDA_planar_422;
		p_reader->properties.bits_per_channel = 10;
		break;
	default: break;
	}

	if( p_reader->stream->codecpar->codec_id == AV_CODEC_ID_PRORES )
	{
		if( p_reader->stream->codecpar->codec_tag == 'h4pa' ){
			p_reader->properties.arrangement = vooDA_planar_444;
			if( 10 >= p_reader->properties.bits_per_channel )
				p_reader->properties.bits_per_channel = 10;
		} else {
			p_reader->properties.arrangement = vooDA_planar_422;
			if( 10 >= p_reader->properties.bits_per_channel )
				p_reader->properties.bits_per_channel = 10;
		}
	}
	else if( p_reader->stream->codecpar->codec_id == AV_CODEC_ID_V210 )
	{
		p_reader->properties.arrangement = vooDA_planar_422;
		p_reader->properties.bits_per_channel = 10;
	}
	else {
		if( p_reader->properties.arrangement == vooDataArrangement_Unknown )
			p_reader->properties.arrangement = vooDA_planar_420;
		if( 0 >= p_reader->properties.bits_per_channel )
			p_reader->properties.bits_per_channel = 8;
	}
	p_reader->properties.color_space = vooCS_YUV;
	p_reader->properties.channel_order = vooCO_c123;
	p_reader->properties.fps = (double)p_reader->stream->avg_frame_rate.num / p_reader->stream->avg_frame_rate.den;
	p_reader->properties.width = p_reader->stream->codecpar->width;
	p_reader->properties.height = p_reader->stream->codecpar->height;
	
	p_reader->expected_seek_tgt = -1;

	return TRUE;
}

VP_API void in_close( void *p_user ){
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	av_api_t api = p_reader->api;
	api.avformat_close_input(&p_reader->format_ctx);
	api.av_frame_free( &p_reader->picture );
	api.avcodec_free_context( &p_reader->codec_ctx );
	api.avformat_free_context( p_reader->format_ctx );
	dlclose(p_reader->h1);
	dlclose(p_reader->h2);
	dlclose(p_reader->h3);
}

VP_API BOOL in_get_properties( voo_sequence_t *p_info, void *p_user ){
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	*p_info = p_reader->properties;
	return TRUE;
}

VP_API unsigned int in_framecount( void *p_user ){
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	return ( unsigned int)p_reader->stream->nb_frames;
}

VP_API BOOL in_seek( unsigned int frame, void *p_user )
{
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	av_api_t api = p_reader->api;

	int64_t seek_target;
	seek_target = (int64_t)(((double)frame/p_reader->properties.fps) * AV_TIME_BASE);
	seek_target = api.av_rescale_q(seek_target, AV_TIME_BASE_Q, p_reader->format_ctx->streams[p_reader->stream->index]->time_base);
	p_reader->expected_seek_tgt = seek_target;
	if( 0 <= api.av_seek_frame( p_reader->format_ctx, p_reader->stream->index, seek_target, AVSEEK_FLAG_FRAME/*|AVSEEK_FLAG_ANY*/ ) ){
		api.avcodec_flush_buffers( p_reader->codec_ctx );
		p_reader->b_eof = FALSE;
		return TRUE;
	}

	return FALSE;
}

VP_API BOOL in_load( unsigned int frame, char *p_buffer, BOOL *pb_skipped, void **pp_frame_user, void *p_user )
{
	int32_t i_ret;
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	av_api_t api = p_reader->api;
	try_again:
	if( (i_ret = api.av_read_frame( p_reader->format_ctx, &p_reader->avpkt )) >= 0 ){

		i_ret = api.avcodec_send_packet( p_reader->codec_ctx, &p_reader->avpkt );
		if( AVERROR( EAGAIN ) == i_ret )
			goto receive;
		if( AVERROR_EOF == i_ret ){
			p_reader->b_eof = TRUE;
			return FALSE;
		}
		if( i_ret < 0 ) {
			api.av_strerror( i_ret, p_reader->last_err, ERRBUFF_LEN );
			goto try_again;
		}
		
		receive:
		i_ret = api. avcodec_receive_frame( p_reader->codec_ctx, p_reader->picture );
		if( AVERROR( EAGAIN ) == i_ret ) goto try_again;
		if( AVERROR_EOF == i_ret ){
			p_reader->b_eof = TRUE;
			return FALSE;
		}
		if( i_ret < 0 ) {
			api.av_strerror( i_ret, p_reader->last_err, ERRBUFF_LEN );
			return FALSE;
		}
		
		if( i_ret == 0 )
		{
			if( p_reader->properties.arrangement == vooDA_v210 ){

				int32_t pel_width = ( p_reader->properties.bits_per_channel + 7 ) >> 3;

				memcpy( p_buffer,
					p_reader->picture->data[ 0 ],
					pel_width * p_reader->properties.width*p_reader->properties.height );

			} else {
				int32_t chr_sh_x = p_reader->properties.arrangement == vooDA_planar_444 ? 0 : 1;
				int32_t chr_sh_y = p_reader->properties.arrangement == vooDA_planar_420 ? 1 : 0;
				int32_t pel_width = ( p_reader->properties.bits_per_channel + 7 ) >> 3;

				memcpy( p_buffer,
					p_reader->picture->data[ 0 ],
					pel_width * p_reader->properties.width*p_reader->properties.height );
				memcpy( p_buffer + p_reader->properties.width*p_reader->properties.height*pel_width,
					p_reader->picture->data[ 1 ],
					pel_width * ( p_reader->properties.width >> chr_sh_x )*( p_reader->properties.height >> chr_sh_y ) );
				memcpy( p_buffer + p_reader->properties.width*p_reader->properties.height*pel_width + ( p_reader->properties.width >> chr_sh_x )*( p_reader->properties.height >> chr_sh_y )*pel_width,
					p_reader->picture->data[ 2 ],
					pel_width * ( p_reader->properties.width >> chr_sh_x )*( p_reader->properties.height >> chr_sh_y ) );
			}
		}
			
	} else {
	
		p_reader->b_eof = TRUE;
		return FALSE;
	}

	return TRUE;
}

VP_API BOOL in_eof( void *p_user ){
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user;
	return p_reader->b_eof;
}

VP_API BOOL in_good( void *p_user ){ return TRUE; }

VP_API BOOL in_reload( void *p_user ){ return TRUE; }

VP_API BOOL get_meta( int idx, char *buffer_k, char *buffer_v, void *p_user_seq )
{
	int32_t _idx = 0;
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user_seq;

	if( idx == _idx++ ){
		char *codec = 0x0;
		uint32_t fourcc = p_reader->stream->codecpar->codec_tag;
		if( fourcc == 'ncpa' )
			codec = "Apple ProRes 422 SD";
		else if( fourcc == 'hcpa' )
			codec = "Apple ProRes 422 HQ";
		else if( fourcc == 'scpa' )
			codec = "Apple ProRes 422 LT";
		else if( fourcc == 'ocpa' )
			codec = "Apple ProRes 422 Proxy";
		else if( fourcc == 'h4pa' )
			codec = "Apple ProRes 4444";
		else if( fourcc == '1cva' )
			codec = "H.264";
		else if( fourcc == '1cvh' )
			codec = "HEVC";
		else if( fourcc == '012v' )
			codec = "v210 Raw";
		else if( fourcc == 'gepj' )
			codec = "MJPEG";
		if( codec ){
			sprintf( buffer_k, "Codec" );
			sprintf( buffer_v, "%s", codec );
		}
	} else if( p_reader->stream->codecpar->bit_rate && idx == _idx++ ) {
		sprintf( buffer_k, "Bitrate" );
		const char *unit = "";
		float bps = (float)p_reader->stream->codecpar->bit_rate;
		if( p_reader->stream->codecpar->bit_rate > 999999 ) {
			unit = "M";
			bps /= 1e6f;
		} else if( p_reader->stream->codecpar->bit_rate > 999 ) {
			unit = "k";
			bps /= 1e3f;
		}
		sprintf( buffer_v, "%1.2f%sb/s", bps, unit );
	}
	else return FALSE;
	return TRUE;
}

VP_API void in_error( const char **pp_err, void *p_user_seq )
{
	ffmpeg_reader_t *p_reader = (ffmpeg_reader_t *)p_user_seq;
	*pp_err = p_reader->last_err;
}




VP_API BOOL in_responsible( const vooChar_t *_filename, char *sixteen_bytes, void *p_user ){
	if( voo_strlen(_filename) < 4 )
		return FALSE;

	vooChar_t filename[1024];
	voo_strcpy( filename,_filename );
	vooChar_t *p = filename;
	for( ; *p++; ) *p = tolower( *p );

	return !voo_strcmp( filename + voo_strlen( filename ) - 4, _v( ".mov" ) )
		|| !voo_strcmp( filename + voo_strlen( filename ) - 4, _v( ".mp4" ) );
}

VP_API BOOL in_file_suffixes( int idx, char const **pp_suffix, void *p_user ){
	switch(idx){
	case 0: *pp_suffix = "mov"; break;
	case 1: *pp_suffix = "mp4"; break;
	default: return FALSE;
	}
	return TRUE;
}




VP_API void voo_describe( voo_plugin_t *p_plugin )
{
	p_plugin->voo_version = VOO_PLUGIN_API_VERSION;

	// plugin main properties
	#ifdef _DEBUG
	#define  DEBUG_STR " DEBUG"
	#else
	#define  DEBUG_STR ""
	#endif
	p_plugin->name = "Quicktime Movie / MP4 Input" DEBUG_STR;
	p_plugin->description = "Brings support for Quicktime Movie and MPEG-4 input. This software uses libraries from the FFmpeg project under the LGPLv2.1.";
	p_plugin->copyright = "A. Neddens (c) 2018, LGPL";
	p_plugin->version = "ver1.2";

	p_plugin->input.uid = "voo.mov.0";
	p_plugin->input.name = "Quicktime/MPEG-4 Movie Support";
	p_plugin->input.description = "Quicktime/MPEG-4 Movie";
	p_plugin->input.file_suffixes = in_file_suffixes;
	p_plugin->input.responsible = in_responsible;
	p_plugin->input.open = in_open;
	p_plugin->input.close = in_close;
	p_plugin->input.get_properties = in_get_properties;
	p_plugin->input.framecount = in_framecount;
	p_plugin->input.seek = in_seek;
	p_plugin->input.load = in_load;
	p_plugin->input.eof = in_eof;
	p_plugin->input.good = in_good;
	p_plugin->input.error_msg = in_error;
	p_plugin->input.reload = in_reload;
	p_plugin->input.get_meta = get_meta;
	p_plugin->input.b_fileBased = TRUE;
	p_plugin->input.flags = VOOInputFlag_DoNotCache;
}


