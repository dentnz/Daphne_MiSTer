#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <set>
#include "mpo_fileio.h"
#include "mpo_mem.h"
#include "numstr.h"
#include "ldp-vldp.h"

//#include "framemod.h"
//#include "../vldp2/vldp/vldp.h"	// to get the vldp structs

// functions and state information provided to the parent thread from VLDP
struct vldp_out_info
{
	// shuts down VLDP, de-allocates any memory that was allocated, etc ...
	void (*shutdown)();

	// All of the following functions return 1 on success, 0 on failure.

	// open an mpeg file and returns immediately.  File won't actually be open until
	// 'status' is equal to STAT_STOPPED.
	int (*open)(const char *filename);

	// like 'open' except it blocks until 'status' is equal to STAT_STOPPED (until all potential parsing has finished, etc)
	// does not return on STAT_BUSY
	// returns 1 if status is STAT_STOPPED or 0 if status ended up to be something else
	int (*open_and_block)(const char *filename);

	// Precaches the indicated file to memory, so that when 'open' is called, the file will
	//  be read from memory instead of the filesystem (for increased speed).
	// IMPORTANT : file won't be precached until status == STAT_STOPPED
	// Returns VLDP_TRUE if the precaching succeeded or VLDP_FALSE on failure.
	VLDP_BOOL (*precache)(const char *filename);

	// Instructus VLDP to 'open' a file that has been precached.  It is referred to
	//  by its precache index instead of a filename.  Behavior is similar to 'open'.
	VLDP_BOOL (*open_precached)(unsigned int uIdx, const char *filename);

	// plays the mpeg that has been previously open.  'timer' is the value relative to uMsTimer that
	// we should use for the beginning of the first frame that will be displayed
	// returns 0 on failure, 1 on success, 2 on busy
	int (*play)(uint32_t timer);

	// searches to a frame relative to the beginning of the mpeg (0 is the first frame)
	// 'min_seek_ms' is the minimum # of milliseconds that this seek must take
	//  (for the purpose of simulating laserdisc seek delay)
	// returns immediately, but search is not complete until 'status' is STAT_PAUSED
	// returns 1 if command was acknowledged, or 0 if we timed out w/o getting acknowlegement
	int (*search)(uint16_t frame, uint32_t min_seek_ms);

	// like search except it blocks until the search is complete
	// 'min_seek_ms' is the minimum # of milliseconds that this seek must take
	//  (for the purpose of simulating laserdisc seek delay)
	// returns 1 if search succeeded, 2 if search is still going, 0 if search failed
	// (so does not do true blocking, we could change this later)
	int (*search_and_block)(uint16_t frame, uint32_t min_seek_ms);

	// skips to 'frame' and immediately begins playing.
	// the mpeg is required to be playing before skip is called, because we accept no new timer as reference
	int (*skip)(uint16_t frame);

	// pauses mpeg playback
	int (*pause)();

	// steps one frame forward while paused
	int (*step_forward)();

	// stops playback
	int (*stop)();

	// Changes the speed of playback (while still maintaining framerate)
	// For example, to get 2X, uSkipPerFrame = 1, uStallPerFrame = 0
	// To get 3X,  uSkipPerFrame = 2, uStallPerFrame = 0
	// To get 1/2X, uSkipPerFrame = 0, uStallPerFrame = 1
	VLDP_BOOL (*speedchange)(unsigned int uSkipPerFrame, unsigned int uStallPerFrame);

	// Attempts to make sure the VLDP sits idly (mainly to prevent it from calling any callbacks)
	// Returns true if VLDP has been locked, or false if we timed out
	VLDP_BOOL (*lock)(unsigned int uTimeoutMs);

	// Unlocks a previous lock operation. Returns true if unlock was successful, or false if we timed out.
	VLDP_BOOL (*unlock)(unsigned int uTimeoutMs);

	////////////////////////////////////////////////////////////

	// State information for the parent thread's benefit
	unsigned int uFpks;	// FPKS = frames per kilosecond (FPS = uFpks / 1000.0)
	unsigned int u2milDivFpks; // (2000000) / uFpks (pre-calculated, used to determine whether to drop frames)
	uint8_t uses_fields;	// whether the video uses fields or not
	uint32_t w;	// width of the mpeg video
	uint32_t h;	// height of the mpeg video
	int status;	// the current status of the VLDP (see STAT_ enum's)
	unsigned int current_frame;	// the current frame of the opened mpeg that we are on
	unsigned int uLastCachedIndex;	// the index of the file that was last precached (if any)
};

/////////////////////////////////////////

// pointer to all functions the VLDP exposes to us ...
const struct vldp_out_info *g_vldp_info = NULL;

//// info that we provide to the VLDP DLL
//struct vldp_in_info g_local_info;

/////////////////////////////////////////

ldp_vldp::ldp_vldp()
{
	m_bIsVLDP = true;	// this is VLDP, so this value is true ...
	blitting_allowed = true;	// blitting is allowed until we get to a certain point in initialization
	m_target_mpegframe = 0;	// mpeg frame # we are seeking to
	m_mpeg_path = "";
	m_cur_mpeg_filename = "";
	m_file_index = 0; // # of mpeg files in our list
	//m_framefile = g_game->get_shortgamename() + m_framefile;	// create a sensible default framefile name
	m_framefile = "lair.txt";
	m_bFramefileSet = false;
	m_altaudio_suffix = "";	// no alternate audio by default
	m_audio_file_opened = false;
	m_cur_ldframe_offset = 0;
	m_blank_on_searches = false;
	m_blank_on_skips = false;

	// RJS HERE
	m_seek_frames_per_ms = 0;
	m_min_seek_delay = 0;

	m_vertical_stretch = 0;

	m_bPreCache = m_bPreCacheForce = false;
	m_mPreCachedFiles.clear();

	m_uSoundChipID = 0;

//	enable_audio1();	// both channels should be on by default
//	enable_audio2();
}

