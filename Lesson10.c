/*
 *      This Code Was Created By Lionel Brits & Jeff Molofee 2000
 *      A HUGE Thanks To Fredric Echols For Cleaning Up
 *      And Optimizing The Base Code, Making It More Flexible!
 *      If You've Found This Code Useful, Please Let Me Know.
 *      Visit My Site At nehe.gamedev.net
 */

#include <math.h>              // Math Library Header File
#include <stdio.h>             // Header File For Standard Input/Output
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>   // Header File For The OpenGL32 Library

#define BTTN_YES 0
#define BTTN_NO  1

static int ShowYesNoMessageBox(SDL_Window *window, int defaultbttn, const char *title, const char *message)
{
	const SDL_MessageBoxButtonFlags defaultflags =
		SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	const SDL_MessageBoxButtonData yesnobttns[2] =
	{
		{ .flags = defaultbttn == BTTN_YES ? defaultflags : 0, .buttonID = BTTN_YES, .text = "Yes" },
		{ .flags = defaultbttn == BTTN_NO  ? defaultflags : 0, .buttonID = BTTN_NO,  .text = "No"  }
	};
	const SDL_MessageBoxData msgbox =
	{
		.flags       = SDL_MESSAGEBOX_INFORMATION,
		.window      = window,
		.title       = title,
		.message     = message,
		.numbuttons  = 2,
		.buttons     = yesnobttns,
		.colorScheme = NULL
	};

	int bttnid = defaultbttn;
	SDL_ShowMessageBox(&msgbox, &bttnid);
	return bttnid;
}

typedef struct tagCAMERA
{
	float heading;
	float xpos, zpos;
	float yrot;                   // Y Rotation
	float walkbias, walkbiasangle;
	float lookupdown;
	float z;                      // Depth Into The Screen
} CAMERA;

typedef struct tagVERTEX
{
	float x, y, z;
	float u, v;
} VERTEX;

typedef struct tagTRIANGLE
{
	VERTEX vertex[3];
} TRIANGLE;

typedef struct tagSECTOR
{
	int numtriangles;
	TRIANGLE *triangle;
} SECTOR;

typedef struct tagAPPSTATE
{
	SDL_Window   *win;         // Holds Our Window Handle
	SDL_GLContext ctx;         // Permanent Rendering Context

	bool fullscreen, blend;

	CAMERA camera;
	unsigned filter;           // Which Filter To Use
	GLuint texture[3];         // Storage For 3 Textures

	SECTOR sector1;            // Our Model Goes Here
} APPSTATE;

static void readstr(FILE *f, char *string)
{
	do
	{
		fgets(string, 255, f);
	} while ((string[0] == '/') || (string[0] == '\n'));
	return;
}

static void SetupWorld(APPSTATE *state)
{
	float x, y, z, u, v;
	int numtriangles;
	FILE *filein;
	char oneline[255];
	filein = fopen("data/world.txt", "r");  // File To Load World Data From

	readstr(filein, oneline);
	sscanf(oneline, "NUMPOLLIES %d\n", &numtriangles);

	state->sector1.triangle = malloc(sizeof(TRIANGLE) * numtriangles);
	state->sector1.numtriangles = numtriangles;
	for (int loop = 0; loop < numtriangles; loop++)
	{
		for (int vert = 0; vert < 3; vert++)
		{
			readstr(filein, oneline);
			sscanf(oneline, "%f %f %f %f %f", &x, &y, &z, &u, &v);
			state->sector1.triangle[loop].vertex[vert].x = x;
			state->sector1.triangle[loop].vertex[vert].y = y;
			state->sector1.triangle[loop].vertex[vert].z = z;
			state->sector1.triangle[loop].vertex[vert].u = u;
			state->sector1.triangle[loop].vertex[vert].v = v;
		}
	}
	fclose(filein);
	return;
}

