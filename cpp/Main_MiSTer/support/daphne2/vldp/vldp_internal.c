/*
 * vldp_internal.c
 *
 * Copyright (C) 2001 Matt Ownby
 *
 * This file is part of VLDP, a virtual laserdisc player.
 *
 * VLDP is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * VLDP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// contains the function for the VLDP private thread

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning (disable:4996)
#endif

#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>	// for malloc/free
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "vldp_internal.h"
#include "vldp_common.h"
#include "mpegscan.h"

//// forward declarations
static void paused_handler(void);
static void play_handler(void);
static void vldp_process_sequence_header(void);
static int ivldp_got_new_command(void);
static void ivldp_ack_command(void);
static void ivldp_lock_handler(void);
static void ivldp_respond_req_play(void);
static void ivldp_render(void);
static void idle_handler_search(int skip);
static void idle_handler_open(void);
static void idle_handler_precache(void);
static void idle_handler_play(void);
static VLDP_BOOL ivldp_parse_mpeg_frame_offsets(char *datafilename,
      uint32_t mpeg_size);
static VLDP_BOOL ivldp_get_mpeg_frame_offsets(char *mpeg_name);

static VLDP_BOOL io_open_precached(unsigned int uIdx);
static VLDP_BOOL io_open(const char *cpszFilename);
static VLDP_BOOL io_is_open(void);
static unsigned int io_length(void);
static VLDP_BOOL io_seek(unsigned int uPos);
static unsigned int io_read(void *buf, unsigned int uBytesToRead);
static void io_close(void);
static void ivldp_respond_req_speedchange(void);
static void ivldp_respond_req_pause_or_step(void);

#pragma warning (push)
#pragma warning (disable:4018)
static void vo_null_draw(uint8_t * const * buf, void *id)
{
   int32_t correct_elapsed_ms = 0;	// we want this signed since we compare against actual_elapsed_ms
   int32_t actual_elapsed_ms = 0;	// we want this signed because it could be negative
   unsigned int uStallFrames = 0;	// how many frames we have to stall during the loop (for multi-speed playback)

   // if we don't need to skip any frames
   if (!(s_frames_to_skip | s_skip_all))
   {
      // loop once, or more than once if we are paused
      do
      {
         VLDP_BOOL bFrameNotShownDueToCmd = VLDP_FALSE;

         // PERFORMANCE WARNING:
         //  We need to use 64-bit math here because otherwise, we will overflow a little after 2 minutes,
         //   using 32-bit math.
         // If you want to assume that you will never be playing video longer than 2 minutes, then you can change this back
         //  to 32-bit integer math.
         // Also, you can use floating point math here, but some CPU's (gp2x) don't have floating point units, which drastically
         //  hurts performance.  On a fast modern PC, you probably won't notice a difference either way.
         int64_t s64Ms = s_uFramesShownSinceTimer;
         s64Ms = (s64Ms * 1000000) / g_out_info.uFpks;

         // compute how much time ought to have elapsed based on our frame count
         correct_elapsed_ms = (int32_t) (s64Ms) +
            // add on any extra delay that has been requested (simulated seek delay)
            s_extra_delay_ms;
         actual_elapsed_ms = g_in_info->uMsTimer - s_timer;

         // the extra delay should only be 'used' once, so for safety reasons we reset
         // it here, where we can guarantee that it only will be used once.
         s_extra_delay_ms = 0;

         // if we are caught up enough that we don't need to skip any frames, then display the frame
         if (actual_elapsed_ms < (correct_elapsed_ms + g_out_info.u2milDivFpks))
         {
//            // this is the potentially expensive callback that gets the hardware overlay
//            // ready to be displayed, so we do this before we sleep
//            // NOTE : if this callback fails, we don't want to display the frame due to double buffering considerations
//            if (g_in_info->prepare_frame(&g_yuv_buf[(int) id]))
//            {

               // stall if we are playing too quickly and if we don't have a command waiting for us
               while (((int32_t) (g_in_info->uMsTimer - s_timer) < correct_elapsed_ms)
                     && (!bFrameNotShownDueToCmd))
               {
                  // IMPORTANT: this delay should come before the check for ivldp_got_new_command,
                  //  so that if we get a new command, we exit the loop immediately without
                  //  delaying, so that we don't have to check a second time for a new command.
//                  SDL_Delay(1);	// note, if this is set to 0, we don't get commands as quickly

                  // Breaking when getting a new commend before our frame has expired
                  //  will shorten 1 frame's length.  However, it could speed skips up,
                  //  so I am leaving it in.
                  // Also, if we get a new command, uMsTimer may not advance until
                  //  we acknowledge the new command.
                  if (ivldp_got_new_command())
                  {
                     // strip off count and examine command
                     switch(g_req_cmdORcount & 0xF0)
                     {
                        case VLDP_REQ_PAUSE:
                        case VLDP_REQ_STEP_FORWARD:
                           ivldp_respond_req_pause_or_step();
                           break;
                        case VLDP_REQ_SPEEDCHANGE:
                           ivldp_respond_req_speedchange();
                           break;
                        case VLDP_REQ_NONE:
                           break;

                           // Anything else, we will not show the next frame and will
                           //  immediately exit this loop in order to handle the command
                           //  elsewhere.
                        default:
                           bFrameNotShownDueToCmd = VLDP_TRUE;
                           break;
                     }
                  }
               }

               // If a command comes in at the last second,
               //  we don't want to render the next frame that we were going to because it could cause overrun
               //  so we only display the frame if we haven't received a command
               // draw the frame
               // we are using the pointer 'id' as an index, kind of risky, but convenient :)
//               if (!bFrameNotShownDueToCmd)
//                  g_in_info->display_frame(&g_yuv_buf[(int) id]);
               // end if we didn't get a new command to interrupt the frame being displayed
//            } // end if the frame was prepared properly
            // else maybe we couldn't get a lock on the buffer fast enough, so we'll have to wait ...

         } // end if we don't drop any frames

         // if the frame was either displayed or dropped (due to lag) ...
         if (!bFrameNotShownDueToCmd)
         {
            // now that the frame has either been displayed or dropped, we can update the counter to reflect
            // NOTE : this should come before play_handler() is called, because if we become paused,
            //  we change the value of s_uFramesShownSinceTimer.
            ++s_uFramesShownSinceTimer;
         }

         // if the frame is to be paused, then stall
         if (s_paused)
            paused_handler();
         // else if we are supposed to be playing
         else
         {
            play_handler();

            // We only want to advance the current frame if we didn't receive a pause request in the play handler.
            // NOTE : this should come after the handlers in case the state of s_paused changes
            if (!s_paused)
            {
               // If we aren't going to stall on any frames ...
               // NOTE : I think this should be toward the outside of this loop, because
               //  if we are skipping while playing at a slower speed, I would think that
               //  the skip should be slower too.  I could be wrong though ...
               // We DO want to make sure that if we are stalling, that the current frame
               //  is not incremented.
               if (uStallFrames == 0)
               {
                  // if we have no pending frame, then the frame we've just displayed is the proceeding frame
                  if (s_uPendingSkipFrame == 0)
                  {
                     // only advance frame # if we've displayed/dropped a frame
                     if (!bFrameNotShownDueToCmd)
                     {
                        ++g_out_info.current_frame;

                        // if we have to stall one or more frames per frame (multi-speed playback)
                        if (s_stall_per_frame > 0)
                           uStallFrames = s_stall_per_frame;

                        // if we are skipping one or more frames per frame that is displayed
                        // (multi-speed playback)
                        if (s_skip_per_frame > 0)
                           s_frames_to_skip = s_frames_to_skip_with_inc = s_skip_per_frame;
                        // else don't adjust the frameskip variables
                     }
                     // else don't advance the frame number
                  }
                  // else we just skipped, so update current frame to reflect that ...
                  else
                  {
                     g_out_info.current_frame = s_uPendingSkipFrame;
                     s_uPendingSkipFrame = 0;
                  }
               } // end if we aren't going to stall on the currently rendered frame
               // else don't increment the frame, but decrement the uStallFrames counter
               else
                  --uStallFrames;
            }
         }

      } while ((s_paused || uStallFrames > 0) && !s_skip_all && !s_step_forward);
      // loop while we are paused OR while we are stalling so video overlay gets redrawn

      s_step_forward = 0;	// clear this in case it was set (since we have now stepped forward)

   } // end if we don't have frames to skip
   // if we have frames to skip
   else
   {		
      // we could skip frames for another reason
      if (s_frames_to_skip > 0)
      {
         --s_frames_to_skip;	// we've skipped a frame, so decrease the count

         // if we need to also increase the frame number (multi-speed playback)
         if (s_frames_to_skip_with_inc > 0)
         {
            --s_frames_to_skip_with_inc;
            ++g_out_info.current_frame;
         }
      }
   }
}
#pragma warning (pop)

static void vo_null_setup_fbuf(uint8_t ** buf, void ** id)
{
//	static int buffer_index = 0;
//	*id = (int*) buffer_index;
//	// We are setting an integer value to a pointer ...
//	// Because it is convenient to let the pointer hold the value of this integer for us
//	// Hopefully it doesn't cause any trouble later ;)
//
//    buf[0] = g_yuv_buf[buffer_index].Y;
//    buf[1] = g_yuv_buf[buffer_index].U;
//    buf[2] = g_yuv_buf[buffer_index].V;
//
//    buffer_index++;
//
//	// we are operating under a (safe?) assumption that this function is called in sets of YUV_BUF_COUNT
//	// so it should be safe to wraparound ... if our assumption is wrong, major havoc will ensure :)
//	if (buffer_index >= YUV_BUF_COUNT)
//		buffer_index = 0;
}

//static int vo_null_setup (int width, int height, vo_setup_result_t * result)
//{
//	int i = 0;
//
//	for (i = 0; i < YUV_BUF_COUNT; i++)
//	{
//		// allocate buffer according to size of picture
//		// We do not re-allocate these buffers if they have already been previous allocated, as a safety measure
//		g_yuv_buf[i].Y_size = width * height;
//		g_yuv_buf[i].UV_size = g_yuv_buf[i].Y_size >> 2;
//		if (!g_yuv_buf[i].Y) g_yuv_buf[i].Y = (unsigned char *) malloc(g_yuv_buf[i].Y_size);
//		if (!g_yuv_buf[i].U) g_yuv_buf[i].U = (unsigned char *) malloc(g_yuv_buf[i].UV_size);
//		if (!g_yuv_buf[i].V) g_yuv_buf[i].V = (unsigned char *) malloc(g_yuv_buf[i].UV_size);
//	}
//
//    result->convert = NULL;
//    return 0;
//}

static void vo_null_close(void)
{
//	int i = 0;
	
//	for (i = 0; i < YUV_BUF_COUNT; i++)
//	{
//		// NOTE : it's ok to call free(NULL) so we do not need to do safety checking here!
//		free(g_yuv_buf[i].Y);
//		g_yuv_buf[i].Y = NULL;
//		free(g_yuv_buf[i].U);
//		g_yuv_buf[i].U = NULL;
//		free(g_yuv_buf[i].V);
//		g_yuv_buf[i].V = NULL;
//	}
}

static void vo_null_open (void)
{
   //memset(g_yuv_buf, 0, sizeof(g_yuv_buf));	// MPO, fill this struct with 0's so that we can track whether mem has been allocated or not
}

/////////////////////////////
// VLDP INTERNAL
/////////////////////////////

// NOTICE : these variables should only be used by the private thread !!!!!!!!!!!!

uint8_t s_old_req_cmdORcount = CMDORCOUNT_INITIAL;	// the last value of the command byte we received
int s_paused = 0;	// whether the video is to be paused
int s_step_forward = 0;	// whether to step 1 frame forward
int s_blanked = 0;	// whether the mpeg video is to be blanked
int s_frames_to_skip = 0;	// how many frames to skip before rendering the next frame (used for P and B frames seeking)
int s_frames_to_skip_with_inc = 0;	// // how many frames to skip while increasing the frame number (for multi-speed playback)
int s_skip_all = 0;	// if we are to skip any frame to be displayed (to avoid seeing frames we shouldn't)

// How many frames we've skipped when s_skip_all is enabled.
// (for performance tuning, we want to minimize this number)
unsigned int s_uSkipAllCount = 0;

uint32_t s_timer = 0;	// FPS timer used by the blitting code to run at the right speed

// any extra delay that null_draw_frame() will use before drawing a frame (intended for laserdisc seek delay simulation)
// NOTE : this value gets reset to 0 after it has been 'used'
uint32_t s_extra_delay_ms = 0;

// how many frames must have been displayed (relative to s_timer!!!)
// before we advance to the next frame
uint32_t s_uFramesShownSinceTimer = 0;

// which frame we've skipped to (0 if there is nothing pending)
unsigned int s_uPendingSkipFrame = 0;

// multi-speed variables ...
unsigned int s_skip_per_frame = 0;	// how many frames to skip per frame (for playing at 2X for example)
unsigned int s_stall_per_frame = 0;	// how many frames to stall per frame (for playing at 1/2X for example)

// pre-cache variables
#define MAX_PRECACHE_FILES 300	/* maximum number of files that we'll pre-cache */
VLDP_BOOL s_bPreCacheEnabled = VLDP_FALSE;	// whether precaching is currently enabled
unsigned int s_uCurPreCacheIdx = 0;	// if pre-caching is enabled, which index is currently being used
unsigned int s_uPreCacheIdxCount = 0;	// how many files have been precached
struct precache_entry_s s_sPreCacheEntries[MAX_PRECACHE_FILES];	// struct array holding precache data