ldp_vldp::~ldp_vldp()
{
	pre_shutdown();
}

// called when daphne starts up
bool ldp_vldp::init_player()
{
	bool result = false;
	bool need_to_parse = false;	// whether we need to parse all video

   // try to read in the framefile
   if (read_frame_conversions())
   {
      // just a sanity check to make sure their frame file is correct
      if (first_video_file_exists())
      {				
         // if the last video file has not been parsed, assume none of them have been
         // This is safe because if they have been parsed, it will just skip them
         if (!last_video_file_parsed())
         {
            printf("Press any key to parse your video file(s). This may take a while. Press ESC if you'd rather quit.");
            need_to_parse = true;
         }

//         if (audio_init())// && !get_quitflag())
//         {
            // if our game is using video overlay,
            // AND if we're not doing tests that an overlay would interfere with
            // we'll use our slower callback

            // 201x.xx.xx - RJS - since the change of moving the LDP thread to start earlier, the video overlays were not
            // initialized (video_init) yet, however this flag can be
            //if (g_game->does_game_use_video_overlay())
            //   g_local_info.prepare_frame = prepare_frame_callback_with_overlay;
            //else
            //{
               // otherwise we can draw the frame much faster w/o worrying about
               // video overlay
            //   g_local_info.prepare_frame = prepare_frame_callback_without_overlay;
            //}
//            g_local_info.display_frame = display_frame_callback;
//            g_local_info.report_parse_progress = report_parse_progress_callback;
//            g_local_info.report_mpeg_dimensions = report_mpeg_dimensions_callback;
//            g_local_info.render_blank_frame = blank_overlay;
//            g_local_info.blank_during_searches = m_blank_on_searches;
//            g_local_info.blank_during_skips = m_blank_on_skips;
//            g_local_info.GetTicksFunc = GetTicksFunc;

//            g_vldp_info = vldp_init(&g_local_info);

//            // if we successfully made contact with VLDP ...
//            if (g_vldp_info != NULL)
//            {
//               // this number is used repeatedly, so we calculate it once
//               g_vertical_offset = g_game->get_video_row_offset();
//
//               // bPreCacheOK will be true if precaching succeeds or is never attempted
//               bool bPreCacheOK = true;
//
//               // If precaching has been requested, do it now.
//               // The check for RAM requirements is done inside the
//               //  precache_all_video function, so we don't need to worry about that here.
//               if (m_bPreCache)
//                  bPreCacheOK = precache_all_video();
//
//               // if we need to parse all the video
//               if (need_to_parse)
//                  parse_all_video();
//
//               // if precaching succeeded or we didn't request precaching
//               if (bPreCacheOK)
//               {
//                  blitting_allowed = false;	// this is the point where blitting isn't allowed anymore
//
//                  // open first file so that
//                  // we can draw video overlay even if the disc is not playing
//                  printline("LDP-VLDP INFO : opening video file . . .");
//                  printline(m_mpeginfo[0].name.c_str());
//                  if (open_and_block(m_mpeginfo[0].name))
//                  {
//                     // although we just opened a video file, we have not opened an audio file,
//                     // so we want to force a re-open of the same video file when we do a real search,
//                     // in order to ensure that the audio file is opened also.
//                     m_cur_mpeg_filename = "";
//
//                     // set MPEG size ASAP in case different from NTSC default
//                     m_discvideo_width = g_vldp_info->w;
//                     m_discvideo_height = g_vldp_info->h;
//
//                     if (is_sound_enabled())
//                     {
//                        struct sounddef soundchip;
//                        soundchip.type = SOUNDCHIP_VLDP;
//                        // no further parameters necessary
//                        m_uSoundChipID = add_soundchip(&soundchip);
//                     }
//
//                     result = true;
//                  }
//                  else
//                     printline("LDP-VLDP ERROR : first video file could not be opened!");
//               } // end if it's ok to proceed
//               // else precaching failed
//               else
//                  printerror("LDP-VLDP ERROR : precaching failed");
//
//            } // end if reading the frame conversion file worked
//            else
//               printline("LDP-VLDP ERROR : vldp_init returned NULL (which shouldn't ever happen)");
//         } // if audio init succeeded
//         else
//         {
//            // only report an audio problem if there is one
//            if (!get_quitflag())
//               printline("Could not initialize VLDP audio!");
//            // otherwise report that they quit
//            else
//               printline("VLDP : Quit requested, shutting down!");
//         } // end if audio init failed or if user opted to quit instead of parse
      } // end if first file was present (sanity check)
//      // else if first file was not found, we do just quit because an error is printed elsewhere
   } // end if framefile was read in properly
   else
   {
      // if the user didn't specify a framefile from the command-line, then give them a little hint.
      if (!m_bFramefileSet)
         printf("You must specify a -framefile argument when using VLDP.");
      // else the other error messages are more than sufficient
   }
	
	// if init didn't completely finish, then we need to shutdown everything
	if (!result)
		shutdown_player();

	return result;
}

void ldp_vldp::shutdown_player()
{
	// if VLDP has been loaded
	if (g_vldp_info)
	{
		g_vldp_info->shutdown();
		g_vldp_info = NULL;
	}
	
//	if (is_sound_enabled())
//	{
//		if (!delete_soundchip(m_uSoundChipID))
//			printf("ldp_vldp::shutdown_player WARNING : sound chip could not be deleted");
//	}
//	audio_shutdown();
//	free_yuv_overlay();	// de-allocate overlay if we have one allocated ...
}