static bool FlipSurface(SDL_Surface *surface)
{
	if (!surface || SDL_LockSurface(surface) < 0)
	{
		return false;
	}

	const int pitch = surface->pitch;
	const int numrows = surface->h;
	unsigned char *pixels = (unsigned char *)surface->pixels;
	unsigned char *tmprow = malloc(sizeof(unsigned char) * pitch);

	unsigned char *row1 = pixels;
	unsigned char *row2 = pixels + (numrows - 1) * pitch;
	for (int i = 0; i < numrows / 2; ++i)
	{
		// Swap rows
		memcpy(tmprow, row1, pitch);
		memcpy(row1, row2, pitch);
		memcpy(row2, tmprow, pitch);

		row1 += pitch;
		row2 -= pitch;
	}

	free(tmprow);
	SDL_UnlockSurface(surface);
	return true;
}

static bool LoadGLTextures(APPSTATE *state)                      // Load bitmap as textures
{
	SDL_Surface *TextureImage = NULL;                            // Create Storage Space For The Texture

	// Load & flip the bitmap, check for errors
	if (!(TextureImage = SDL_LoadBMP("Data/Mud.bmp")) || !FlipSurface(TextureImage))
	{
		return false;
	}

	glGenTextures(3, &state->texture[0]);                        // Create three textures
	GLint params[3][3] =
	{
		[0] = { GL_NEAREST, GL_NEAREST, GL_FALSE },              // Nearest filtered
		[1] = { GL_LINEAR, GL_LINEAR, GL_FALSE },                // Linear filtered
		[2] = { GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_TRUE },  // MipMapped
	};
	for (int i = 0; i < 3; i++)
	{
		glBindTexture(GL_TEXTURE_2D, state->texture[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params[i][0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params[i][1]);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, params[i][2]);
		glTexImage2D(GL_TEXTURE_2D,
			0, 3, TextureImage->w, TextureImage->h,
			0, GL_BGR, GL_UNSIGNED_BYTE, TextureImage->pixels);
	}

	SDL_DestroySurface(TextureImage);                            // Free the image structure
	return true;
}

static void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
	double h = 1.0 / tan(fovy * (SDL_PI_D / 180.0) * 0.5);
	double w = h / aspect;
	double invcliprng = 1.0 / (zFar - zNear);
	double z = -(zFar + zNear) * invcliprng;
	double z2 = -(2.0 * zFar * zNear) * invcliprng;
	GLdouble mtx[16] =
	{
		w,   0.0, 0.0, 0.0,
		0.0, h,   0.0, 0.0,
		0.0, 0.0, z,  -1.0,
		0.0, 0.0, z2,  0.0
	};
	glLoadMatrixd(mtx);
}

static void ReSizeGLScene(int width, int height)         // Resize And Initialize The GL Window
{
	if (height == 0)                                     // Prevent A Divide By Zero By
	{
		height = 1;                                      // Making Height Equal One
	}

	glViewport(0, 0, width, height);                     // Reset The Current Viewport

	glMatrixMode(GL_PROJECTION);                         // Select The Projection Matrix
	glLoadIdentity();                                    // Reset The Projection Matrix

	// Calculate The Aspect Ratio Of The Window
	gluPerspective(45.0f, (float)width / (float)height, 0.1f, 100.0f);

	glMatrixMode(GL_MODELVIEW);                          // Select The Modelview Matrix
	glLoadIdentity();                                    // Reset The Modelview Matrix
}

static bool InitGL(APPSTATE *state)                      // All Setup For OpenGL Goes Here
{
	if (!LoadGLTextures(state))                          // Jump To Texture Loading Routine
	{
		return false;
	}

	glEnable(GL_TEXTURE_2D);                             // Enable Texture Mapping
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);                   // Set The Blending Function For Translucency
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);                // This Will Clear The Background Color To Black
	glClearDepth(1.0);                                   // Enables Clearing Of The Depth Buffer
	glDepthFunc(GL_LESS);                                // The Type Of Depth Test To Do
	glEnable(GL_DEPTH_TEST);                             // Enables Depth Testing
	glShadeModel(GL_SMOOTH);                             // Enables Smooth Color Shading
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);   // Really Nice Perspective Calculations

	SetupWorld(state);

	return true;                                         // Initialization Went OK
}

