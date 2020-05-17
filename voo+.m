/**
 *  Support for MOV and mp4 files in vooya
 *  Copyright (C) 2018  Arion Neddens
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

/*
 *	Build with: clang -framework Cocoa -framework AVFoundation -framework CoreVideo -framework CoreMedia -shared -o ./voo+.dylib voo+.m
 */

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#include <stdlib.h>
#include "voo_plugin.h"


@interface movReader : NSObject
@property (strong) NSString *filename;
@property (strong) AVURLAsset *movieAsset;
@property (strong) AVAssetReader *assetReader;
@property (strong) AVAssetReaderTrackOutput *assetReaderOutput;

@property NSInteger width;
@property NSInteger height;
@property float bitrate;
@property NSUInteger frames;
@property NSUInteger next_frame;
@property BOOL eof;
@property float fps;
@property (strong) NSString *error;

@property (strong) NSString *type;
@property (strong) NSString *subtype;
@property NSInteger pixel_format;

@end

void def_message( void *_,const char *what ){
	NSLog(@"[voo+] %s", what);
}
char error[2048];





@implementation movReader
{
	void (*message)(void *,const char*);
	void *msg_ptr;
}

- (instancetype)init{
	_error = @"";
	_movieAsset = nil;
	_assetReader = nil;
	_assetReaderOutput = nil;
	return self;
}

- (BOOL) createReader:(NSUInteger)atFrame{
	return [self createReader:atFrame appInfo:0x0];
}

- (BOOL) createReader:(NSUInteger)atFrame appInfo:(voo_app_info_t *)p_app_info{

	self.eof = NO;
	if( !self.movieAsset ){
		self.movieAsset = [[AVURLAsset alloc] initWithURL:[NSURL fileURLWithPath:self.filename] options:nil];
	}

	if( p_app_info ){
		msg_ptr = p_app_info->p_message_cargo;
		message = p_app_info->pf_console_message;
	}
	if( !message ) message = def_message;

	[self.assetReader cancelReading];
	[self.assetReaderOutput release];
	[self.assetReader release];

	NSError *error = nil;
	self.assetReader = [[AVAssetReader alloc] initWithAsset:self.movieAsset
													  error:&error];

	if( !self.assetReader ){
		if( error )
			self.error = error.userInfo[NSLocalizedFailureReasonErrorKey];
		[self.movieAsset release];
		self.movieAsset = nil;
		return FALSE;
	}

	NSArray *videoTracks=[self.movieAsset tracksWithMediaType:AVMediaTypeVideo];
	if( ![videoTracks count] ){
		self.error = @"This file contains no video.";
		[self report:self.error];
		return FALSE;
	}

	AVAssetTrack *videoTrack0 = [videoTracks objectAtIndex:0];

	CGSize size = [videoTrack0 naturalSize];
	self.width = size.width;
	self.height = size.height;
	self.bitrate = videoTrack0.estimatedDataRate;
	for (id formatDescription in videoTrack0.formatDescriptions) {
		#define FourCC2Str(code) (char[5]){(code >> 24) & 0xFF, (code >> 16) & 0xFF, (code >> 8) & 0xFF, code & 0xFF, 0}
		CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)formatDescription;
		CMVideoCodecType codec = CMFormatDescriptionGetMediaType(desc);
		FourCharCode subType = CMFormatDescriptionGetMediaSubType(desc);
		self.type = [NSString stringWithCString:(const char *)FourCC2Str(codec) encoding:NSUTF8StringEncoding];
		self.subtype = [NSString stringWithCString:(const char *)FourCC2Str(subType) encoding:NSUTF8StringEncoding];
	}

	if( [self.subtype isEqualToString:@"hvc1"] ){
		self.error = @"HEVC is not supported.";
		[self report:self.error];
		return FALSE;
	}

	self.pixel_format = [self.subtype isEqualToString:@"avc1"]
		? kCVPixelFormatType_420YpCbCr8Planar : ([self.subtype isEqualToString:@"ap4h"]
 		? kCVPixelFormatType_444YpCbCr10 : kCVPixelFormatType_422YpCbCr10);


	NSMutableDictionary *dictionary=[[NSDictionary dictionaryWithObjectsAndKeys:
									  [NSNumber numberWithInt:(int)self.pixel_format],
									  (NSString*)kCVPixelBufferPixelFormatTypeKey,
									  nil] mutableCopy];

	self.assetReaderOutput = [[AVAssetReaderTrackOutput alloc]
									initWithTrack:videoTrack0
									outputSettings:dictionary];

	if(![self.assetReader canAddOutput:self.assetReaderOutput]){
		self.error = @"Unknown Quicktime Error.";
		[self report:self.error];
		return FALSE;
	}

	[self.assetReader addOutput:self.assetReaderOutput];

	float durationInSeconds = CMTimeGetSeconds(self.movieAsset.duration);
	self.fps = (float)videoTrack0.nominalFrameRate;
	self.frames = (NSUInteger)(.5 + durationInSeconds * self.fps);
	self.next_frame = atFrame;

	self.assetReader.timeRange = CMTimeRangeMake(CMTimeMake(atFrame,(int)self.fps), kCMTimePositiveInfinity);

	if( [self.assetReader startReading] )
	{
		return TRUE;
	}

	self.error = [NSString stringWithFormat:@"%@\nFile type is %@/%@.", [self.assetReader error].localizedDescription, self.type, self.subtype];
	[self report:self.error];
	return FALSE;
}