bool ldp_vldp::open_and_block(const string &strFilename)
{
	bool bResult = false;

	// during parsing, blitting is allowed
	// NOTE: we want this here so that it is true before the initial open command is called.
	//  Otherwise we'd put it inside wait_for_status ...
	blitting_allowed = true;

	// see if this filename has been precached
	map<string, unsigned int>::const_iterator mi = m_mPreCachedFiles.find(strFilename);

	// if the file has not been precached and we are able to open it
	if ((mi == m_mPreCachedFiles.end() &&
			(g_vldp_info->open((m_mpeg_path + strFilename).c_str())))
			// OR if the file has been precached and we are able to refer to it
		|| (g_vldp_info->open_precached(mi->second, (m_mpeg_path + strFilename).c_str())))
	{
		bResult = wait_for_status(STAT_STOPPED);
		if (bResult)
			m_cur_mpeg_filename = strFilename;
	}

	blitting_allowed = false;

	return bResult;
}

bool ldp_vldp::precache_and_block(const string &strFilename)
{
	bool bResult = false;

	// during precaching, blitting is allowed
	// NOTE: we want this here so that it is true before the initial open command is called.
	//  Otherwise we'd put it inside wait_for_status ...
	blitting_allowed = true;

	if (g_vldp_info->precache((m_mpeg_path + strFilename).c_str()))
		bResult = wait_for_status(STAT_STOPPED);

	blitting_allowed = false;

	return bResult;
}

bool ldp_vldp::wait_for_status(unsigned int uStatus)
{
//	while (g_vldp_info->status == STAT_BUSY)
//	{
//		// if we got a parse update, then show it ...
//		if (g_bGotParseUpdate)
//		{
//			// redraw screen blitter before we display it
//			update_parse_meter();
//			vid_blank();
//			vid_blit(get_screen_blitter(), 0, 0);
//			vid_flip();
//			g_bGotParseUpdate = false;
//		}
//
//		SDL_check_input();	// so that windows events are handled
//		make_delay(20);	// be nice to CPU
//	}

	// if opening succeeded
	if ((unsigned int) g_vldp_info->status == uStatus)
		return true;

	return false;
}

bool ldp_vldp::nonblocking_search(char *frame)
{
	
	bool result = false;
	string filename = "";
	string oggname = "";
	uint16_t target_ld_frame = (uint16_t) atoi(frame);
	uint64_t u64AudioTargetPos = 0;	// position in audio to seek to (in samples)
	unsigned int seek_delay_ms = 0;	// how many ms this seek must be delayed (to simulate laserdisc lag)
	//audio_pause();	// pause the audio before we seek so we don't have overrun
	
	// do we need to compute seek_delay_ms?
	// (This is best done sooner than later so get_current_frame() is more accurate
	if (m_seek_frames_per_ms > 0)
	{
		// this value should be approximately the last frame we displayed
		// it doesn't get changed to the new frame until the seek is complete
		uint16_t cur_frame = get_current_frame();
		unsigned int frame_delta = 0;	// how many frames we're skipping
		
		// if we're seeking forward
		if (target_ld_frame > cur_frame)
			frame_delta = target_ld_frame - cur_frame;
		else
			frame_delta = cur_frame - target_ld_frame;
		
		seek_delay_ms = (unsigned int) (frame_delta / m_seek_frames_per_ms);

#ifdef DEBUG
		string msg = "frame_delta is ";
		//msg += numstr::ToStr(frame_delta);
		msg += " and seek_delay_ms is ";
		//msg += numstr::ToStr(seek_delay_ms);
		printf(msg.c_str());
#endif
		
	}

	// make sure our seek delay does not fall below our minimum
	if (seek_delay_ms < m_min_seek_delay) seek_delay_ms = m_min_seek_delay;

	m_target_mpegframe = mpeg_info(filename, target_ld_frame); // try to get a filename

	// if we can convert target frame into a filename, do it!
	if (filename != "")
	{
		result = true;	// now we assume success unless we fail
		
		// if the file to be opened is different from the one we have opened
		// OR if we don't yet have a file open ...
		// THEN open the file! :)
		if (filename != m_cur_mpeg_filename)
		{
			// if we were able to successfully open the video file
			if (open_and_block(filename))
			{
				result = true;

				// this is done inside open_and_block now ...
				//m_cur_mpeg_filename = filename; // make video file our new current file
				
				// if sound is enabled, try to open an audio stream to match the video stream
//				if (is_sound_enabled())
//				{
//					// try to open an optional audio file to go along with video
//					oggize_path(oggname, filename);
//					m_audio_file_opened = open_audio_stream(oggname.c_str());
//				}
			}
			else
			{
				printf("LDP-VLDP.CPP : Could not open video file ");
				//printline(filename.c_str());
				result = false;
			}
		}

		// if we're ok so far, try the search
		if (result)
		{
			// IMPORTANT : 'uFPKS' _must_ be queried AFTER a new mpeg has been opened,
			//  because sometimes a framefile can include mpegs that have different framerates
			//  from each other.
			unsigned int uFPKS = g_vldp_info->uFpks;

			m_discvideo_width = g_vldp_info->w;
			m_discvideo_height = g_vldp_info->h;

			// IMPORTANT : this must come before the optional FPS adjustment takes place!!!
//			u64AudioTargetPos = get_audio_sample_position(m_target_mpegframe);

			//if (!need_frame_conversion())
			//{
				// If the mpeg's FPS and the disc's FPS differ, we need to adjust the mpeg frame
				// NOTE: AVOID this if you can because it makes seeking less accurate
//				if (g_game->get_disc_fpks() != uFPKS)
//				{
//					string s = "NOTE: converting FPKS from " + numstr::ToStr(g_game->get_disc_fpks()) +
//						" to " + numstr::ToStr(uFPKS) + ". This may be less accurate.";
//					printf(s.c_str());
//					m_target_mpegframe = (m_target_mpegframe * uFPKS) / g_game->get_disc_fpks();
//				}
			//}

			// try to search to the requested frame
			if (g_vldp_info->search((uint16_t) m_target_mpegframe, seek_delay_ms))
			{
				// if we have an audio file opened, do an audio seek also
//				if (m_audio_file_opened)
//					result = seek_audio(u64AudioTargetPos);
			}
			else
				printf("LDP-VLDP.CPP : Search failed in video file");
		}
		// else opening the file failed
	}
	// else mpeg_info() wasn't able to provide a filename ...
	else
	{
		printf("LDP-VLDP.CPP ERROR: frame could not be converted to file, probably due to a framefile error.");
		//outstr("Your framefile must begin no later than frame ");
		//printf(frame);
		printf("This most likely is your problem!");
	}
	
	return(result);
}

