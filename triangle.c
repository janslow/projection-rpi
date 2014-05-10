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

// A rotating cube rendered with OpenGL|ES. Three images used as textures on the cube faces.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "bcm_host.h"

#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#include "triangle.h"
#ifndef VIDEO_H
  #include "video.h"
#endif


#define PATH "./"

#define IMAGE_SIZE_WIDTH 1920
#define IMAGE_SIZE_HEIGHT 1080

// #define ENABLE_TEXTURES

#ifndef M_PI
  #define M_PI 3.141592654
#endif
  

typedef struct
{
  uint32_t screen_width;
  uint32_t screen_height;
// OpenGL|ES objects
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;
  GLuint tex;
// Alpha channel
  float alpha;
} CUBE_STATE_T;

static void init_ogl(CUBE_STATE_T *state);
static void redraw_scene(CUBE_STATE_T *state);
static void init_textures(CUBE_STATE_T *state, char *filename);
static void exit_func(void);
static void update_fade(CUBE_STATE_T *state, FADE_DATA_T *fade);
static double seconds();
static void stop_video_blocking(VIDEO_THREAD_DATA_T *video);

static volatile int terminate;
static CUBE_STATE_T _state, *state=&_state;

static void* eglImage = 0;
static pthread_t videoThread;
static VIDEO_THREAD_DATA_T _video, *video=&_video;
static FADE_DATA_T _fade, *fade=&_fade;

static GLbyte quadx[4*3] = {
  -10, -10,  0,
  10, -10,  0,
  -10,  10,  0,
  10,  10,  0
};
static const GLfloat texCoords[4 * 2] = {
  0.f,  0.f,
  1.f,  0.f,
  0.f,  1.f,
  1.f,  1.f
};
static GLfloat colorx[4*4] = {
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
};

/***********************************************************
 * Name: init_ogl
 *
 * Arguments:
 *       CUBE_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the display, OpenGL|ES context and screen stuff
 *
 * Returns: void
 *
 ***********************************************************/
static void init_ogl(CUBE_STATE_T *state)
{
  int32_t success = 0;
  EGLBoolean result;
  EGLint num_config;

  static EGL_DISPMANX_WINDOW_T nativewindow;

  DISPMANX_ELEMENT_HANDLE_T dispman_element;
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  static const EGLint attribute_list[] =
  {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    //EGL_SAMPLES, 4,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };
  
  EGLConfig config;
  
  // get an EGL display connection
  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(state->display!=EGL_NO_DISPLAY);
  
  // initialize the EGL display connection
  result = eglInitialize(state->display, NULL, NULL);
  assert(EGL_FALSE != result);

  // get an appropriate EGL frame buffer configuration
  // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
  result = eglSaneChooseConfigBRCM(state->display, attribute_list, &config, 1, &num_config);
  assert(EGL_FALSE != result);
  
  // create an EGL rendering context
  state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, NULL);
  assert(state->context!=EGL_NO_CONTEXT);
  
  // create an EGL window surface
  success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
  assert( success >= 0 );
  
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = state->screen_width;
  dst_rect.height = state->screen_height;
  
  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = state->screen_width << 16;
  src_rect.height = state->screen_height << 16;        

  dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
  dispman_update = vc_dispmanx_update_start( 0 );
      
  dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
    0/*layer*/, &dst_rect, 0/*src*/,
    &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
    
  nativewindow.element = dispman_element;
  nativewindow.width = state->screen_width;
  nativewindow.height = state->screen_height;
  vc_dispmanx_update_submit_sync( dispman_update );
    
  state->surface = eglCreateWindowSurface( state->display, config, &nativewindow, NULL );
  assert(state->surface != EGL_NO_SURFACE);

  // connect the context to the surface
  result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
  assert(EGL_FALSE != result);
  
  state->alpha = 1.0f;
  // Set background color and clear buffers
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  // Enable back face culling.
  glEnable(GL_CULL_FACE);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);

  glMatrixMode(GL_MODELVIEW);

  // Set-up model

  glViewport(0, 0, (GLsizei)state->screen_width, (GLsizei)state->screen_height);
    
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glOrthof(-10, 10, -10, 10, 40, 60);

  glEnableClientState( GL_VERTEX_ARRAY );
  glVertexPointer( 3, GL_BYTE, 0, quadx );
  glEnableClientState (GL_COLOR_ARRAY);
  glColorPointer(4, GL_FLOAT, 0, colorx);

  // reset model position
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}


/***********************************************************
 * Name: redraw_scene
 *
 * Arguments:
 *       CUBE_STATE_T *state - holds OGLES model info
 *
 * Description:   Draws the model and calls eglSwapBuffers
 *                to render to screen
 *
 * Returns: void
 *
 ***********************************************************/
static void redraw_scene(CUBE_STATE_T *state)
{
  glLoadIdentity();
  // move camera back to see the cube
  glTranslatef(0.f, 0.f, -50.0f);

  colorx[3] = state->alpha;
  colorx[7] = state->alpha;
  colorx[11] = state->alpha;
  colorx[15] = state->alpha;

  // Start with a clear screen
  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  // draw first 4 vertices
  glDrawArrays( GL_TRIANGLE_STRIP, 0, 4);

  eglSwapBuffers(state->display, state->surface);
}

