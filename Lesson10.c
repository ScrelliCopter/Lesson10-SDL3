/*
 *  This code was created by Lionel Brits & Jeff Molofee 2000.
 *  A HUGE thanks to Fredric Echols for cleaning up
 *  and optimizing the base code, making it more flexible!
 *  If you've found this code useful, please let me know.
 *  Visit my site at https://nehe.gamedev.net
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

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
	float yrot;
	float walkbias, walkbiasangle;
	float lookupdown;
	float z;
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
	SDL_Window   *win;
	SDL_GLContext ctx;

	bool fullscreen, blend;

	CAMERA camera;
	unsigned filter;    // Filtered texture selection
	GLuint texture[3];  // Filtered textures

	SECTOR sector1;
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
	filein = fopen("data/world.txt", "r");  // File to load world data from

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
	if (!surface || !SDL_LockSurface(surface))
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

static bool LoadGLTextures(APPSTATE *state)
{
	SDL_Surface *TextureImage = NULL;

	// Load & flip the bitmap
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

	// Free temporary surface
	SDL_DestroySurface(TextureImage);
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

/*  Set viewport size and setup matrices         *
 *  width   - Width of the OpenGL framebuffer    *
 *  height  - Height of the OpenGL framebuffer   */
static void ReSizeGLScene(int width, int height)
{
	if (height == 0)                              // Prevent division-by-zero by ensuring height is non-zero
	{
		height = 1;
	}

	glViewport(0, 0, width, height);              // Reset the current viewport

	glMatrixMode(GL_PROJECTION);                  // Select & reset the projection matrix
	glLoadIdentity();

	float aspect = (float)width / (float)height;  // Calculate aspect ratio
	gluPerspective(45.0f, aspect, 0.1f, 100.0f);  // Setup perspective matrix

	glMatrixMode(GL_MODELVIEW);                   // Select & reset the modelview matrix
	glLoadIdentity();
}

static bool InitGL(APPSTATE *state)
{
	if (!LoadGLTextures(state))                         // Load textures
	{
		return false;
	}

	glEnable(GL_TEXTURE_2D);                            // Enable texture mapping
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);                  // Set the blending function for translucency
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);               // Set the background clear color to black
	glClearDepth(1.0);                                  // Ensure depth buffer clears to furthest value
	glDepthFunc(GL_LESS);                               // Pass if pixel depth value tests less than the depth buffer value
	glEnable(GL_DEPTH_TEST);                            // Enable depth testing
	glShadeModel(GL_SMOOTH);                            // Enable Smooth color shading
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);  // Request the implementation to use perspective correct interpolation

	SetupWorld(state);

	return true;
}

static void DrawGLScene(APPSTATE *state)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Clear the color and depth buffers
	glLoadIdentity();                                    // Reset modelview

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

static void KillGLWindow(APPSTATE *state)
{
	// Restore windowed state & cursor visibility
	if (state->fullscreen)
	{
		SDL_SetWindowFullscreen(state->win, false);
		SDL_ShowCursor();
	}

	// Release and delete rendering context
	if (state->ctx)
	{
		if (!SDL_GL_MakeCurrent(state->win, NULL))
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SHUTDOWN ERROR", "Release Of RC Failed.", NULL);
		}

		SDL_GL_DestroyContext(state->ctx);
		state->ctx = NULL;
	}

	SDL_DestroyWindow(state->win);
	state->win = NULL;
}


/*  This code creates our OpenGL window, parameters are:                    *
 *  title           - Title to appear at the top of the window              *
 *  width           - Width of the OpenGL window or fullscreen mode         *
 *  height          - Height of the OpenGL window or fullscreen mode        *
 *  bits            - Number of bits to use for color (8/16/24/32)          *
 *  fullscreenflag  - Use fullscreen mode (true) Or windowed mode (false)   */
static bool CreateGLWindow(APPSTATE *state, char *title, int width, int height, int bits, bool fullscreenflag)
{
	state->fullscreen = fullscreenflag;

	if (!(state->win = SDL_CreateWindow(title, width, height,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY)))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Window Creation Error.", NULL);
		return false;
	}

	// Try entering fullscreen mode if requested
	if (fullscreenflag)
	{
		if (!SDL_SetWindowFullscreen(state->win, true))
		{
			// If mode switching fails, ask the user to quit or use to windowed mode
			int bttnid = ShowYesNoMessageBox(state->win, BTTN_YES, "NeHe GL",
				"The Requested Fullscreen Mode Is Not Supported By\nYour Video Card. Use Windowed Mode Instead?");
			if (bttnid == BTTN_NO)
			{
				// User chose to quit
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "ERROR", "Program Will Now Close.", NULL);
				return false;
			}
			state->fullscreen = false;
		}
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);           // Double buffered framebuffer
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 0);               // Color bits ignored
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);             // No alpha buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);       // No accumulation buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);         // Accumulation bits ignored
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);            // 16-bit Z-buffer (depth buffer)
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);           // No stencil buffer


	if (!(state->ctx = SDL_GL_CreateContext(state->win)))  // Create rendering context
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Create A GL Rendering Context.", NULL);
		return false;
	}

	if (!SDL_GL_MakeCurrent(state->win, state->ctx))       // Activate the rendering context
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Activate The GL Rendering Context.", NULL);
		return false;
	}

	SDL_ShowWindow(state->win);
	SDL_GL_SetSwapInterval(1);                             // Enable VSync
	ReSizeGLScene(width, height);                          // Set up our viewport and perspective

	if (!InitGL(state))                                    // Initialize the scene
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Initialization Failed.", NULL);
		return false;
	}

	return true;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
	APPSTATE *state = (APPSTATE *)appstate;
	switch (event->type)
	{
	case SDL_EVENT_QUIT:                                          // Have we received a quit event?
		return SDL_APP_SUCCESS;

	case SDL_EVENT_KEY_DOWN:
		if (event->key.key == SDLK_ESCAPE)                        // Quit on escape key
		{
			return SDL_APP_SUCCESS;
		}
		if (!event->key.repeat)                                   // Was a key just pressed?
		{
			switch (event->key.key)
			{
			case SDLK_B:                                          // B = Toggle blending
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

			case SDLK_F:                                          // F = Cycle texture filtering
				state->filter += 1;
				if (state->filter > 2)
				{
					state->filter = 0;
				}
				break;

			case SDLK_F1:                                         // F1 = Toggle fullscreen / windowed mode
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

SDL_AppResult SDL_AppIterate(void *appstate)
{
	APPSTATE *state = (APPSTATE *)appstate;
	DrawGLScene(state);             // Draw the scene
	SDL_GL_SwapWindow(state->win);  // Swap buffers (double buffering)

	// Handle keyboard input
	const bool *keys = SDL_GetKeyboardState(NULL);

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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
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
		.blend = false,  // Blending off

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

	// Ask the user if they would like to start in fullscreen or windowed mode
	int bttnid = ShowYesNoMessageBox(state->win, BTTN_NO, "Start FullScreen?",
		"Would You Like To Run In Fullscreen Mode?");

	// Create our OpenGL window
	const bool wantfullscreen = (bttnid == BTTN_YES);
	if (!CreateGLWindow(state, "Lionel Brits & NeHe's 3D World Tutorial", 640, 480, 16, wantfullscreen))
	{
		return SDL_APP_FAILURE;
	}

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	if (appstate)
	{
		APPSTATE *state = (APPSTATE *)appstate;
		free(state->sector1.triangle);
		if (state->ctx)
		{
			glDeleteTextures(3, state->texture);
		}
		KillGLWindow(state);
		free(state);
	}

	SDL_Quit();
}