// it should be safe to assume that if this function is getting called, that we have not yet got a result from the search
int ldp_vldp::get_search_result()
{
	// if search is finished and has succeeded
	if (g_vldp_info->status == STAT_PAUSED)
		return SEARCH_SUCCESS;
	
	// if the search failed
	else if (g_vldp_info->status == STAT_ERROR)
		return SEARCH_FAIL;
	
	// else it's busy so we just wait ...
	return SEARCH_BUSY;
}

unsigned int ldp_vldp::play()
{
	unsigned int result = 0;
	string ogg_path = "";
	bool bOK = true;	// whether it's ok to issue the play command
	
	// if we haven't opened any mpeg file yet, then do so now
	if (m_cur_mpeg_filename == "")
	{
		bOK = open_and_block(m_mpeginfo[0].name);	// get the first mpeg available in our list
		if (bOK)
		{
			// this is done inside open_and_block now ...
			
			// if sound is enabled, try to load an audio stream to go with video stream ...
//			if (is_sound_enabled())
//			{
//				// try to open an optional audio file to go along with video
//				oggize_path(ogg_path, m_mpeginfo[0].name);
//				m_audio_file_opened = open_audio_stream(ogg_path.c_str());
//			}
			
		}
		else
		{
//			outstr("LDP-VLDP.CPP : in play() function, could not open mpeg file ");
//			printf(m_mpeginfo[0].name.c_str());
		}
	} // end if we haven't opened a file yet
	
	// we need to keep this separate in case an mpeg is already opened
//	if (bOK)
//	{
//		audio_play(0);
//		if (g_vldp_info->play(0))
//			result = GET_TICKS();
//	}

	if (!result)
		printf("VLDP ERROR : play command failed!");
	
	return result;
}

// skips forward a certain # of frames during playback without pausing
// Caveats: Does not work with an mpeg of the wrong framerate, does not work with an mpeg
//  that uses fields, and does not skip across file boundaries.
// Returns true if skip was successful
bool ldp_vldp::skip_forward(uint16_t frames_to_skip, uint16_t target_frame)
{
	bool result = false;
	
	target_frame = (uint16_t) (target_frame - m_cur_ldframe_offset);	// take offset into account
	// this is ok (and possible) because we don't support skipping across files

	unsigned int uFPKS = g_vldp_info->uFpks;
	unsigned int uDiscFPKS = 23.976; //= g_game->get_disc_fpks();

	// We don't support skipping on mpegs that differ from the disc framerate
	if (uDiscFPKS == uFPKS)
	{
		// make sure they're not using fields
		// UPDATE : I don't see any reason why using fields would be a problem anymore,
		//  but since I am not aware of any mpegs that use fields that require skipping,
		//  I am leaving this restriction in here just to be safe.
		if (!g_vldp_info->uses_fields)
		{
			// advantage to this method is no matter how many times we skip, we won't drift because we are using m_play_time as our base
			// if we have an audio file opened
			if (m_audio_file_opened)
			{
				//uint64_t u64AudioTargetPos = (((uint64_t) target_frame) * FREQ1000) / uDiscFPKS;
				//uint64_t u64AudioTargetPos = get_audio_sample_position(target_frame);
	
				// seek and play if seeking was successful
//				if (seek_audio(u64AudioTargetPos))
//					audio_play(m_uElapsedMsSincePlay);
			}
			// else we have no audio file open, but that's ok ...
	
			// if VLDP was able to skip successfully
			if (g_vldp_info->skip(target_frame))
				result = true;
			else
				printf("LDP-VLDP ERROR : video skip failed");
		}
		else
			printf("LDP-VLDP ERROR : Skipping not supported with mpegs that use fields (such as this one)");
	}
	else
	{
//		string s = "LDP-VLDP ERROR : Skipping not supported when the mpeg's framerate differs from the disc's (" +
//			numstr::ToStr(uFPKS / 1000.0) + " vs " + numstr::ToStr(uDiscFPKS / 1000.0) + ")";
//		printf(s.c_str());
	}
	
	return result;
}

void ldp_vldp::pause()
{
	g_vldp_info->pause();
//	audio_pause();
}

bool ldp_vldp::change_speed(unsigned int uNumerator, unsigned int uDenominator)
{
	// if we aren't doing 1X, then stop the audio (this can be enhanced later)
	if ((uNumerator != 1) || (uDenominator != 1))
	{
		//audio_pause();
	}
	else
	{
    	// else the audio should be played at the correct location
		string filename;	// dummy, not used

		// calculate where our audio position should be
		unsigned int target_mpegframe = mpeg_info(filename, get_current_frame());
//		uint64_t u64AudioTargetPos = get_audio_sample_position(target_mpegframe);
//
//		// try to get the audio playing again
//		if (seek_audio(u64AudioTargetPos))
//		{
//			//audio_play(m_uElapsedMsSincePlay);
//		}
//		else
//			printf("WARNING : trying to seek audio after playing at 1X failed");
	}

	if (g_vldp_info->speedchange(m_uFramesToSkipPerFrame, m_uFramesToStallPerFrame))
      return true;

	return false;
}