- (void)report:(NSString *)what{
	message( msg_ptr,[what UTF8String]  );
}

- (NSString *)codec{
	if( [self.subtype isEqualToString:@"v210"] )
		return @"v210";
	else if( [self.subtype isEqualToString:@"jpeg"] )
		return @"Motion JPEG";
	else if( [self.subtype isEqualToString:@"avc1"] )
		return @"H.264";
	else if( [self.subtype isEqualToString:@"apcn"] )
		return @"Apple ProRes 422 SD";
	else if( [self.subtype isEqualToString:@"apch"] )
		return @"Apple ProRes 422 HQ";
	else if( [self.subtype isEqualToString:@"apcs"] )
		return @"Apple ProRes 422 LT";
	else if( [self.subtype isEqualToString:@"apco"] )
		return @"Apple ProRes 422 Proxy";
	else if( [self.subtype isEqualToString:@"ap4h"] )
		return @"Apple ProRes 4444";

	return @"–";
}

// we are non-ARC
- (void)dealloc{
	[self.movieAsset release];
	[self.assetReader release];
	[self.assetReaderOutput release];
	[super dealloc];
}

@end










VP_API BOOL in_responsible( const vooChar_t *filename, char *sixteen_bytes, void *p_user ){
	char magic[] = {0x00, 0x00, 0x00, 0x14, 0x66, 0x74, 0x79, 0x70, 0x71, 0x74, 0x20, 0x20};
	if( !memcmp( sixteen_bytes, magic, sizeof(magic) ) ) return TRUE;
	uint32_t len = (uint32_t)voo_strlen( filename );
	vooChar_t _filename[1024];
	voo_strcpy( _filename,filename );
	vooChar_t *p = _filename;
	for( ; *p++; ) *p = tolower( *p );
	return len > 4 && (!voo_strcmp( _filename + len - 4, _v(".mov") )
					 ||!voo_strcmp( _filename + len - 4, _v(".mp4") ));
}

VP_API BOOL in_open( const vooChar_t *filename, voo_app_info_t *p_app_info, void **pp_user ){
	movReader *mov_reader = [movReader new];
	*pp_user = mov_reader;
	mov_reader.filename = [NSString stringWithUTF8String:filename];
	if( [mov_reader.filename isEqualToString:@"-"] ){
		mov_reader.error = @"stdin is not supported by the Quicktime Movie Input Plugin.";
		[mov_reader report:mov_reader.error];
		return FALSE;
	}

	return [mov_reader createReader:0 appInfo:p_app_info];
}

