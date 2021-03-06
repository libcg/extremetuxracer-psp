/* --------------------------------------------------------------------
EXTREME TUXRACER

Copyright (C) 1999-2001 Jasmin F. Patry (Tuxracer)
Copyright (C) 2010 Extreme Tuxracer Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
---------------------------------------------------------------------*/

#include "winsys.h"
#include "ogl.h"
#include "audio.h"
#include "game_ctrl.h"
#include "font.h"
#include "score.h"
#include "textures.h"

#ifdef __PSP__
#include <pspctrl.h>

#define EVENT_PRESS(jkey, kkey) \
	if (pad.Buttons & jkey && !(oldPad.Buttons & jkey)) { \
		psp_event.keysym.sym = kkey; \
		SDL_PushEvent((SDL_Event*)&psp_event); \
	}

#define EVENT_RELEASE(jkey, kkey) \
	if (!(pad.Buttons & jkey) && oldPad.Buttons & jkey) { \
		psp_event.keysym.sym = kkey; \
		SDL_PushEvent((SDL_Event*)&psp_event); \
	}
#endif

#define USE_JOYSTICK true

CWinsys Winsys;

CWinsys::CWinsys () {
	screen = NULL;
	for (int i=0; i<NUM_GAME_MODES; i++) {
		modefuncs[i].init   = NULL;
		modefuncs[i].loop   = NULL;
		modefuncs[i].term   = NULL;
		modefuncs[i].keyb   = NULL;
		modefuncs[i].mouse  = NULL;
		modefuncs[i].motion = NULL;
		modefuncs[i].jaxis  = NULL;
		modefuncs[i].jbutt  = NULL;
	}
	new_mode = NO_MODE;
	lasttick = 0;

	joystick = NULL;
	numJoysticks = 0;
	joystick_active = false;

 	resolution[0] = MakeRes (0, 0);
	resolution[1] = MakeRes (DEFAULT_X_RESOLUTION, DEFAULT_Y_RESOLUTION);
	resolution[2] = MakeRes (1024, 768);
	resolution[3] = MakeRes (1152, 864);
	resolution[4] = MakeRes (1280, 960);
	resolution[5] = MakeRes (1280, 1024);
	resolution[6] = MakeRes (1360, 768);
	resolution[7] = MakeRes (1400, 1050);
	resolution[8] = MakeRes (1440, 900);
	resolution[9] = MakeRes (1680, 1050);

	auto_x_resolution = DEFAULT_X_RESOLUTION;
	auto_y_resolution = DEFAULT_Y_RESOLUTION;

	clock_time = 0;
	cur_time = 0;
	lasttick = 0;
	elapsed_time = 0;
	remain_ticks = 0;
}

CWinsys::~CWinsys () {}

TScreenRes CWinsys::MakeRes (int width, int height) {
	TScreenRes res;
	res.width = width;
	res.height = height;
	return res;
}

TScreenRes CWinsys::GetResolution (int idx) {
	if (idx < 0 || idx >= NUM_RESOLUTIONS) {
		return MakeRes (DEFAULT_X_RESOLUTION, DEFAULT_Y_RESOLUTION);
	}
	return resolution[idx];
}

string CWinsys::GetResName (int idx) {
	string line;
	if (idx < 0 || idx >= NUM_RESOLUTIONS) return "default";
	if (idx == 0) return ("auto");
	line = Int_StrN (resolution[idx].width);
	line += " x " + Int_StrN (resolution[idx].height);
	return line;
}

double CWinsys::CalcScreenScale () {
#ifdef __PSP__
	return 0.5;
#else
	double hh = (double)param.y_resolution;
	if (hh < 768) return 0.78; 
	else if (hh == 768) return 1.0;
	else return (hh / 768);
#endif
}

/*
typedef struct SDL_Surface {
    Uint32 flags;                           // Read-only 
    SDL_PixelFormat *format;                // Read-only 
    int w, h;                               // Read-only 
    Uint16 pitch;                           // Read-only 
    void *pixels;                           // Read-write 
    SDL_Rect clip_rect;                     // Read-only 
    int refcount;                           // Read-mostly
} SDL_Surface;
*/

void CWinsys::SetupVideoMode (TScreenRes resolution) {
    int bpp = 0;
    Uint32 video_flags = SDL_OPENGL;
#ifdef __PSP__
    video_flags |= SDL_FULLSCREEN;
    bpp = 16;
#else    
    if (param.fullscreen) video_flags |= SDL_FULLSCREEN;
	switch (param.bpp_mode ) {
		case 0:	bpp = 0; break;
		case 1:	bpp = 16; break;
		case 2:	bpp = 32; break;
		default: param.bpp_mode = 0; bpp = 0;
    }
#endif
	if ((screen = SDL_SetVideoMode 
	(resolution.width, resolution.height, bpp, video_flags)) == NULL) {
		Message ("couldn't initialize video",  SDL_GetError()); 
		Message ("set to default resolution");
		screen = SDL_SetVideoMode (DEFAULT_X_RESOLUTION, DEFAULT_Y_RESOLUTION,
								   bpp, video_flags);
		param.res_type = 1;
		SaveConfigFile ();
	}
	SDL_Surface *surf = SDL_GetVideoSurface ();
	param.x_resolution = surf->w;
	param.y_resolution = surf->h;
	if (resolution.width == 0 && resolution.height == 0) {
		auto_x_resolution = param.x_resolution;
		auto_y_resolution = param.y_resolution;
	}
 	param.scale = CalcScreenScale ();
	if (param.use_quad_scale) param.scale = sqrt (param.scale);
}