void ldp_vldp::think()
{
	// VLDP relies on this number
	// (m_uBlockedMsSincePlay is only non-zero when we've used blocking seeking)
	//g_local_info.uMsTimer = m_uElapsedMsSincePlay + m_uBlockedMsSincePlay;
}

#ifdef DEBUG
// This function tests to make sure VLDP's current frame is the same as our internal current frame.
unsigned int ldp_vldp::get_current_frame()
{
	int32_t result = 0;

	// safety check
	if (!g_vldp_info) return 0;

	unsigned int uFPKS = g_vldp_info->uFpks;

	// the # of frames that have advanced since our search
	unsigned int uOffset = g_vldp_info->current_frame - m_target_mpegframe;

	unsigned int uStartMpegFrame = m_target_mpegframe;

	// since the mpeg's beginning does not correspond to the laserdisc's beginning, we add the offset
	result = m_cur_ldframe_offset + uStartMpegFrame + uOffset;

	// if we're out of bounds, just set it to 0
	if (result <= 0)
	{
		result = 0;
	}

	// if we got a legitimate frame
	else
	{
		// FIXME : THIS CODE HAS BUGS IN IT, I HAVEN'T TRACKED THEM DOWN YET HEHE
		// if the mpeg's FPS and the disc's FPS differ, we need to adjust the frame that we return
		if (g_game->get_disc_fpks() != uFPKS)
			return m_uCurrentFrame;
	}

	// if there's even a slight mismatch, report it ...
	if ((unsigned int) result != m_uCurrentFrame)
	{
		// if VLDP is ahead, that is especially disturbing
		if ((unsigned int) result > m_uCurrentFrame)
		{
			string s = "ldp-vldp::get_current_frame() [vldp ahead]: internal frame is " + numstr::ToStr(m_uCurrentFrame) +
				", vldp's current frame is " + numstr::ToStr(result);
			printf(s.c_str());

			s = "g_local_info.uMsTimer is " + numstr::ToStr(g_local_info.uMsTimer) + ", which means frame offset " +
				numstr::ToStr((g_local_info.uMsTimer * uFPKS) / 1000000);
			s += " (" + numstr::ToStr(g_local_info.uMsTimer * uFPKS * 0.000001) + ") ";
			printf(s.c_str());
			s = "m_uCurrentOffsetFrame is " + numstr::ToStr(m_uCurrentOffsetFrame) + ", m_last_seeked frame is " + numstr::ToStr(m_last_seeked_frame);
			printf(s.c_str());
			unsigned int uMsCorrect = ((result - m_last_seeked_frame) * 1000000) / uFPKS;
			s = "correct elapsed ms is " + numstr::ToStr(uMsCorrect);
			// show float if we have decent FPU
			s += " (" + numstr::ToStr((result - m_last_seeked_frame) * 1000000.0 / uFPKS) + ") ";
			s += ", which is frame offset " + numstr::ToStr((uMsCorrect * uFPKS) / 1000000);
			// show float result if we have a decent FPU
			s += " (" + numstr::ToStr(uMsCorrect * uFPKS * 0.000001) + ")";
			printf(s.c_str());

		}

		// This will be behind more than 1 frame (legitimately) playing at faster than 1X, 
		//  so uncomment this with that in mind ...
		/*
		// else if VLDP is behind more than 1 frame, that is also disturbing
		else if ((m_uCurrentFrame - result) > 1)
		{
			string s = "ldp-vldp::get_current_frame() [vldp behind]: internal frame is " + numstr::ToStr(m_uCurrentFrame) +
				", vldp's current frame is " + numstr::ToStr(result);
			printline(s.c_str());
		}

		// else vldp is only behind 1 frame, that is to be expected at times due to threading issues ...
		*/

	}

	return m_uCurrentFrame;
}
#endif

void ldp_vldp::set_search_blanking(bool enabled)
{
	m_blank_on_searches = enabled;
}

void ldp_vldp::set_skip_blanking(bool enabled)
{
	m_blank_on_skips = enabled;
}

void ldp_vldp::set_seek_frames_per_ms(double value)
{
	m_seek_frames_per_ms = value;
}

void ldp_vldp::set_min_seek_delay(unsigned int value)
{
	m_min_seek_delay = value;
}

// sets the name of the frame file
void ldp_vldp::set_framefile(const char *filename)
{
	m_bFramefileSet = true;
	m_framefile = filename;
}

// sets alternate soundtrack
void ldp_vldp::set_altaudio(const char *audio_suffix)
{
	m_altaudio_suffix = audio_suffix;
}

// sets alternate soundtrack
void ldp_vldp::set_vertical_stretch(unsigned int value)
{
	m_vertical_stretch = value;
}

void ldp_vldp::test_helper(unsigned uIterations)
{
	// We aren't calling think_delay because we want to have a lot of milliseconds pass quickly without actually waiting.

	// make a certain # of milliseconds elapse ....
	for (unsigned int u = 0; u < uIterations; u++)
		pre_think();

}

//////////////////////////////////