#define MAX_LDP_FRAMES 60000

static FILE *g_mpeg_handle = NULL;	// mpeg file we currently have open
// TODO may need mpeg2 for this
//static mpeg2dec_t *g_mpeg_data = NULL;	// structure for libmpeg2's state
static uint32_t g_frame_position[MAX_LDP_FRAMES] = { 0 };	// the file position of each I frame
static uint16_t g_totalframes = 0;	// total # of frames in the current mpeg

#define BUFFER_SIZE 262144
static uint8_t g_buffer[BUFFER_SIZE];	// buffer to hold mpeg2 file as we read it in

#define HEADER_BUF_SIZE 200
static uint8_t g_header_buf[HEADER_BUF_SIZE];
static unsigned int g_header_buf_size = 0;	// size of the header buffer

// how many frames we will stall after beginning playback (should be 1, because presumably before we start playing,
//  the disc has been paused showing the same frame, and we want the frame to display 1 more frame before moving
//  to the next one.  This behavior is somewhat arbitrary to act like we think laserdisc players act, but this
//  value could also be proven to be 0 if someone does future research.)
#define PLAY_FRAME_STALL 1


void open()
{
    printf("opening\n");
    idle_handler_open();

}

void search()
{
    idle_handler_search(0);
}

////////////////////////////////////////////////

