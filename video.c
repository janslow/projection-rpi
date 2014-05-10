/*
Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, OtherCrashOverride
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	 * Redistributions of source code must retain the above copyright
		notice, this list of conditions and the following disclaimer.
	 * Redistributions in binary form must reproduce the above copyright
		notice, this list of conditions and the following disclaimer in the
		documentation and/or other materials provided with the distribution.
	 * Neither the name of the copyright holder nor the
		names of its contributors may be used to endorse or promote products
		derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video decode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

#ifndef VIDEO_H
	#include "video.h"
#endif


static int video_decode(VIDEO_THREAD_DATA_T *video);

static void pause_if_necessary(VIDEO_THREAD_DATA_T *video);
static void devamp_if_necessary(VIDEO_THREAD_DATA_T *video, FILE *in);

static OMX_BUFFERHEADERTYPE* eglBuffer = NULL;
static COMPONENT_T* egl_render = NULL;

static VIDEO_THREAD_DATA_T *video;

void my_fill_buffer_done(void* data, COMPONENT_T* comp)
{
  if (OMX_FillThisBuffer(ilclient_get_handle(egl_render), eglBuffer) != OMX_ErrorNone)
	{
		printf("OMX_FillThisBuffer failed in callback\n");
		exit(1);
	}
}


// Modified function prototype to work with pthreads
void *video_decode_main(void *arg)
{
	printf("pV: video_decode_test start\n");
	video->state = VIDEO_STATE_STOPPED;

	video = arg;

	printf("pV: %s\n", video->filename);

	int code = video_decode(video);
	video->state = VIDEO_STATE_TERMINATED;
	printf("pV: terminating with code %d\n", code);
	return (void*) code;
}

static void pause_if_necessary(VIDEO_THREAD_DATA_T *video) {
	if (video->command == VIDEO_COMMAND_PAUSE) {
		video->state = VIDEO_STATE_PAUSED;
		struct timespec timInterval, timRemainder;
		timInterval.tv_sec = 0;
		timInterval.tv_nsec = 50000000L;
		while (video->command == VIDEO_COMMAND_PAUSE) {
			nanosleep(&timInterval, &timRemainder);
		}
	}
}

static void devamp_if_necessary(VIDEO_THREAD_DATA_T *video, FILE *in) {
	if (feof(in)) {
		if (video->command == VIDEO_COMMAND_DEVAMP)
			video->command = VIDEO_COMMAND_STOP;
		else
			rewind(in);
	}
}

static void setupClockState(OMX_TIME_CONFIG_CLOCKSTATETYPE cstate) {
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;
	cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
	cstate.nWaitMask = 1;
}

static void setupVideoFormat(OMX_VIDEO_PARAM_PORTFORMATTYPE format) {
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 130;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;
}
	
static int video_decode(VIDEO_THREAD_DATA_T *video) {
	COMPONENT_T *list[5];
	TUNNEL_T tunnel[4];
	ILCLIENT_T *client;
	FILE *in;

	int status = 0;
	unsigned int data_len = 0;

	memset(list, 0, sizeof(list));
	memset(tunnel, 0, sizeof(tunnel));

	if((in = fopen(video->filename, "rb")) == NULL)
		return -2;

	if((client = ilclient_init()) == NULL)
	{
		fclose(in);
		return -3;
	}

	if(OMX_Init() != OMX_ErrorNone)
	{
		ilclient_destroy(client);
		fclose(in);
		return -4;
	}

	// callback
	ilclient_set_fill_buffer_done_callback(client, my_fill_buffer_done, 0);

	// Video Decoder
	COMPONENT_T *video_decode = NULL;
	if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
		status = -14;
	list[0] = video_decode;

	// EGL Renderer
	if(status == 0 && ilclient_create_component(client, &egl_render, "egl_render", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_OUTPUT_BUFFERS) != 0)
		status = -14;
	list[1] = egl_render;

	// Clock
	COMPONENT_T *clock = NULL;
	if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[2] = clock;
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	memset(&cstate, 0, sizeof(cstate));
	setupClockState(cstate);

	if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		status = -13;

	// Video Scheduler
	COMPONENT_T *video_scheduler = NULL;
	if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[3] = video_scheduler;

	// Setup tunnels
	set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
	set_tunnel(tunnel+1, video_scheduler, 11, egl_render, 220);
	set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

	// Setup clock tunnel
	if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
		status = -15;
	else
		ilclient_change_component_state(clock, OMX_StateExecuting);

	if(status == 0)
		ilclient_change_component_state(video_decode, OMX_StateIdle);

	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	setupVideoFormat(format);

	if(status == 0 &&
		OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
		ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) != 0)
		status = -16;

	if (status == 0) {
		OMX_BUFFERHEADERTYPE *buf;
		int port_settings_changed = 0;
		int first_packet = 1;

		ilclient_change_component_state(video_decode, OMX_StateExecuting);

		while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
		{
			pause_if_necessary(video);

			devamp_if_necessary(video, in);

			if (video->command == VIDEO_COMMAND_STOP || video->command == VIDEO_COMMAND_TERMINATE) {
				break;
			}
			video->state = VIDEO_STATE_PLAYING;

			// feed data and wait until we get port settings changed
			unsigned char *dest = buf->pBuffer;

			data_len += fread(dest, 1, buf->nAllocLen-data_len, in);

			if(port_settings_changed == 0 &&
				((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
				 (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
																		 ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
			{
				port_settings_changed = 1;

				if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
				{
					status = -7;
					break;
				}

				ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

				// now setup tunnel to egl_render
				if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
				{
					status = -12;
					break;
				}

				// Set egl_render to idle
				ilclient_change_component_state(egl_render, OMX_StateIdle);

				// Enable the output port and tell egl_render to use the texture as a buffer
				//ilclient_enable_port(egl_render, 221); THIS BLOCKS SO CANT BE USED
				if (OMX_SendCommand(ILC_GET_HANDLE(egl_render), OMX_CommandPortEnable, 221, NULL) != OMX_ErrorNone)
				{
					printf("OMX_CommandPortEnable failed.\n");
					exit(1);
				}

				if (OMX_UseEGLImage(ILC_GET_HANDLE(egl_render), &eglBuffer, 221, NULL, video->eglImage) != OMX_ErrorNone)
				{
					printf("OMX_UseEGLImage failed.\n");
					return -1;
				}

				// Set egl_render to executing
				ilclient_change_component_state(egl_render, OMX_StateExecuting);


				// Request egl_render to write data to the texture buffer
				if(OMX_FillThisBuffer(ILC_GET_HANDLE(egl_render), eglBuffer) != OMX_ErrorNone)
				{
					printf("OMX_FillThisBuffer failed.\n");
					return -4;
				}
			}
			if(!data_len)
				break;

			buf->nFilledLen = data_len;
			data_len = 0;

			buf->nOffset = 0;
			if(first_packet)
			{
				buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
				first_packet = 0;
			}
			else
				buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

			if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
			{
				status = -6;
				break;
			}
		}

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
			status = -20;

		// need to flush the renderer to allow video_decode to disable its input port
		ilclient_flush_tunnels(tunnel, 0);

		ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
	}

	fclose(in);

	ilclient_disable_tunnel(tunnel);
	ilclient_disable_tunnel(tunnel+1);
	ilclient_disable_tunnel(tunnel+2);
	ilclient_teardown_tunnels(tunnel);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}