// read frame conversions in from LD-frame to mpeg-frame data file
bool ldp_vldp::read_frame_conversions()
{
	struct mpo_io *p_ioFileConvert;
	string s = "";
	string frame_string = "";
	bool result = false;
	string framefile_path;
	
	framefile_path = m_framefile;

	p_ioFileConvert = mpo_open(framefile_path.c_str(), MPO_OPEN_READONLY);
	
	// if the file was not found in the relative directory, try looking for it in the framefile directory
	if (!p_ioFileConvert)
	{
		//framefile_path = g_homedir.get_framefile(framefile_path);	// add directory to front of path
		framefile_path = "./lair.txt";	// add directory to front of path
		p_ioFileConvert = mpo_open(framefile_path.c_str(), MPO_OPEN_READONLY);
	}
	
	// if the framefile was opened successfully
	if (p_ioFileConvert)
	{
		MPO_BYTES_READ bytes_read = 0;
		// 2017.12.28 - RJS - Is this ever freed?  Or saved off?
		char *ff_buf = (char *) MPO_MALLOC((unsigned int) (p_ioFileConvert->size+1));	// add an extra byte to null terminate
		if (ff_buf != NULL)
		{
			if (mpo_read(ff_buf, (unsigned int) p_ioFileConvert->size, &bytes_read, p_ioFileConvert))
			{
				// if we successfully read in the whole framefile
				if (bytes_read == p_ioFileConvert->size)
				{
					string err_msg = "";
					
					ff_buf[bytes_read] = 0;	// NULL terminate the end of the file to be safe
					
					// if parse was successful
					if (parse_framefile(
						(const char *) ff_buf,
						framefile_path.c_str(),
						m_mpeg_path,
						&m_mpeginfo[0],
						m_file_index,
						sizeof(m_mpeginfo) / sizeof(struct fileframes),
						err_msg))
					{
						printf("Framefile parse succeeded. Video/Audio directory is: ");
						//printf(m_mpeg_path.c_str());
						result = true;
					}
					else
					{
						printf("Framefile Parse Error");
						//printf(err_msg.c_str());
						err_msg = "Mpeg Path : " + m_mpeg_path;
						//printf(err_msg.c_str());
						//printf("---BEGIN FRAMEFILE CONTENTS---");
						// print the entire contents of the framefile to make it easier to us to debug newbie problems using their daphne_log.txt
						//printf(ff_buf);
						//printf("---END FRAMEFILE CONTENTS---");
					}
				}
				else printf("ldp-vldp.cpp : framefile read error");
			}
			else printf("ldp-vldp.cpp : framefile read error");
		}
		else printf("ldp-vldp.cpp : mem alloc error");
		mpo_close(p_ioFileConvert);
	}
	else
	{
		//s = "Could not open framefile : " + m_framefile;
		printf("Could not open framefile");
		//printf(s.c_str());
	}
	
	return result;
}

// if file does not exist, we print an error message
bool ldp_vldp::first_video_file_exists()
{
	string full_path = "";
	bool result = false;
	
	// if we have at least one file
	if (m_file_index)
	{
		full_path = m_mpeg_path;
		full_path += m_mpeginfo[0].name;
		if (mpo_file_exists(full_path.c_str()))
			result = true;
		else
		{
			full_path = "Could not open file : " + full_path;	// keep using full_path just because it's convenient
			printf("could not open file");
			//printerror(full_path.c_str());
		}
	}
	else
	{
		printf("ERROR : Framefile seems empty, it's probably invalid");
		printf("Read the documentation to learn how to create framefiles.");
	}
	
	return result;
}

// returns true if the last video file has been parsed
// This is so we don't parse_all_video if all files are already parsed
bool ldp_vldp::last_video_file_parsed()
{
	string full_path = "";
	
	// if we have at least one file
	if (m_file_index > 0)
	{
		full_path = m_mpeg_path;
		full_path += m_mpeginfo[m_file_index-1].name;
		full_path.replace(full_path.length() - 3, 3, "dat");	// replace pre-existing suffix (which is probably .m2v) with 'dat'
		
		if (mpo_file_exists(full_path.c_str()))
         return true;
	}
	
   return false;
}

// opens (and closes) all video files, forcing any unparsed video files to get parsed
void ldp_vldp::parse_all_video()
{
	unsigned int i = 0;

	for (i = 0; i < m_file_index; i++)
	{
		// if the file can be opened...
		if (open_and_block(m_mpeginfo[i].name))
		{
			g_vldp_info->search_and_block(0, 0);	// search to frame 0 to render it so the user has something to watch while he/she waits
			think();	// let opengl have a chance to display the frame
		}
		else
		{
			printf("LDP-VLDP: Could not parse video because file ");
			//printf(m_mpeginfo[i].name.c_str());
			printf(" could not be opened.");
			break;
		}
	}
}

bool ldp_vldp::precache_all_video()
{
	bool bResult = true;

	unsigned int i = 0;
	string full_path = "";
	mpo_io *io = NULL;

	// to make sure the total file size is correct
	// (it's legal for a framefile to have the same file listed more than once)
	set<string> sDupePreventer;

	uint64_t u64TotalBytes = 0;

	// first compute file size ...
	for (i = 0; i < m_file_index; i++)
	{
		full_path = m_mpeg_path + m_mpeginfo[i].name;	// create full pathname to file
		
		// if this file's size hasn't already been taken into account
		if (sDupePreventer.find(m_mpeginfo[i].name) == sDupePreventer.end())
		{
			io = mpo_open(full_path.c_str(), MPO_OPEN_READONLY);
			if (io)
			{
				u64TotalBytes += io->size;
				mpo_close(io);	// we're done with this file ...
				sDupePreventer.insert(m_mpeginfo[i].name);	// we've used it now ...
			}
			// else file can't be opened ...
			else
			{
				printf("LDP-VLDP: when precaching, the file ");
				//printf(full_path.c_str());
				printf(" cannot be opened.");
				bResult = false;
				break;
			}
		} // end if the filesize hasn't been taken into account
		// else the filesize has already been taken into account
	}

	// if we were able to compute the file size ...
	if (bResult)
	{
		const unsigned int uFUDGE = 256;	// how many megs we assume the OS needs in addition to our application running
		unsigned int uReqMegs = (unsigned int) ((u64TotalBytes / 1048576) + uFUDGE);
		//unsigned int uMegs = get_sys_mem();

		// if we have enough memory (accounting for OS overhead, which may need to increase in the future)
		//  OR if the user wants to force precaching despite our check ...
		if (/*(uReqMegs < uMegs) || */ (m_bPreCacheForce))
		{
			for (i = 0; i < m_file_index; i++)
			{
				// if the file in question has not yet been precached
				if (m_mPreCachedFiles.find(m_mpeginfo[i].name) == m_mPreCachedFiles.end())
				{
					// try to precache and if it fails, bail ...
					if (precache_and_block(m_mpeginfo[i].name))
					{
						// store the index of the file that we last precached
						m_mPreCachedFiles[m_mpeginfo[i].name] = g_vldp_info->uLastCachedIndex;
					}
					else
					{
						full_path = m_mpeg_path + m_mpeginfo[i].name;

						printf("LDP-VLDP: precaching of file ");
						//printf(full_path.c_str());
						printf(" failed.");
						bResult = false;
					}
				} // end if file has not been precached
				// else file has already been precached, so don't precache it again
			}
		}
		else
		{
			printf("Not enough memory to precache video stream.");
			//numstr::ToStr(uReqMegs)).c_str());
			bResult = false;
		}
	}

	return bResult;
}