// this is our video thread which gets called
void idle_handler()
{
	int done = 0;

	vo_null_open();	// open 'null' driver (we just pass decoded frames to parent thread)
    //g_mpeg_data = mpeg2_init();

	// unless we are drawing video to the screen, we just sit here
	// and listen for orders from the parent thread
	while (!done)
	{

		// while we have received new commands to be processed
		// (so we don't go to sleep on skips)
		while (ivldp_got_new_command() && !done)
		{
			// examine the actual command (strip off the count)
			switch(g_req_cmdORcount & 0xF0)
			{
			case VLDP_REQ_QUIT:
				done = 1;
                io_close();

                g_out_info.status = STAT_ERROR;
//            mpeg2_close(g_mpeg_data);	// shutdown libmpeg2
                vo_null_close();		// shutdown null driver

            // de-allocate any files that have been precached
            while (s_uPreCacheIdxCount > 0)
            {
               --s_uPreCacheIdxCount;
               free(s_sPreCacheEntries[s_uPreCacheIdxCount].ptrBuf);
            }

            ivldp_ack_command();	// acknowledge quit command
				break;
			case VLDP_REQ_OPEN:
				idle_handler_open();
				break;
			case VLDP_REQ_PRECACHE:
				idle_handler_precache();
				break;
			case VLDP_REQ_PLAY:
				idle_handler_play();
				break;
			case VLDP_REQ_SEARCH:
				idle_handler_search(0);
				break;
			case VLDP_REQ_SKIP:
				idle_handler_search(1);
				break;
			case VLDP_REQ_PAUSE:	// pause command while we're already idle?  this is an error
			case VLDP_REQ_STOP:	// stop command while we're already idle? this is an error
				g_out_info.status = STAT_ERROR;
				ivldp_ack_command();
				break;
			case VLDP_REQ_LOCK:
				ivldp_lock_handler();
				break;
			default:
				fprintf(stderr, "VLDP WARNING : Idle handler received command which it is ignoring\n");
				break;
			} // end switch
//			SDL_Delay(0);	// give other threads some breathing room (but not much hehe)
		} // end if we got a new command

		g_in_info->render_blank_frame();	// This makes sure that the video overlay gets drawn even if there is no video being played

		// we need to delay here because otherwise, this idle loop will execute at 100% cpu speed and really slow things down
		// It shouldn't hurt us when we get a command that requires immediate attention (such as skip) because of the
		// inner while loop above (while ivldp_got_new_command)
		// NOTE : we want to delay for about 1 frame (or field) here
//		SDL_Delay(16);	// 1 field is 16.666ms assuming 60 hz

	} // end while we have not received a quit command

	//return 0;
}

// returns 1 if there is a new command waiting for us or 0 otherwise
static int ivldp_got_new_command(void)
{
	// if they are no longer equal	
	if (g_req_cmdORcount != s_old_req_cmdORcount)
		return 1;
	
	return 0;
}

// acknowledges a command sent by the parent thread
// NOTE : We don't check to see if parent thread got our acknowledgement because it creates too much latency
static void ivldp_ack_command(void)
{
	s_old_req_cmdORcount = g_req_cmdORcount;
	g_ack_count++;	// here is where we acknowledge
}

static void ivldp_lock_handler(void)
{
	ivldp_ack_command();
	{
		VLDP_BOOL bLocked = VLDP_TRUE;

		// the user should unlock immediately after locking, so we need not check for other commands
		while (bLocked == VLDP_TRUE)
		{
//			SDL_Delay(1);
			if (ivldp_got_new_command())
			{
				switch (g_req_cmdORcount & 0xF0)
				{
				case VLDP_REQ_UNLOCK:
					ivldp_ack_command();
					bLocked = VLDP_FALSE;
					break;
				default:
					fprintf(stderr, "WARNING : lock handler received a command %x that wasn't to unlock it\n", g_req_cmdORcount);
					break;
				}
			}
		}
	}
}

// the handler we call if we're paused... called from video_out_sdl
// this function is called from within a while loop
static void paused_handler(void)
{
	// the moment we render the still frame, we need to reset the FPS timer so we don't try to catch-up
	if (g_out_info.status != STAT_PAUSED)
	{
		g_out_info.status = STAT_PAUSED;

		// reset these vars because otherwise null_draw_frame will loop redundantly for no good reason
		s_timer = g_in_info->uMsTimer;	// since we have just rendered the frame we searched to, we refresh the timer
		s_uFramesShownSinceTimer = 1;	// this gives us a little breathing room
	}

	// if we have a new command coming in	
	if (ivldp_got_new_command())
	{
		// strip off the count and examine the command
		switch (g_req_cmdORcount & 0xF0)
		{
		case VLDP_REQ_PLAY:
			ivldp_respond_req_play();
			break;
		// if we get any of these commands, we have to skip the remaining buffered frames and reset
		case VLDP_REQ_STOP:
		case VLDP_REQ_QUIT:
		case VLDP_REQ_OPEN:
		case VLDP_REQ_SEARCH:
			s_skip_all = 1;
			s_uSkipAllCount = 0;
			// we don't acknowledge these requests here, we let the idle handler take care of them
			break;
		case VLDP_REQ_STEP_FORWARD:
			// if they've also requested a step forward
			// NOTE : no need to change status, as we started paused and we'll end paused
			ivldp_ack_command();
			s_step_forward = 1;
			break;
		case VLDP_REQ_LOCK:
			ivldp_lock_handler();
			break;
		default:	// else if we get a pause command or another command we don't know how to handle, just ignore it
			fprintf(stderr, "WARNING : pause handler received command %x that it is ignoring\n", g_req_cmdORcount);
			ivldp_ack_command();	// acknowledge the command
			break;
		} // end switch
	} // end if we have a new command coming in
}

