#define VIDEO_H

#define VIDEO_COMMAND_NULL -1
#define VIDEO_COMMAND_PLAY 0
#define VIDEO_COMMAND_STOP 1
#define VIDEO_COMMAND_PAUSE 2
#define VIDEO_COMMAND_DEVAMP 3
#define VIDEO_COMMAND_TERMINATE 4

#define VIDEO_STATE_PLAYING 0
#define VIDEO_STATE_STOPPED 1 
#define VIDEO_STATE_PAUSED 2
#define VIDEO_STATE_TERMINATED 3

void* video_decode_main(void* arg);

typedef struct
{
   char *filename;
   void *eglImage;
   int command;
   int state;
} VIDEO_THREAD_DATA_T;