void CWinsys::SetupVideoMode (int idx) {
	if (idx < 0 || idx >= NUM_RESOLUTIONS) {
		SetupVideoMode (MakeRes (DEFAULT_X_RESOLUTION, DEFAULT_Y_RESOLUTION));
	}
	else SetupVideoMode (resolution[idx]);
}

void CWinsys::SetupVideoMode (int width, int height) {
	SetupVideoMode (MakeRes (width, height));
}

void CWinsys::InitJoystick () {
    if (SDL_InitSubSystem (SDL_INIT_JOYSTICK) < 0) {
		Message ("Could not initialize SDL_joystick: %s", SDL_GetError());
		return;
	}
	numJoysticks = SDL_NumJoysticks ();
	if (numJoysticks < 1) {
		joystick = NULL;
		return;		
	}	
	SDL_JoystickEventState (SDL_ENABLE);
	joystick = SDL_JoystickOpen (0);	// first stick with number 0
    if (joystick == NULL) {
		Message ("Cannot open joystick %s", SDL_GetError ());
		return;
    }
	joystick_active = true;
}

void CWinsys::Init () {
    Uint32 sdl_flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE | SDL_INIT_TIMER;
    if (SDL_Init (sdl_flags) < 0) Message ("Could not initialize SDL");
    SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);

	#if defined (USE_STENCIL_BUFFER)
	    SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);
	#endif
	
	SetupVideoMode (GetResolution (param.res_type));
	Reshape (param.x_resolution, param.y_resolution);

    SDL_WM_SetCaption (WINDOW_TITLE, WINDOW_TITLE);
	KeyRepeat (false);
	if (USE_JOYSTICK) InitJoystick ();
//	SDL_EnableUNICODE (1);
}