// the handler we call if we're playing
// this handler is called from within a while loop
static void play_handler(void)
{
	// if we've received a new incoming command
	if (ivldp_got_new_command())
	{
		// strip off count and examine command
		switch(g_req_cmdORcount & 0xF0)
		{
		case VLDP_REQ_NONE:	// no incoming command
			break;
		case VLDP_REQ_PAUSE:
		case VLDP_REQ_STEP_FORWARD:
			ivldp_respond_req_pause_or_step();
			break;
		case VLDP_REQ_SPEEDCHANGE:
			ivldp_respond_req_speedchange();
			break;
		case VLDP_REQ_STOP:
		case VLDP_REQ_QUIT:
		case VLDP_REQ_OPEN:
		case VLDP_REQ_SEARCH:
		case VLDP_REQ_SKIP:
			s_skip_all = 1;	// bail out, handle these requests in the idle handler
			s_uSkipAllCount = 0;
			break;
		case VLDP_REQ_LOCK:
			ivldp_lock_handler();
			break;
		default:	// unknown or redundant command, just ignore
			ivldp_ack_command();
			fprintf(stderr, "WARNING : play handler received command which it is ignoring\n");
			break;
		}
	} // end if we got a new command
}

// sets the framerate inside our info structure based upon the framerate code received
static void ivldp_set_framerate(uint8_t frame_rate_code)
{
	// now to compute the framerate

	switch (frame_rate_code)
	{
	case 1:
		g_out_info.uFpks = 23976;
		break;
	case 2:
		g_out_info.uFpks = 24000;
		break;
	case 3:
		g_out_info.uFpks = 25000;
		break;
	case 4:
		g_out_info.uFpks = 29970;
		break;
	case 5:
		g_out_info.uFpks = 30000;
		break;
	case 6:
		g_out_info.uFpks = 50000;
		break;
	case 7:
		g_out_info.uFpks = 59940;
		break;
	case 8:
		g_out_info.uFpks = 60000;
		break;
	default:
		// else we got an invalid frame rate code
		fprintf(stderr, "ERROR : Invalid frame rate code!\n");
		g_out_info.uFpks = 1000;	// to avoid divide by 0 error
		break;
	} // end switch

	// precalculate values that we use over and over again
	g_out_info.u2milDivFpks = 2000000 / g_out_info.uFpks;

}

//static void decode_mpeg2(uint8_t *current, uint8_t *end)
//{
//    const mpeg2_info_t *info;
//    mpeg2_state_t state;
//    vo_setup_result_t setup_result;
//    mpeg2_buffer(g_mpeg_data, current, end);
//    info = mpeg2_info(g_mpeg_data);
//
//    // loop until we return (state is -1)
//    while (1) {
//        state = mpeg2_parse(g_mpeg_data);
//        switch (state) {
//        case STATE_BUFFER:
//            return;
//    	case STATE_SEQUENCE:
//		    /* might set nb fbuf, convert format, stride */
//		    /* might set fbufs */
////		    if (vo_null_setup (info->sequence->width,
////				       info->sequence->height, &setup_result))
////				//printf (stderr, "display setup failed\n");	// this should never happen
//
////		    if (setup_result.convert)
////				mpeg2_convert (g_mpeg_data, setup_result.convert, NULL);
//
//            uint8_t * buf[3];
//            void * id;
//
//            //vo_null_setup_fbuf(buf, &id);
//            mpeg2_set_buf (g_mpeg_data, buf, id);
//            //vo_null_setup_fbuf(buf, &id);
//            mpeg2_set_buf (g_mpeg_data, buf, id);
//            //vo_null_setup_fbuf(buf, &id);
//            mpeg2_set_buf (g_mpeg_data, buf, id);
//            break;
//        case STATE_SLICE:
//        case STATE_END:
//        case STATE_INVALID_END:
//            /* draw current picture */
//            /* might free frame buffer */
//            //if (info->display_fbuf) vo_null_draw (info->display_fbuf->buf,info->display_fbuf->id);
//            break;
//        default:
//            break;
//        } // end switch
//    }     // end endless for loop
//}

/////////////////

// Pre-caches sequence header so that vldp_process_sequence_header (and thus any seeks) are faster
// NOTE: this does change the file position
void vldp_cache_sequence_header()
{
	uint32_t val = 0;
	unsigned int index = 0;

	io_seek(0);	// start at beginning
	io_read(g_header_buf, HEADER_BUF_SIZE); // assume that we must find the first frame in this chunk of bytes
		// if not, we'll have to increase the number

	// go until we have found the first frame or we run out of data
	while (val != 0x000001B8)
	{
		val = val << 8;
		val |= g_header_buf[index];	// add newest byte to bottom of val
		index++;	// advance the end pointer
		if (index > HEADER_BUF_SIZE)
		{
			fprintf(stderr, "VLDP : Could not find first frame in 0x%x bytes.  Modify source code to increase buffer!\n", HEADER_BUF_SIZE);
			break;
		}
	}

	// subtract 4 because we stopped when we found the 4 byte header of the first frame
	g_header_buf_size = index - 4;
}

// feeds libmpeg2 the beginning of the file up to the first Group of Picture
// It needs this info to get setup to play from an arbitrary position in the file that isn't the beginning
// In other words, this is ONLY used when we are seeking to an arbitrary frame
static void vldp_process_sequence_header(void)
{
//	decode_mpeg2 (g_header_buf, g_header_buf + g_header_buf_size);	// decode the pre-cached sequence header
}