/*
uint64_t ldp_vldp::get_audio_sample_position(unsigned int uTargetMpegFrame)
{
	uint64_t u64AudioTargetPos = 0;

	if (!need_frame_conversion())
		u64AudioTargetPos = (((uint64_t) uTargetMpegFrame) * FREQ1000) / g_game->get_disc_fpks();
		// # of samples to seek to in the audio stream
	// If we are already doing a frame conversion elsewhere, we don't want to do it here again twice
	//  but we do need to set the audio to the correct time
	else
		u64AudioTargetPos = (((uint64_t) uTargetMpegFrame) * FREQ1000) / get_frame_conversion_fpks();

	return u64AudioTargetPos;
}
*/

// returns # of frames to seek into file, and mpeg filename
// if there's an error, filename is ""
// WARNING: This assumes the mpeg and disc are running at exactly the same FPS
// If they aren't, you will need to calculate the actual mpeg frame to seek to
// The reason I don't return time here instead of frames is because it is more accurate to
//  return frames if they are at the same FPS (which hopefully they are hehe)
uint16_t ldp_vldp::mpeg_info (string &filename, uint16_t ld_frame)
{
	unsigned int index = 0;
	uint16_t mpeg_frame = 0;	// which mpeg frame to seek (assuming mpeg and disc have same FPS)
	filename = "";	// blank 'filename' means error, so we default to this condition for safety reasons
	
	// find the mpeg file that has the LD frame inside of it
	while ((index+1 < m_file_index) && (ld_frame >= m_mpeginfo[index+1].frame))
		index = index + 1;

	// make sure that the frame they've requested comes after the first frame in our framefile
	if (ld_frame >= m_mpeginfo[index].frame)
	{
		// make sure a filename exists (when can this ever fail? verify!)
		if (m_mpeginfo[index].name != "")
		{
			filename = m_mpeginfo[index].name;
			mpeg_frame = (uint16_t) (ld_frame - m_mpeginfo[index].frame);
			m_cur_ldframe_offset = m_mpeginfo[index].frame;
		}
		else
		{
			printf("VLDP error, no filename found");
			mpeg_frame = 0;
		}
	}
	// else frame is out of range ...
	
	return(mpeg_frame);
}

bool ldp_vldp::parse_framefile(const char *pszInBuf, const char *pszFramefileFullPath,
							   string &sMpegPath, struct fileframes *pFrames, unsigned int &frame_idx, unsigned int max_frames, string &err_msg)
{
	bool result = false;
	const char *pszPtr = pszInBuf;
	unsigned int line_number = 0;	// for debugging purposes
	char ch = 0;
	
	frame_idx = 0;
	err_msg = "";
	
	// read absolute or relative directory
	pszPtr = read_line(pszPtr, sMpegPath);
	
	// if there are no additional lines
	if (!pszPtr)
	{
		// if there is at least 1 line
		if (sMpegPath.size() > 0)
			err_msg = "Framefile only has 1 line in it. Framefiles must have at least 2 lines in it.";
		else err_msg = "Framefile appears to be empty. Framefile must have at least 2 lines in it.";

		printf("framefile error");
		return false;	// normally I only like to have 1 return per function, but this is a good spot to return..
	}
	
	++line_number;	// if we get this far, it means we've read our first line
	
	// If sMpegPath is NOT an absolute path (doesn't begin with a unix '/' or have the second char as a win32 ':')
	//  then we want to use the framefile's path for convenience purposes
	// (this should be win32 and unix compatible)
	if ((sMpegPath[0] != '/') && (sMpegPath[0] != '\\') && (sMpegPath[1] != ':'))
	{
		string path = "";
		
		// try to isolate the path of the framefile
		if (get_path_of_file(pszFramefileFullPath, path))
		{
			// put the path of the framefile in front of the relative path of the mpeg files
			// This will allow people to move the location of their mpegs around to
			// different directories without changing the framefile at all.
			// For example, if the framefile and the mpegs are in the same directory,
			// then the framefile's first line could be "./"
			sMpegPath = path + sMpegPath;
		}
	}
	// else it is an absolute path, so we ignore the framefile's path
	
	string s = "";
	// convert all \'s to /'s to be more unix friendly (doesn't hurt win32)
	for (unsigned int i = 0; i < sMpegPath.length(); i++)
	{
		ch = sMpegPath[i];
		if (ch == '\\') ch = '/';
		s += ch;
	}
	sMpegPath = s;
	
	// Clean up after the user if they didn't end the path with a '/' character
	if (ch != '/')
	{
		sMpegPath += "/";	// windows will accept / as well as \, so we're ok here
	}
	
	string word = "", remaining = "";
	result = true;	// from this point, we should assume success
	int32_t frame = 0;
	
	// parse through entire file
	while (pszPtr != NULL)
	{
		pszPtr = read_line(pszPtr, s);	// read in a line
		++line_number;
		
		// if we can find the first word (frame #)
		if (find_word(s.c_str(), word, remaining))
		{
			// check for overflow
			if (frame_idx >= max_frames)
			{
				err_msg = "Framefile has too many entries in it."
					" You can increase the value of MAX_MPEG_FILES and recompile.";
				result = false;
				break;
			}

			// TODO numstr::ToInt32
			frame = numstr::ToInt32(word.c_str());	// this should work, even with whitespace after it

			// If frame is valid AND we are able to find the name of the 
			// (a non-integer will be converted to 0, so we need to make sure it really is supposed to be 0)
			if (((frame != 0) || (word == "0"))
				&& find_word(remaining.c_str(), word, remaining))
			{
				pFrames[frame_idx].frame = (int32_t) frame;
				pFrames[frame_idx].name = word;
				++frame_idx;
			}
			else
			{
				// This error has been stumping self-proclaimed "experienced emu'ers"
				// so I am going to extra effort to make it clear to them what the
				// problem is.
//				err_msg = "Expected a number followed by a string, but on line " +
//					numstr::ToStr(line_number) + ", found this: " + s + "(";
//				// print hex values of bad string to make troubleshooting easier
//				for (size_t idx = 0; idx < s.size(); idx++)
//					err_msg += "0x" + numstr::ToStr(s[idx], 16) + " ";
//
//				err_msg += ")";

				printf("error - line 1193 of ldp-vldp\n");
				printf("linenumber %d", line_number);
				printf("frame %d", frame);
				result = false;
				break;
			}
		}
		// else it is probably just an empty line, so we can ignore it
		
	} // end while file hasn't been completely parsed
	
	// if we ended up with no entries AND didn't get any error, then the framefile is bad
	if ((frame_idx == 0) && (result == true))
	{
		err_msg = "Framefile appears to not have any entries in it.";
		result = false;
	}
	
	return result;
}