static void DrawGLScene(APPSTATE *state)                 // Here's Where We Do All The Drawing
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Clear The Screen And The Depth Buffer
	glLoadIdentity();                                    // Reset The View

	GLfloat x_m, y_m, z_m, u_m, v_m;
	GLfloat xtrans = -state->camera.xpos;
	GLfloat ztrans = -state->camera.zpos;
	GLfloat ytrans = -state->camera.walkbias - 0.25f;
	GLfloat sceneroty = 360.0f - state->camera.yrot;

	int numtriangles;

	glRotatef(state->camera.lookupdown, 1.0f, 0.0f, 0.0f);
	glRotatef(sceneroty, 0.0f, 1.0f, 0.0f);

	glTranslatef(xtrans, ytrans, ztrans);
	glBindTexture(GL_TEXTURE_2D, state->texture[state->filter]);

	numtriangles = state->sector1.numtriangles;

	// Process Each Triangle
	for (int loop_m = 0; loop_m < numtriangles; loop_m++)
	{
		glBegin(GL_TRIANGLES);
			glNormal3f(0.0f, 0.0f, 1.0f);
			x_m = state->sector1.triangle[loop_m].vertex[0].x;
			y_m = state->sector1.triangle[loop_m].vertex[0].y;
			z_m = state->sector1.triangle[loop_m].vertex[0].z;
			u_m = state->sector1.triangle[loop_m].vertex[0].u;
			v_m = state->sector1.triangle[loop_m].vertex[0].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);

			x_m = state->sector1.triangle[loop_m].vertex[1].x;
			y_m = state->sector1.triangle[loop_m].vertex[1].y;
			z_m = state->sector1.triangle[loop_m].vertex[1].z;
			u_m = state->sector1.triangle[loop_m].vertex[1].u;
			v_m = state->sector1.triangle[loop_m].vertex[1].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);

			x_m = state->sector1.triangle[loop_m].vertex[2].x;
			y_m = state->sector1.triangle[loop_m].vertex[2].y;
			z_m = state->sector1.triangle[loop_m].vertex[2].z;
			u_m = state->sector1.triangle[loop_m].vertex[2].u;
			v_m = state->sector1.triangle[loop_m].vertex[2].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);
		glEnd();
	}
}

static void KillGLWindow(APPSTATE *state)                // Properly Kill The Window
{
	if (state->fullscreen)                               // Are We In Fullscreen Mode?
	{
		SDL_SetWindowFullscreen(state->win, SDL_FALSE);  // If So Switch Back To The Desktop
		SDL_ShowCursor();                                // Show Mouse Pointer
	}

	if (state->ctx)                                      // Do We Have A Rendering Context?
	{
		if (SDL_GL_MakeCurrent(state->win, NULL))        // Are We Able To Release The DC And RC Contexts?
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SHUTDOWN ERROR", "Release Of RC Failed.", NULL);
		}

		SDL_GL_DeleteContext(state->ctx);                // Delete The RC
		state->ctx = NULL;                               // Set RC To NULL
	}

	SDL_DestroyWindow(state->win);                       // Destroy The Window
	state->win = NULL;                                   // Set hWnd To NULL
}


/*  This Code Creates Our OpenGL Window.  Parameters Are:                   *
 *  title           - Title To Appear At The Top Of The Window              *
 *  width           - Width Of The GL Window Or Fullscreen Mode             *
 *  height          - Height Of The GL Window Or Fullscreen Mode            *
 *  bits            - Number Of Bits To Use For Color (8/16/24/32)          *
 *  fullscreenflag  - Use Fullscreen Mode (TRUE) Or Windowed Mode (FALSE)   */