// opens a new mpeg2 file
// The file is positioned at the beginning
static void idle_handler_open()
{
	char req_file[STRSIZE] = { 0 };
	unsigned int req_idx = g_req_idx;
	VLDP_BOOL req_precache = g_req_precache;
	VLDP_BOOL bSuccess = VLDP_FALSE;

	SAFE_STRCPY(req_file, g_req_file, sizeof(req_file));	// after we ack the command, this string could become clobbered at any time

	// NOTE : it is very important that we change our status to BUSY before acknowledging the command, because
	//  our previous status could be STAT_ERROR, which causes problems with the *_and_block commands.
	g_out_info.status = STAT_BUSY;	// make us busy while opening the file
	// @TODO dentnz - Ack the open
	ivldp_ack_command();	// acknowledge open command

	// reset libmpeg2 so it is prepared to begin reading from a new m2v file
//	mpeg2_reset(g_mpeg_data,0);

	// if we have previously opened an mpeg, we need to close it and reset
	if (io_is_open())
	{
		io_close();

		// since the overlay is double buffered, we want to blank it 
		// twice before closing it.  This is to avoid a 'flicker effect' that we can
		// get when switching between mpeg files.
		g_in_info->render_blank_frame();
		g_in_info->render_blank_frame();

		// we need to close this surface, because the new mpeg may have a different resolution than the old one, and therefore,
		// the YUV buffer must be re-allocated
      vo_null_close();
	}

	// if we've been requested to open a real file ...
	if (!req_precache)
	{
	    printf("opening real file\n");
		bSuccess = io_open(req_file);
    }
	// else we've been requested to open a precached file...
	else
		bSuccess = io_open_precached(req_idx);

	// If file was opened successfully,
	//  check to make sure it's a video stream and also get framerate
	if (bSuccess)
	{
		uint8_t small_buf[8];
		io_read(small_buf, sizeof(small_buf));	// 1st 8 bytes reveal much
		
		// if we find the proper mpeg2 video header at the beginning of the file
		if (((small_buf[0] << 24) | (small_buf[1] << 16) | (small_buf[2] << 8) | small_buf[3]) == 0x000001B3)
		{
			g_out_info.w = (small_buf[4] << 4) | (small_buf[5] >> 4);	// get mpeg width
			g_out_info.h = ((small_buf[5] & 0x0F) << 8) | small_buf[6];	// get mpeg height
			ivldp_set_framerate(small_buf[7] & 0xF);	// set the framerate

			io_seek(0);	// go back to beginning for parser's benefit

			// load/parse all the frame locations in the file for super fast seeking
			if (ivldp_get_mpeg_frame_offsets(req_file))
			{
//				g_in_info->report_mpeg_dimensions(g_out_info.w, g_out_info.h);	// this function creates the video overlay.
				// We want to make sure we do this _after_ the frame offsets are loaded in because
				// graphics are drawn to the main screen if parsing needs to be done.
                // TODO dentnz - this header is needed for proper mpeg2 decoding
				vldp_cache_sequence_header();	// cache sequence header for faster seeking

				io_seek(0);	// seek back to beginning of file
                printf("got the file to open, and offsets loaded too. Stopping\n");
				g_out_info.status = STAT_STOPPED;	// now that the file is open, we're ready to play
			}
			else
			{
				io_close();
				fprintf(stderr, "VLDP PARSE ERROR : Is the video stream damaged?\n");
				g_out_info.status = STAT_ERROR;	// change from BUSY to ERROR
			}
		} // end if a proper mpeg header was found
		
		// if the file had a bad header
		else
		{
			io_close();
			fprintf(stderr, "VLDP ERROR : Did not find expected header.  Is this mpeg stream demultiplexed??\n");
			g_out_info.status = STAT_ERROR;
		}
	} // end if file exists
	else
	{
		fprintf(stderr, "VLDP ERROR : Could not open file!\n");
		g_out_info.status = STAT_ERROR;
	}
}

// gets called when the user wants to precache a file ...
static void idle_handler_precache(void)
{
	char req_file[STRSIZE] = { 0 };

	SAFE_STRCPY(req_file, g_req_file, sizeof(req_file));	// after we ack the command, this string could become clobbered at any time

	// always set the status before acknowledging the command so previous status doesn't get through
	g_out_info.status = STAT_BUSY;	// make us busy while opening the file
	ivldp_ack_command();

	// if we still have room in our array to precache ...
	if (s_uPreCacheIdxCount < MAX_PRECACHE_FILES)
	{
		// GET LENGTH OF ACTUAL FILE
		FILE *F = fopen(req_file, "rb");
		if (F)
		{
			struct stat filestats;
			fstat(fileno(F), &filestats);	// get stats for file to get file length
			s_sPreCacheEntries[s_uPreCacheIdxCount].uLength = filestats.st_size;
			s_sPreCacheEntries[s_uPreCacheIdxCount].uPos = 0;	// start at the beginning

			// allocate RAM to hold file ...
			s_sPreCacheEntries[s_uPreCacheIdxCount].ptrBuf = malloc(filestats.st_size);

			// if malloc succeeded
			if (s_sPreCacheEntries[s_uPreCacheIdxCount].ptrBuf)
			{
				unsigned char *u8Ptr = (unsigned char *) s_sPreCacheEntries[s_uPreCacheIdxCount].ptrBuf;
				unsigned int uTotalBytesRead = 0;
				const unsigned int READ_SIZE = 1048576;	// how many bytes to read in at a time

				g_in_info->report_parse_progress(-1);	// notify other thread that we're starting

				// load in the file ...
				for (;;)
				{
					unsigned int uBytesRead = 0;
					unsigned int uBytesToRead = READ_SIZE;
					unsigned int uBytesLeft = filestats.st_size -
						uTotalBytesRead;

					// don't overflow
					if (uBytesToRead > uBytesLeft)
                  uBytesToRead = uBytesLeft;

					uBytesRead = (unsigned int) fread(u8Ptr + uTotalBytesRead, 1, uBytesToRead, F);
					uTotalBytesRead += uBytesRead;

					// if we're done ...
					if (uTotalBytesRead >= (unsigned int) filestats.st_size)
						break;

					// update user on our precache progress
					g_in_info->report_parse_progress((double) uTotalBytesRead /
						s_sPreCacheEntries[s_uPreCacheIdxCount].uLength);
				}

				g_in_info->report_parse_progress(1);	// notify other thread that we're done ...

				// notify other thread of which index we've used to precache this file
				g_out_info.uLastCachedIndex = s_uPreCacheIdxCount;

				// we're done with this entry, so the count increases
				// (this must be done after we've read in the file so that the index is correct for that operation)
				++s_uPreCacheIdxCount;

				g_out_info.status = STAT_STOPPED;	// success
			}
			// else malloc failed
			else
				g_out_info.status = STAT_ERROR;
			fclose(F);
		}
		// else we couldn't open the file
		else
			g_out_info.status = STAT_ERROR;
	}
	// else we're out of room, so return an error
	else
		g_out_info.status = STAT_ERROR;
}