/***********************************************************
 * Name: init_textures
 *
 * Arguments:
 *       CUBE_STATE_T *state - holds OGLES model info
 *       char *filename - filename of video texture
 *
 * Description:   Initialise OGL|ES texture surfaces to use image
 *                buffers
 *
 * Returns: void
 *
 ***********************************************************/
static void init_textures(CUBE_STATE_T *state, char *filename)
{
  #ifdef ENABLE_TEXTURES
  // load three texture buffers but use them on six OGL|ES texture surfaces
  glGenTextures(1, &state->tex);

  glBindTexture(GL_TEXTURE_2D, state->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, 0,
           GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* Create EGL Image */
  eglImage = eglCreateImageKHR(
           state->display,
           state->context,
           EGL_GL_TEXTURE_2D_KHR,
           (EGLClientBuffer)state->tex,
           0);
   
  if (eglImage == EGL_NO_IMAGE_KHR)
  {
    printf("eglCreateImageKHR failed.\n");
    exit(1);
  }
  #endif

  video->filename = filename;
  video->eglImage = eglImage;
  video->state = VIDEO_STATE_STOPPED;
  video->command = VIDEO_COMMAND_PLAY;
  pthread_create(&videoThread, NULL, video_decode_main, &video);

  #ifdef ENABLE_TEXTURES
  // setup overall texture environment
  glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glEnable(GL_TEXTURE_2D);

  // Bind texture surface to current vertices
  glBindTexture(GL_TEXTURE_2D, state->tex);
  #endif
}
//------------------------------------------------------------------------------

static void exit_func(void)
// Function to be passed to atexit().
{
  
  printf("\nCLEAN UP\n");
  if (eglImage != 0)
  {
    if (!eglDestroyImageKHR(state->display, (EGLImageKHR) eglImage))
      printf("eglDestroyImageKHR failed.");
  }

  // clear screen
  glClear( GL_COLOR_BUFFER_BIT );
  printf("Cleared color buffer\n");
  eglSwapBuffers(state->display, state->surface);
  printf("Swapped buffers\n");

  // Release OpenGL resources
  eglMakeCurrent( state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
  printf("Made Current\n");
  eglDestroySurface( state->display, state->surface );
  printf("Destroyed Surface\n");
  eglDestroyContext( state->display, state->context );
  printf("Destroyed Context\n");
  eglTerminate( state->display );

  printf("\ncube closed\n");
}

static void stop_video_blocking(VIDEO_THREAD_DATA_T* video) {
  video->command = VIDEO_COMMAND_TERMINATE;

  struct timespec timInterval, timRemainder;
  timInterval.tv_sec = 0;
  timInterval.tv_nsec = 100000000L;
  printf("Waiting for video thread to terminate\n");
  while (video->state != VIDEO_STATE_TERMINATED)
  {
    nanosleep(&timInterval, &timRemainder);
  }
}

//==============================================================================

static double seconds() {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (double)t.tv_nsec / 1000000000 + (double)t.tv_sec;
}

static void update_fade(CUBE_STATE_T *state, FADE_DATA_T *fade) {
  if (fade < 0 || fade->speed < 0)
    return;
  // dir > 1 iff alpha is increasing
  int dir = 1;
  if (fade->target < fade->startAlpha)
    dir = -1;

  double timeDiff = seconds() - fade->startSeconds;
  // delta = magnitude of change since start
  float delta = timeDiff * fade->speed;
  // alpha = alpha' * delta * dir
  float alpha = fade->startAlpha + (delta * dir);
  if ((dir > 0 && alpha > fade->target) || (dir < 0 && alpha < fade->target)) {
    printf("Finished fade to %f\n", fade->target);
    state->alpha = fade->target;
    fade->speed = -1;
  } else {
    state->alpha = alpha;
  }
}
void sig_handler(int signo) {
  terminate = 1;
  signal(SIGINT, SIG_DFL);
}

int main (int argc, char **argv)
{
  if (argc < 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    exit(1);
  }

  bcm_host_init();
  printf("Note: ensure you have sufficient gpu_mem configured\n");

  // Clear application state
  memset( state, 0, sizeof( *state ) );
  printf("State memory allocated\n");
  
  // Start OGLES
  init_ogl(state);
  printf("OpenGL ES initialized\n");

  // initialise the OGLES texture(s)
  init_textures(state, argv[1]);
  printf("Textures Initialized\n");

  fade->target = 0.0f;
  fade->speed = 0.1f;
  fade->startSeconds = seconds();
  fade->startAlpha = state->alpha;

  signal(SIGINT, sig_handler);

  printf("\nStarting render loop\n");
  while (!terminate)
  {
    update_fade(state, fade);

    redraw_scene(state);
  }
  printf("Finished render loop\n");

  stop_video_blocking(video);

  printf("Video thread terminated\n");
  exit_func();
  printf("Clean-up finished\n");
  return 0;
}