VP_API void in_close( void *p_user_seq ){
	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	[mov_reader release];
}

VP_API BOOL in_get_properties( voo_sequence_t *p_info, void *p_user_seq ){

	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	p_info->width = (int)mov_reader.width;
	p_info->height = (int)mov_reader.height;
	p_info->fps = mov_reader.fps;
	p_info->color_space = vooCS_YUV;
	p_info->channel_order = vooCO_c123;
	if( mov_reader.pixel_format == kCVPixelFormatType_422YpCbCr10 ){
		p_info->arrangement = vooDA_v210;
		p_info->bits_per_channel = 10;
	} else if( mov_reader.pixel_format == kCVPixelFormatType_444YpCbCr10 ){
		p_info->arrangement = vooDA_v410;
		p_info->bits_per_channel = 10;
	} else {
		p_info->arrangement = vooDA_planar_420;
		p_info->bits_per_channel = 8;
	}

	return TRUE;
}

VP_API unsigned int in_framecount( void *p_user_seq ){
	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	return (unsigned int)mov_reader.frames;
}

VP_API BOOL in_seek( unsigned int frame, void *p_user_seq ){

	movReader *mov_reader = (__bridge movReader *)p_user_seq;

	if( mov_reader.next_frame == frame ) return TRUE;

	return [mov_reader createReader:frame];
}

VP_API BOOL in_load( unsigned int frame, char *p_buffer, BOOL *pb_skipped, void **pp_frame_user, void *p_user_seq ){

	movReader *mov_reader = (__bridge movReader *)p_user_seq;

	if( frame != mov_reader.next_frame ){
		if( ![mov_reader createReader:frame] )
			return FALSE;
	}

	AVAssetReader *assetReader = mov_reader.assetReader;
	AVAssetReaderTrackOutput *assetReaderOutput = mov_reader.assetReaderOutput;

	if( [assetReader status] != AVAssetReaderStatusReading ){
		mov_reader.next_frame = ~0;
		return FALSE;
	}

	mov_reader.next_frame = frame + 1;
	if( mov_reader.next_frame >= mov_reader.frames )
		mov_reader.eof = YES;

	if( mov_reader.pixel_format == kCVPixelFormatType_420YpCbCr8Planar ){

		CMSampleBufferRef buffer = [assetReaderOutput copyNextSampleBuffer];
		if( assetReader.status == AVAssetReaderStatusFailed ) goto err;
		CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(buffer);
		CVPixelBufferLockBaseAddress(imageBuffer,0);

		size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
		size_t width = CVPixelBufferGetWidth(imageBuffer);
		size_t height = CVPixelBufferGetHeight(imageBuffer);
		uint8_t *src_buff;
		src_buff = CVPixelBufferGetBaseAddressOfPlane(imageBuffer,0);
		bytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,0);
		for( size_t y=height; y--; ){
			memcpy( p_buffer, src_buff, bytesPerRow);
			p_buffer += width;
			src_buff += bytesPerRow;
		}
		src_buff = CVPixelBufferGetBaseAddressOfPlane(imageBuffer,1);
		bytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,1);
		height >>= 1;
		width >>= 1;
		for( size_t y=height; y--; ){
			memcpy( p_buffer, src_buff, bytesPerRow);
			p_buffer += width;
			src_buff += bytesPerRow;
		}
		src_buff = CVPixelBufferGetBaseAddressOfPlane(imageBuffer,2);
		bytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,2);
		for( size_t y=height; y--; ){
			memcpy( p_buffer, src_buff, bytesPerRow);
			p_buffer += width;
			src_buff += bytesPerRow;
		}

		CVPixelBufferUnlockBaseAddress(imageBuffer, 0);

	} else {

		CMSampleBufferRef buffer = [assetReaderOutput copyNextSampleBuffer];
		if( assetReader.status == AVAssetReaderStatusFailed ) goto err;
		CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(buffer);
		CVPixelBufferLockBaseAddress(imageBuffer,0);

		size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
		size_t height = CVPixelBufferGetHeight(imageBuffer);
		uint8_t *src_buff = CVPixelBufferGetBaseAddressOfPlane(imageBuffer,0);
		bytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,0);
		memcpy( p_buffer, src_buff, height * bytesPerRow);

		CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
	}

	*pb_skipped = FALSE;
	return TRUE;