// starts playing the mpeg from the very beginning
// This is ONLY called when a file has just been opened and no seeking has taken place,
// OR if ivldp_render() has hit EOF and rewound back to the beginning
// This ASSUMES that g_mpeg_handle is at the BEGINNING of the file
// and that mpeg2_partial_init() has been called
static void idle_handler_play(void)
{
    // @TODO dentnz - Tell FPGA we are playing
	ivldp_respond_req_play();
	// when the frame is actually blitted is when we set the status to STAT_PLAYING
	// @TODO dentnz - start sending mpeg data to the FPGA
	ivldp_render();
}

// responds to play request
static void ivldp_respond_req_play(void)
{
	s_timer = g_req_timer;
	//fprintf(stderr, "ivldp_respond_req_play() : g_req_timer is %u, and uMstimer is %u\n", g_req_timer, g_in_info->uMsTimer);	// REMOVE ME
	s_uFramesShownSinceTimer = PLAY_FRAME_STALL;	// we want to render the currently shown frame for 1 frame before moving on
	g_out_info.status = STAT_PLAYING;	// we strive for instant response (and catch-up to maintain timing)
	ivldp_ack_command();	// acknowledge the play command
	s_paused = 0;	// we to not want to pause on 1 frame
	s_blanked = 0;	// we want to see the video
	s_frames_to_skip = s_frames_to_skip_with_inc = 0;	// skip no frames, just play from current position
	// this value is reset again as soon as we confirm that we are playing
}

// gets called if ivldp_got_new_command() returns true and the new command
//  is either a pause or a step
static void ivldp_respond_req_pause_or_step(void)
{
	// if they've also requested a step forward
	if ((g_req_cmdORcount & 0xF0) == VLDP_REQ_STEP_FORWARD)
		s_step_forward = 1;
	// NOTE : by design, our status should not change until paused_handler is called, so we leave it at PLAYING for now
	ivldp_ack_command();
	//printf("VLDP_REQ_PAUSED received when frame is %u, uMsTimer is %u\n", g_out_info.current_frame, g_in_info->uMsTimer);	// DBG REMOVE ME!@
	s_paused = 1;
	s_blanked = 0;
}

// gets called if ivldp_got_new_command() returns true and the new command
//  is a speed change command
static void ivldp_respond_req_speedchange(void)
{
	s_skip_per_frame = g_req_skip_per_frame;
	s_stall_per_frame = g_req_stall_per_frame;
	ivldp_ack_command();
}

// displays 1 or more frames to the screen, according to the state variables.
// This function can be used to do both still frames and moving video.  Play and search both use this function.
static void ivldp_render(void)
{
   uint8_t *end = NULL;
   int render_finished = 0;

   s_skip_all = 0;	// default value, don't skip frames unless the play or paused handler orders it

   //printf("s_skip_all skipped %u frames.\n", s_uSkipAllCount);

   // check to make sure a file has been opened
   if (!io_is_open())
   {
      render_finished = 1;
      fprintf(stderr, "VLDP RENDER ERROR : we tried to render an mpeg but none was open!\n");
      g_out_info.status = STAT_ERROR;
   }

   // while we're not finished playing and pausing		
   while (!render_finished)
   {
      //		end = g_buffer + fread (g_buffer, 1, BUFFER_SIZE, g_mpeg_handle);
      end = g_buffer + io_read(g_buffer, BUFFER_SIZE);

      // safety check, they could be equal if we were already at EOF before we tried this
      // read chunk of video stream
//      if (g_buffer != end)
//         decode_mpeg2 (g_buffer, end);	// display it to the screen

      // if we've read to the end of the mpeg2 file, then we can't play anymore, so we pause on last frame
      if (end != (g_buffer + BUFFER_SIZE))
      {
         g_out_info.status = STAT_STOPPED;	// it's a toss-up between this and STAT_PAUSED
         render_finished = 1;

         // reset libmpeg2 so it is prepared to begin reading from the beginning of the file
//         mpeg2_reset(g_mpeg_data,0);
         io_seek(0);	// seek to the beginning of the file
         g_out_info.current_frame = 0;	// set frame # to beginning of file where it belongs
      }

      // if a new command is coming in, check to see if we need to stop
      if (ivldp_got_new_command())
      {
         // check to see if we need to suddenly abort the rendering process
         switch (g_req_cmdORcount & 0xF0)
         {
            case VLDP_REQ_QUIT:
            case VLDP_REQ_OPEN:
            case VLDP_REQ_SEARCH:
            case VLDP_REQ_STOP:
               g_out_info.status = STAT_BUSY;
               render_finished = 1;
               break;
            case VLDP_REQ_SKIP:
               // do not change the playing status because skips are supposed to be instant
               render_finished = 1;
               break;
         } // end switch
      } // end if they got a new command
   } // end while
}