static bool CreateGLWindow(APPSTATE *state, char *title, int width, int height, int bits, bool fullscreenflag)
{
	SDL_Rect WindowRect;                                        // Grabs Rectangle Upper Left / Lower Right Values
	WindowRect.x = 0;                                           // Set Left Value To 0
	WindowRect.w = width;                                       // Set Right Value To Requested Width
	WindowRect.y = 0;                                           // Set Top Value To 0
	WindowRect.h = height;                                      // Set Bottom Value To Requested Height

	state->fullscreen = fullscreenflag;                         // Set The Global Fullscreen Flag

	// Create The Window
	if (!(state->win = SDL_CreateWindow(title,
		WindowRect.w,                                           // Window Width
		WindowRect.h,                                           // Window Height
		SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY)))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Window Creation Error.", NULL);
		return false;                                           // Return FALSE
	}

	if (fullscreenflag)                                         // Attempt Fullscreen Mode?
	{
		if (SDL_SetWindowFullscreen(state->win, SDL_TRUE) < 0)  // Try To Set Selected Mode And Get Results.
		{
			// If The Mode Fails, Offer Two Options.  Quit Or Use Windowed Mode.
			int bttnid = ShowYesNoMessageBox(state->win, BTTN_YES, "NeHe GL",
				"The Requested Fullscreen Mode Is Not Supported By\nYour Video Card. Use Windowed Mode Instead?");
			if (bttnid == BTTN_NO)
			{
				// Pop Up A Message Box Letting User Know The Program Is Closing.
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "ERROR", "Program Will Now Close.", NULL);
				return false;                                   // Return FALSE
			}
			state->fullscreen = false;
		}
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);                // Must Support Double Buffering
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 0);                    // Color Bits Ignored
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);                  // No Alpha Buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);            // No Accumulation Buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);              // Accumulation Bits Ignored
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);                 // 16Bit Z-Buffer (Depth Buffer)
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);                // No Stencil Buffer


	if (!(state->ctx = SDL_GL_CreateContext(state->win)))       // Are We Able To Get A Rendering Context?
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Create A GL Rendering Context.", NULL);
		return false;                                           // Return FALSE
	}

	if (SDL_GL_MakeCurrent(state->win, state->ctx))             // Try To Activate The Rendering Context
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Activate The GL Rendering Context.", NULL);
		return false;                                           // Return FALSE
	}

	SDL_ShowWindow(state->win);
	SDL_GL_SetSwapInterval(1);
	ReSizeGLScene(width, height);                               // Set Up Our Perspective GL Screen

	if (!InitGL(state))                                         // Initialize Our Newly Created GL Window
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Initialization Failed.", NULL);
		return false;                                           // Return FALSE
	}

	return true;                                                // Success
}

int SDL_AppEvent(void *appstate, const SDL_Event *event)
{
	APPSTATE *state = (APPSTATE *)appstate;
	switch (event->type)
	{
	case SDL_EVENT_QUIT:                                          // Have we received a quit event?
		return SDL_APP_SUCCESS;                                   // Exit with success status

	case SDL_EVENT_KEY_DOWN:
		if (event->key.keysym.sym == SDLK_ESCAPE)                 // Quit on Escape
		{
			return SDL_APP_SUCCESS;                               // Exit with success status
		}
		if (!event->key.repeat)                                   // Was a key just pressed?
		{
			switch (event->key.keysym.sym)
			{
			case SDLK_b:                                          // B = Toggle blending
				state->blend = !state->blend;
				if (!state->blend)
				{
					glDisable(GL_BLEND);
					glEnable(GL_DEPTH_TEST);
				}
				else
				{
					glEnable(GL_BLEND);
					glDisable(GL_DEPTH_TEST);
				}
				break;

			case SDLK_f:                                          // F = Cycle texture filtering
				state->filter += 1;
				if (state->filter > 2)
				{
					state->filter = 0;
				}
				break;

			case SDLK_F1:                                         // F1 = Toggle Fullscreen / Windowed Mode
				SDL_SetWindowFullscreen(state->win, !state->fullscreen);
				break;

			default: break;
			}
		}
		break;

	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:                     // Deal with window resizes
		ReSizeGLScene(event->window.data1, event->window.data2);  // data1=Backbuffer Width, data2=Backbuffer Height
		break;

		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
			state->fullscreen = true;
			break;

		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			state->fullscreen = false;
			break;

	default: break;
	}
	return SDL_APP_CONTINUE;
}