uint32_t g_parse_start_time = 0;	// when mpeg parsing began approximately ...
double g_parse_start_percentage = 0.0;	// the first percentage report we received ...
bool g_parsed = false;	// whether we've received any data at all ...

// this should be called from parent thread
//void update_parse_meter()
//{
//   // if we have some data collected
//   if (g_dPercentComplete01 >= 0)
//   {
//      double elapsed_s = 0;	// how many seconds have elapsed since we began this ...
//      double total_s = 0;	// how many seconds the entire operation is likely to take
//      double remaining_s = 0;	// how many seconds are remaining
//
//      double percent_complete = g_dPercentComplete01 * 100.0;	// switch it from [0-1] to [0-100]
//
//      elapsed_s = (elapsed_ms_time(g_parse_start_time)) * 0.001;	// compute elapsed seconds
//      double percentage_accomplished = percent_complete - g_parse_start_percentage;	// how much 'percentage' points we've accomplished
//
//      total_s = (elapsed_s * 100.0) / percentage_accomplished;	// 100 signifies 100 percent (I got this equation by doing some algebra on paper)
//
//      // as long as percent_complete is always 100 or lower, total_s will always be >= elapsed_s, so no checking necessary here
//      remaining_s = total_s - elapsed_s;
//
//      SDL_Surface *screen = get_screen_blitter();	// the main screen that we can draw on ...
//
//      SDL_FillRect(screen, NULL, 0);	// erase previous stuff on the screen blitter
//
//      // if we have some progress to report ...
//      if (remaining_s > 0)
//      {
//         char s[160];
//         int half_h = screen->h >> 1;	// calculations to center message on screen ...
//         int half_w = screen->w >> 1;
//         sprintf(s, "Video parsing is %02.f percent complete, %02.f seconds remaining.\n", percent_complete, remaining_s);
//         SDLDrawText(s, screen, FONT_SMALL, (half_w-((strlen(s)/2)*FONT_SMALL_W)), half_h-FONT_SMALL_H);
//
//         // now draw a little graph thing ...
//         SDL_Rect clip = screen->clip_rect;
//         const int THICKNESS = 10;	// how thick our little status bar will be
//         clip.y = (clip.h - THICKNESS) / 2;	// where to start our little status bar
//         clip.h = THICKNESS;
//         clip.y += FONT_SMALL_H + 5;	// give us some padding
//
//         SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format, 255, 255, 255));	// draw a white bar across the screen ...
//
//         clip.x++;	// move left boundary in 1 pixel
//         clip.y++;	// move upper boundary down 1 pixel
//         clip.w -= 2;	// move right boundary in 1 pixel
//         clip.h -= 2;	// move lower boundary in 1 pixel
//
//         SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format, 0, 0, 0));	// fill inside with black
//
//         clip.w = (uint16_t)((screen->w * g_dPercentComplete01) + 0.5) - 1;	// compute how wide our progress bar should be (-1 to take into account left pixel border)
//
//         // go from full red (hardly complete) to full green (fully complete)
//         SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format,
//                  (uint8_t)(255 * (1.0 - g_dPercentComplete01)),
//                  (uint8_t)(255 * g_dPercentComplete01),
//                  0));
//      }
//   }
//}

// percent_complete is between 0 and 1
// a negative value means that we are starting to parse a new file NOW
//void report_parse_progress_callback(double percent_complete_01)
//{
//	g_dPercentComplete01 = percent_complete_01;
//	g_bGotParseUpdate = true;
//	g_parsed = true;	// so we can know to re-create the overlay
//
//	// if a new parse is starting
//	if (percent_complete_01 < 0)
//	{
//		// NOTE : this would be a good place to automatically free the yuv overlay
//		// BUT it was causing crashes... free_yuv_overlay here if you find any compatibility problems on other platforms
//		g_parse_start_time = refresh_ms_time();
//		g_parse_start_percentage = 0;
//	}
//}