// searches to any arbitrary frame, be it I, P, or B, and renders it
// if skip is set, it will do a laserdisc skip instead of a search (ie it will go a frame, resume playback,
// and not adjust any timers)
static void idle_handler_search(int skip)
{
	uint32_t proposed_pos = 0;
	uint16_t req_frame = g_req_frame; // after we acknowledge the command, g_req_frame could become clobbered
	uint32_t min_seek_ms = g_req_min_seek_ms;	// g_req_min_seek_ms can be clobbered at any time after we acknowledge command

	// adjusted req frame is the requested frame with fields taken into account
	unsigned int uAdjustedReqFrame = 0;

	unsigned int actual_frame = 0;
	int skipped_I = 0;

	// status must be changed before acknowledging command, because previous status could be STAT_ERROR, which
	//  causes problems with *_and_block vldp API commands.
	if (!skip)
      g_out_info.status = STAT_BUSY;
	else
	{
      // else we're skipping
      // (our status is already STAT_PLAYING so we don't need to set it)
		// Since we're skipping, we must re-calculate s_uFramesShownSinceTimer because it is possible on laggy systems for
		//  this value to be behind what it ought to be, and we need to be perfectly in sync with the parent thread's
		//  g_in_info->uMsTimer value in order to display the correct frames.
		// NOTE : this _must_ happen before we acknowledge the command, because we need g_in_info->uMsTimer to be constant while
		//  we recalculate s_uFramesShownSinceTimer
		// NOTE 2 : we must use uint64_t here because otherwise this will overflow sometime after 2 minutes which DOES happen
		//  on Astron Belt if 'infinite timer' is enabled (it doesn't do any searches, just skips)
		unsigned int uNewFramesShownSinceTimer = (unsigned int) ((((uint64_t) (g_in_info->uMsTimer - s_timer)) * g_out_info.uFpks) / 1000000);

		// take into account the extra frame we did when playback started
		uNewFramesShownSinceTimer += PLAY_FRAME_STALL;

		s_uFramesShownSinceTimer = uNewFramesShownSinceTimer;
	}

	ivldp_ack_command();	// acknowledge search/skip command

	// reset libmpeg2 so it is prepared to start from a new spot
//	mpeg2_reset(g_mpeg_data,0);

    // @todo - dentnz - here we need to send the sequence header to the fpga
	vldp_process_sequence_header();	// we need to process the sequence header before we can jump around the file for frames

	// if we're doing a search...
	if (!skip)
	{
		s_paused = 1;	// we do want to pause on the frame we search to

		//s_timer = g_in_info->uMsTimer;	// reset timer so framerate is correct
		// NOTE : resetting s_timer here doesn't do much if we aren't simulating
		// artificial seek delay, because paused_handler() will reset it again anyway.
		// (but it is important for seek delay!)

		s_uFramesShownSinceTimer = 0;
		// NOTE : by setting this to 0, we are eliminating any potential unnecessary
		// delays before paused_handler() gets called, which is what we want.

		s_extra_delay_ms = min_seek_ms;	// any extra delay we have will be when the parent thread requested (this can be 0)

		// if we are to blank during searches ...
//		if (g_in_info->blank_during_searches)
//			g_in_info->render_blank_frame();
	}

	// if we are skipping we are actually in the middle of playback so we don't reset the timer
	else
	{
		// NOTE : we don't want to change the status here because
		// skipping is supposed to be instantaneous

		s_paused = 0;

		// if we need to blank frame for skipping
		if (g_in_info->blank_during_skips)
			g_in_info->render_blank_frame();
	}

	// adjusted req frame is the requested frame with fields taken into account
	uAdjustedReqFrame = req_frame;

	// if we're using fields, then the requested frame must be doubled (2 fields per frame)
	if (g_out_info.uses_fields) uAdjustedReqFrame <<= 1;

	actual_frame = uAdjustedReqFrame;

	// do a bounds check
	if (uAdjustedReqFrame < g_totalframes)
	{
		proposed_pos = g_frame_position[uAdjustedReqFrame];	// get the proposed position

		s_frames_to_skip = s_frames_to_skip_with_inc = 0;	// the below problem is no longer a problem

		// loop until we find which position in the file to seek to
		for (;;)
      {
         // if the frame we want is not an I frame, go backward until we find an I frame, and increase # of frames to skip forward
         while ((proposed_pos == 0xFFFFFFFF) && (actual_frame > 0))
         {
            s_frames_to_skip++;
            actual_frame--;
            proposed_pos = g_frame_position[actual_frame];
         }
         skipped_I++;

         // if we are only 2 frames away from an I frame, we will get a corrupted image and need to go back to
         // the I frame before this one
         if ((skipped_I < 2) && (s_frames_to_skip < 3) && (actual_frame > 0))
            proposed_pos = 0xFFFFFFFF;
         else
         {
            printf("We've decided on a position within the file.\n");
            printf("skipped_I is %d\n", skipped_I);
            printf("s_frames_to_skip is %d\n", s_frames_to_skip);
            printf("actual_frame is %d\n", actual_frame);
            break;
         }
      }

		printf("frames_to_skip is %d, skipped_I is %d\n", s_frames_to_skip, skipped_I);
		printf("position in mpeg2 stream we are seeking to : %x\n", proposed_pos);

		io_seek(proposed_pos);
//		fseek(g_mpeg_handle, proposed_pos, SEEK_SET);	// go to the place in the stream where the I frame begins

		// if we're seeking, we can change the frame right now ...
		if (!skip)
		{
			g_out_info.current_frame = req_frame;	// this is no longer incremented in null_draw_frame due to s_paused being set
			s_uPendingSkipFrame = 0;
		}
		// if we're skipping, we have to leave the current frame alone until it changes, in order
		//  to be consistent with actual laserdisc behavior.
		else
			s_uPendingSkipFrame = req_frame;

		s_blanked = 0;	// we want to see the frame

		ivldp_render();
	} // end if the bounds check passed
	else
	{
		fprintf(stderr, "SEARCH ERROR : frame %u was requested, but it is out of bounds\n", req_frame);
		g_out_info.status = STAT_ERROR;
	}
}

// parses an mpeg video stream to get its frame offsets, or if the parsing had taken place earlier
static VLDP_BOOL ivldp_get_mpeg_frame_offsets(char *mpeg_name)
{
	struct dat_header header;
	char datafilename[320]       = { 0 };
	VLDP_BOOL mpeg_datafile_good = VLDP_FALSE;
	FILE *data_file              = NULL;
	VLDP_BOOL result             = VLDP_TRUE;
	unsigned int mpeg_size       = io_length();

	// change extension of file to be dat instead of (presumably) m2v
	SAFE_STRCPY(datafilename, mpeg_name, sizeof(datafilename));
	strcpy(&datafilename[strlen(mpeg_name)-3], "dat");

	// loop until we get a good datafile
	// or until we get an error
	while (!mpeg_datafile_good && result)
	{
		data_file = fopen(datafilename, "rb");	// check to see if datafile exists

		// If file cannot be opened, try to create it.
		// Most likely the file cannot be opened because it doesn't
		// exist.
      // we could open the file here, but there is no need to
      // because we will loop back through and open the file anyway			
		if (!data_file)
			result = ivldp_parse_mpeg_frame_offsets(datafilename, mpeg_size);
		// else if the file has been opened
		else
		{
			// now that file exists and we have it open, we have to read it

			fseek(data_file, 0L, SEEK_SET);
			fread(&header, sizeof(header), 1, data_file);	// read .DAT file header

			// if version, file size, or finished are wrong, the dat file is no good and has to be regenerated
			if ((header.length != mpeg_size) || (header.version != DAT_VERSION) || (header.finished != 1))
			{
				printf("*** Alleged mpeg size is %u, actual size is %u\n", header.length, mpeg_size);
				printf("Finished flag is %x\n", header.finished);
				printf("DAT version is %x\n", header.version);
				printf("NOTICE : MPEG data file has to be created again!\n");
				fclose(data_file);
				data_file = NULL;
				
				// try to delete obsolete .DAT file so we can create a modern one
				if (remove(datafilename) == -1)
				{
					fprintf(stderr, "Couldn't delete obsolete .DAT file!\n");
					result = VLDP_FALSE;
				}
			}
			else
			{
				g_out_info.uses_fields = header.uses_fields;
				mpeg_datafile_good = 1;	// escape the loop
			}
		} // end if mpeg parsing was not necessary
	} // end while we don't have a good datafile and haven't gotten an error
	
	// if we didn't exit the loop because of an error, then we need to read the datafile
	if (result && data_file)
	{
		g_totalframes = 0;

		// read all the frame positions
		// if we don't read 4 bytes, it means we've hit the EOF and we're done
		while (fread(&g_frame_position[g_totalframes], 4, 1, data_file) == 1)
		{
			g_totalframes++;
			
			// safety check, it is possible to make mpegs with too many frames to fit onto one CAV laserdisc
			// (in fact I did this, and it caused a lot of problems in the debug stages hehe)
			if (g_totalframes >= MAX_LDP_FRAMES)
			{
				fprintf(stderr, "ERROR : current mpeg has too many frames, VLDP will ignore any frames above %u\n", MAX_LDP_FRAMES);
				break;
			}
		}
		//printf("*** g_totalframes is %u\n", g_totalframes);
		//printf("And frame 0's offset is %x\n", g_frame_position[0]);
	}

	// close any files that are still open
	if (data_file)
		fclose(data_file);
	
	return result;
}