void CWinsys::KeyRepeat (bool repeat) {
	if (repeat) 
		SDL_EnableKeyRepeat (SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	else SDL_EnableKeyRepeat (0, 0);
}

void CWinsys::SetFonttype () {
	if (param.use_papercut_font > 0) {
		FT.SetFont ("pc20");
	} else {
		FT.SetFont ("bold");
	}
}

void CWinsys::CloseJoystick () {
	if (joystick_active) SDL_JoystickClose (joystick);	
}

void CWinsys::Quit () {
	CloseJoystick ();
	Tex.FreeTextureList ();
	Course.FreeCourseList ();
	Course.ResetCourse ();
	SaveMessages ();
	Audio.Close ();		// frees music and sound as well
	FT.Clear ();
	if (g_game.argument < 1) Players.SavePlayers ();
	Score.SaveHighScore ();
	SDL_Quit ();
    exit (0);
}

void CWinsys::PrintJoystickInfo () {
	if (joystick_active == false) {
		Message ("No joystick found");
		return;
	}
	PrintStr ("");
	PrintStr (SDL_JoystickName (0));
	int num_buttons = SDL_JoystickNumButtons (joystick);
	printf ("Joystick has %d button%s\n", num_buttons, num_buttons == 1 ? "" : "s");
	int num_axes = SDL_JoystickNumAxes (joystick);
	printf ("Joystick has %d ax%ss\n\n", num_axes, num_axes == 1 ? "i" : "e");
}

// ------------ modes -------------------------------------------------

void CWinsys::SetModeFuncs (
		TGameMode mode, TInitFuncN init, TLoopFuncN loop, TTermFuncN term,
		TKeybFuncN keyb, TMouseFuncN mouse, TMotionFuncN motion,
		TJAxisFuncN jaxis, TJButtFuncN jbutt, TKeybFuncS keyb_spec) {
    modefuncs[mode].init      = init;
   	modefuncs[mode].loop      = loop;
    modefuncs[mode].term      = term;
    modefuncs[mode].keyb      = keyb;
   	modefuncs[mode].mouse     = mouse;
    modefuncs[mode].motion    = motion;
    modefuncs[mode].jaxis     = jaxis;
    modefuncs[mode].jbutt     = jbutt;
    modefuncs[mode].keyb_spec = keyb_spec;
}

void CWinsys::IdleFunc () {}

bool CWinsys::ModePending () {
	return g_game.mode != new_mode;
}

/*
typedef struct{
  Uint8 scancode;
  SDLKey sym;
  SDLMod mod;
  Uint16 unicode;
} SDL_keysym;*/

void CWinsys::PollEvent () {
    SDL_Event event; 
	SDL_keysym sym;
    unsigned int key, axis;
    int x, y;
	float val;

#ifdef __PSP__
	static SceCtrlData pad, oldPad;
	SDL_KeyboardEvent psp_event;
	
	oldPad = pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	
	// generate SDL events from key presses and releases

	psp_event.type = SDL_KEYDOWN;
	psp_event.which = 0;
	psp_event.state = SDL_PRESSED;
	EVENT_PRESS(PSP_CTRL_CROSS, SDLK_RETURN);
	EVENT_PRESS(PSP_CTRL_CIRCLE, SDLK_ESCAPE);
	EVENT_PRESS(PSP_CTRL_LEFT, SDLK_LEFT);
	EVENT_PRESS(PSP_CTRL_RIGHT, SDLK_RIGHT);
	EVENT_PRESS(PSP_CTRL_UP, SDLK_UP);
	EVENT_PRESS(PSP_CTRL_DOWN, SDLK_DOWN);
	
	psp_event.type = SDL_KEYUP;
	psp_event.which = 0;
	psp_event.state = SDL_RELEASED;
	EVENT_RELEASE(PSP_CTRL_CROSS, SDLK_RETURN);
	EVENT_RELEASE(PSP_CTRL_CIRCLE, SDLK_ESCAPE);
	EVENT_RELEASE(PSP_CTRL_LEFT, SDLK_LEFT);
	EVENT_RELEASE(PSP_CTRL_RIGHT, SDLK_RIGHT);
	EVENT_RELEASE(PSP_CTRL_UP, SDLK_UP);
	EVENT_RELEASE(PSP_CTRL_DOWN, SDLK_DOWN);
#endif

	while (SDL_PollEvent (&event)) {
		if (ModePending()) {
			IdleFunc ();
    	} else {
			switch (event.type) {
				case SDL_KEYDOWN:
				if (modefuncs[g_game.mode].keyb) {
					SDL_GetMouseState (&x, &y);
					key = event.key.keysym.sym; 
					(modefuncs[g_game.mode].keyb) (key, key >= 256, false, x, y);
				} else if (modefuncs[g_game.mode].keyb_spec) {
					sym = event.key.keysym;
					(modefuncs[g_game.mode].keyb_spec) (sym, false);
				}
				break;
	
				case SDL_KEYUP:
				if (modefuncs[g_game.mode].keyb) {
					SDL_GetMouseState (&x, &y);
					key = event.key.keysym.sym; 
					(modefuncs[g_game.mode].keyb)  (key, key >= 256, true, x, y);
				} else if (modefuncs[g_game.mode].keyb_spec) {
					sym = event.key.keysym;
					(modefuncs[g_game.mode].keyb_spec) (sym, true);
				}
				break;
	
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
				if (modefuncs[g_game.mode].mouse) {
					(modefuncs[g_game.mode].mouse) 
						(event.button.button, event.button.state,
						event.button.x, event.button.y);
				}
				break;
	
				case SDL_MOUSEMOTION:
					if (modefuncs[g_game.mode].motion) 
					(modefuncs[g_game.mode].motion) (event.motion.x, event.motion.y);
				break;

				case SDL_JOYAXISMOTION:  
				if (joystick_active) {
					axis = event.jaxis.axis;
					if (modefuncs[g_game.mode].jaxis && axis < 2) {
						val = (float)event.jaxis.value / 32768;
							(modefuncs[g_game.mode].jaxis) (axis, val);
					}
				}
				break; 

				case SDL_JOYBUTTONDOWN:  
				case SDL_JOYBUTTONUP:  
				if (joystick_active) {
					if (modefuncs[g_game.mode].jbutt) {
						(modefuncs[g_game.mode].jbutt) 
							(event.jbutton.button, event.jbutton.state);
					}
				}
				break;

				case SDL_VIDEORESIZE:
					param.x_resolution = event.resize.w;
					param.y_resolution = event.resize.h;
					SetupVideoMode (param.res_type);
					Reshape (event.resize.w, event.resize.h);
				break;
			
				case SDL_QUIT: 
					Quit ();
				break;
			}
    	}
	}
}

void CWinsys::ChangeMode () {
	// this function is called when new_mode is set
	// terminate function of previous mode
	if (g_game.mode >= 0 &&  modefuncs[g_game.mode].term != 0) 
	    (modefuncs[g_game.mode].term) ();
	g_game.prev_mode = g_game.mode;

	// init function of new mode
	if (modefuncs[new_mode].init != 0) {
		(modefuncs[new_mode].init) ();
		clock_time = SDL_GetTicks() * 1.e-3;
	}

	g_game.mode = new_mode;
	// new mode is now the current mode.
}

void CWinsys::CallLoopFunction () {
		cur_time = SDL_GetTicks() * 1.e-3;
		g_game.time_step = cur_time - clock_time;
		if (g_game.time_step < 0.0001) g_game.time_step = 0.0001;
		clock_time = cur_time;

		if (modefuncs[g_game.mode].loop != 0) 
			(modefuncs[g_game.mode].loop) (g_game.time_step);	
}

void CWinsys::EventLoop () {
    while (true) {
		PollEvent ();
	    if (ModePending()) ChangeMode ();
		CallLoopFunction ();
		SDL_Delay (g_game.loopdelay);
    }
}

unsigned char *CWinsys::GetSurfaceData () {
	return (unsigned char*)screen->pixels;
}