err:
	[mov_reader report:[NSString stringWithFormat:@"%@\n%@\n",
						assetReader.error.localizedDescription,
						assetReader.error.userInfo[@"NSLocalizedFailureReason"]]];
	return FALSE;
}

VP_API BOOL in_eof( void *p_user_seq ){
	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	return mov_reader.eof;
}

VP_API BOOL in_good( void *p_user_seq ){
	return TRUE;
}

VP_API BOOL in_reload( void *p_user_seq ){
	return FALSE;
}

VP_API void in_error( const char **pp_err, void *p_user_seq ){
	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	const char *err = [mov_reader.error UTF8String];
	if( strlen(err) )
		strncpy( error, err, 2047 );
	*pp_err = error;
}

VP_API BOOL in_file_suffixes( int idx, char const **pp_suffix, void *p_user_seq ){
	switch(idx){
		case 0: *pp_suffix = "mov"; break;
		case 1: *pp_suffix = "mp4"; break;
		default:
			return FALSE;
	}
	return TRUE;
}

BOOL in_get_meta( int idx, char *buffer_k, char *buffer_v, void *p_user_seq ){

	movReader *mov_reader = (__bridge movReader *)p_user_seq;
	int _idx=0;
	if( idx == _idx++ ){
		sprintf( buffer_k, "Type/Subtype" );
		sprintf( buffer_v, "%s/%s", [mov_reader.type UTF8String], [mov_reader.subtype UTF8String] );
	} else if( idx == _idx++ ){
		sprintf( buffer_k, "Codec" );
		sprintf( buffer_v, "%s", [[mov_reader codec] UTF8String] );
	} else if( idx == _idx++ ){
		sprintf( buffer_k, "Bitrate" );
		float bps = mov_reader.bitrate;
		const char *u = "";
		if( mov_reader.bitrate > 999999 ){
			u = "M";
			bps /= 1000000;
		} else if( mov_reader.bitrate > 999 ){
			u = "k";
			bps /= 1000;
		}
		sprintf( buffer_v, "%1.2f%sb/s", bps, u );
	} else return FALSE;

	return TRUE;
}





VP_API void voo_describe( voo_plugin_t *p_plugin )
{
	p_plugin->voo_version = VOO_PLUGIN_API_VERSION;

	// plugin main properties
	p_plugin->name = "Quicktime Movie / MP4 Input";
	p_plugin->description = "Brings support for Quicktime Movie and MPEG-4 input.";
	p_plugin->copyright = "(c) 2018 A. Neddens, LGPL";
	p_plugin->version = "ver1.2";

	// p_user could point to static data we need everywhere.
	p_plugin->p_user = NULL;

	p_plugin->input.uid = "voo.mov.0";
	p_plugin->input.name = "Quicktime/MPEG-4 Movie Support";
	p_plugin->input.description = "Quicktime/MPEG-4 Movie";
	p_plugin->input.close = in_close;
	p_plugin->input.get_properties = in_get_properties;
	p_plugin->input.framecount = in_framecount;
	p_plugin->input.seek = in_seek;
	p_plugin->input.load = in_load;
	p_plugin->input.eof = in_eof;
	p_plugin->input.good = in_good;
	p_plugin->input.reload = in_reload;
	p_plugin->input.open = in_open;
	p_plugin->input.responsible = in_responsible;
	p_plugin->input.error_msg = in_error;
	p_plugin->input.file_suffixes = in_file_suffixes;
	p_plugin->input.get_meta = in_get_meta;
	p_plugin->input.b_fileBased = TRUE;
}