static VLDP_BOOL ivldp_parse_mpeg_frame_offsets(char *datafilename,
      uint32_t mpeg_size)
{
	FILE *data_file = fopen(datafilename, "wb");	// create file
	struct dat_header header;	// header to put inside .DAT file

	// if we could create the file successfully, then we need to populate it
	if (data_file)
	{
		uint32_t pos = 0;	// position in the file
		int count = 0;
		int parse_result = 0;

		header.version = DAT_VERSION;
		header.finished = 0;
		header.uses_fields = 0;
		header.length = mpeg_size;
		fwrite(&header, sizeof(header), 1, data_file);
		// first thing that goes in the file is the .DAT header
		// That way we can re-use the file another time with confidence that it's
		// the right one

		init_mpegscan();

		g_in_info->report_parse_progress(-1);	// notify other thread that we're starting

		// keep reading the file while there is a file left to be read
		do
		{
#define PARSE_CHUNK 200000

			parse_result = parse_video_stream(data_file, PARSE_CHUNK);
			pos += PARSE_CHUNK;

			// we want to give the user updates but don't want to flood them
			if (count > 10)
			{
				count = 0;
				
				// report progress to parent thread
				g_in_info->report_parse_progress((double) pos / mpeg_size);
			}
			count++;

		} while (parse_result == P_IN_PROGRESS);

		g_in_info->report_parse_progress(1);	// notify other thread that we're done

		// if parse finished, then we have to update the header
		if (parse_result != P_ERROR)
		{
			header.finished = 1;
			header.uses_fields = 0;
			if (parse_result == P_FINISHED_FIELDS)
				header.uses_fields = 1;
			fseek(data_file, 0L, SEEK_SET);
			fwrite(&header, sizeof(header), 1, data_file);	// save changes
		}

		// we have to close data file because it's write-only
		// and it needs to be re-opened read-only
		fclose(data_file);
		data_file = NULL;

		// if the mpeg did not finish parsing gracefully, we've got problems
		// NOTE : I separated this from the other if above to guarantee that the file gets closed
		if (parse_result == P_ERROR)
		{
			fprintf(stderr, "There was an error parsing the MPEG file.\n");
			fprintf(stderr, "Either there is a bug in the parser or the MPEG file is corrupt.\n");
			fprintf(stderr, "OR the user aborted the decoding process :)\n");
			remove(datafilename);
			return VLDP_FALSE;
		}
	} // end if we could create the file successfully

	// we couldn't create data file which means no write permission probably
	// this is probably a good time to shut VLDP down =]
	else
	{
		fprintf(stderr, "Could not create file %s\n", datafilename);
		fprintf(stderr, "This probably means you don't have permission to create the file\n");
		return VLDP_FALSE;
	}

	return VLDP_TRUE;
}

static VLDP_BOOL io_open(const char *cpszFilename)
{
	// make sure everything is closed
	if ((!s_bPreCacheEnabled) && (!g_mpeg_handle))
	{
		g_mpeg_handle = fopen(cpszFilename, "rb");
		if (g_mpeg_handle)
         return VLDP_TRUE;
	}
	return VLDP_FALSE;
}

static VLDP_BOOL io_open_precached(unsigned int uIdx)
{
	// make sure everything is closed
	if ((!g_mpeg_handle) && (!s_bPreCacheEnabled))
	{
		// make sure index is within range ...
		if (uIdx < s_uPreCacheIdxCount)
		{
			s_uCurPreCacheIdx = uIdx;
			s_bPreCacheEnabled = VLDP_TRUE;
			s_sPreCacheEntries[s_uCurPreCacheIdx].uPos = 0;	// when opening, rewind to beginning
			return VLDP_TRUE;
		}
		// else out of range ...
	}
	return VLDP_FALSE;	
}

static unsigned int io_read(void *buf, unsigned int uBytesToRead)
{
	unsigned int uBytesRead = 0;

	// if we're reading from a file stream
	if (g_mpeg_handle)
		uBytesRead = (unsigned int) fread(buf, 1, uBytesToRead, g_mpeg_handle);
	else
	{
      // else we're reading from a precache stream
		struct precache_entry_s *entry = &s_sPreCacheEntries[s_uCurPreCacheIdx];
		unsigned int uBytesLeft = entry->uLength - entry->uPos;

		// if we're trying to read beyond our means ...
		if (uBytesToRead > uBytesLeft)
			uBytesToRead = uBytesLeft;

		memcpy(buf, ((unsigned char *) entry->ptrBuf) + entry->uPos, uBytesToRead);
		uBytesRead = uBytesToRead;
		entry->uPos += uBytesRead;
	}

	return uBytesRead;
}

VLDP_BOOL io_seek(unsigned int uPos)
{
	if (g_mpeg_handle)
	{
		if (fseek(g_mpeg_handle, uPos, SEEK_SET) == 0)
			return VLDP_TRUE;
	}
	else
	{
		struct precache_entry_s *entry = &s_sPreCacheEntries[s_uCurPreCacheIdx];

		// if we're seeking within bounds ...
		if (uPos < entry->uLength)
		{
			entry->uPos = uPos;
			return VLDP_TRUE;
		}
	}
	return VLDP_FALSE;
}

static void io_close(void)
{
	if (g_mpeg_handle)
	{
		fclose(g_mpeg_handle);
		g_mpeg_handle = NULL;
	}
	else if (s_bPreCacheEnabled)
		s_bPreCacheEnabled = VLDP_FALSE;

	// else nothing is open ...
}

static VLDP_BOOL io_is_open(void)
{
	if ((g_mpeg_handle) || (s_bPreCacheEnabled))
		return VLDP_TRUE;
	return VLDP_FALSE;
}

static unsigned int io_length(void)
{
	if (g_mpeg_handle)
	{
		struct stat the_stat;
		fstat(fileno(g_mpeg_handle), &the_stat);
		return the_stat.st_size;
	}
	else if (s_bPreCacheEnabled)
		return s_sPreCacheEntries[s_uCurPreCacheIdx].uLength;

	return 0;
}