int SDL_AppIterate(void *appstate)
{
	APPSTATE *state = (APPSTATE *)appstate;
	DrawGLScene(state);             // Draw the scene
	SDL_GL_SwapWindow(state->win);  // Swap buffers (Double buffering)

	// Handle keyboard input
	const Uint8 *keys = SDL_GetKeyboardState(NULL);

	if (keys[SDL_SCANCODE_PAGEUP])
	{
		state->camera.z -= 0.02f;
	}

	if (keys[SDL_SCANCODE_PAGEDOWN])
	{
		state->camera.z += 0.02f;
	}

	const float piover180 = 0.0174532925f;

	if (keys[SDL_SCANCODE_UP])
	{
		state->camera.xpos -= sinf(state->camera.heading * piover180) * 0.05f;
		state->camera.zpos -= cosf(state->camera.heading * piover180) * 0.05f;
		if (state->camera.walkbiasangle >= 359.0f)
		{
			state->camera.walkbiasangle = 0.0f;
		}
		else
		{
			state->camera.walkbiasangle += 10;
		}
		state->camera.walkbias = sinf(state->camera.walkbiasangle * piover180) / 20.0f;
	}

	if (keys[SDL_SCANCODE_DOWN])
	{
		state->camera.xpos += sinf(state->camera.heading * piover180) * 0.05f;
		state->camera.zpos += cosf(state->camera.heading * piover180) * 0.05f;
		if (state->camera.walkbiasangle <= 1.0f)
		{
			state->camera.walkbiasangle = 359.0f;
		}
		else
		{
			state->camera.walkbiasangle -= 10;
		}
		state->camera.walkbias = sinf(state->camera.walkbiasangle * piover180) / 20.0f;
	}

	if (keys[SDL_SCANCODE_RIGHT])
	{
		state->camera.heading -= 1.0f;
		state->camera.yrot = state->camera.heading;
	}

	if (keys[SDL_SCANCODE_LEFT])
	{
		state->camera.heading += 1.0f;
		state->camera.yrot = state->camera.heading;
	}

	if (keys[SDL_SCANCODE_PAGEUP])
	{
		state->camera.lookupdown -= 1.0f;
	}

	if (keys[SDL_SCANCODE_PAGEDOWN])
	{
		state->camera.lookupdown += 1.0f;
	}

	return SDL_APP_CONTINUE;
}

int SDL_AppInit(void **appstate, int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		return SDL_APP_FAILURE;
	}

	APPSTATE *state = *appstate = malloc(sizeof(APPSTATE));
	if (!state)
	{
		return SDL_APP_FAILURE;
	}
	*state = (APPSTATE)
	{
		.win = NULL,
		.ctx = NULL,

		.fullscreen = false,
		.blend = false,          // Blending OFF

		.camera = (CAMERA)
		{
			.heading = 0.0f,
			.xpos = 0.0f,
			.zpos = 0.0f,
			.yrot = 0.0f,
			.walkbias = 0.0f,
			.walkbiasangle = 0.0f,
			.lookupdown = 0.0f,
			.z = 0.0f
		},

		.filter = 0,
		.texture = { 0, 0, 0 },
		.sector1 = (SECTOR){ .numtriangles = 0, .triangle = NULL }
	};

	// Ask The User Which Screen Mode They Prefer
	int bttnid = ShowYesNoMessageBox(state->win, BTTN_NO, "Start FullScreen?",
		"Would You Like To Run In Fullscreen Mode?");

	// Create Our OpenGL Window
	const bool wantfullscreen = (bttnid == BTTN_YES);
	if (!CreateGLWindow(state, "Lionel Brits & NeHe's 3D World Tutorial", 640, 480, 16, wantfullscreen))
	{
		return SDL_APP_FAILURE;  // Quit If Window Was Not Created
	}

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate)
{
	// Shutdown
	if (appstate)
	{
		APPSTATE *state = (APPSTATE *)appstate;
		free(state->sector1.triangle);
		if (state->ctx)
		{
			glDeleteTextures(3, state->texture);
		}
		KillGLWindow(state);  // Kill The Window
		free(state);
	}
	SDL_Quit();
}
