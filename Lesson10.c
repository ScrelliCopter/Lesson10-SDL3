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
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>   // Header File For The OpenGL32 Library

SDL_Window*   hWnd = NULL;     // Holds Our Window Handle
SDL_GLContext hRC = NULL;      // Permanent Rendering Context

bool keys[SDL_NUM_SCANCODES];  // Array Used For The Keyboard Routine
bool active = true;            // Window Active Flag Set To TRUE By Default
bool fullscreen = true;        // Fullscreen Flag Set To Fullscreen Mode By Default
bool blend;                    // Blending ON/OFF
bool bp;                       // B Pressed?
bool fp;                       // F Pressed?

static const SDL_MessageBoxButtonData yesnobttns[2] =
{
	{
		/*flags    */ 0,
		/*buttonid */ 0,
		/*text     */ "Yes"
	},
	{
		/*flags    */ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
		/*buttonid */ 1,
		/*text     */ "No"
	}
};

const float piover180 = 0.0174532925f;
float heading;
float xpos;
float zpos;

GLfloat yrot;               // Y Rotation
GLfloat walkbias = 0;
GLfloat walkbiasangle = 0;
GLfloat lookupdown = 0.0f;
GLfloat z = 0.0f;           // Depth Into The Screen

GLuint filter;              // Which Filter To Use
GLuint texture[3];          // Storage For 3 Textures

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
	TRIANGLE* triangle;
} SECTOR;

SECTOR sector1;             // Our Model Goes Here:

void readstr(FILE *f, char *string)
{
	do
	{
		fgets(string, 255, f);
	} while ((string[0] == '/') || (string[0] == '\n'));
	return;
}

void SetupWorld()
{
	float x, y, z, u, v;
	int numtriangles;
	FILE *filein;
	char oneline[255];
	filein = fopen("data/world.txt", "rt");  // File To Load World Data From

	readstr(filein, oneline);
	sscanf(oneline, "NUMPOLLIES %d\n", &numtriangles);

	sector1.triangle = malloc(sizeof(TRIANGLE) * numtriangles);
	sector1.numtriangles = numtriangles;
	for (int loop = 0; loop < numtriangles; loop++)
	{
		for (int vert = 0; vert < 3; vert++)
		{
			readstr(filein, oneline);
			sscanf(oneline, "%f %f %f %f %f", &x, &y, &z, &u, &v);
			sector1.triangle[loop].vertex[vert].x = x;
			sector1.triangle[loop].vertex[vert].y = y;
			sector1.triangle[loop].vertex[vert].z = z;
			sector1.triangle[loop].vertex[vert].u = u;
			sector1.triangle[loop].vertex[vert].v = v;
		}
	}
	fclose(filein);
	return;
}

bool FlipSurface(SDL_Surface *surface)
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

int LoadGLTextures()                    // Load Bitmaps And Convert To Textures
{
	int Status = SDL_FALSE;             // Status Indicator

	SDL_Surface *TextureImage = NULL;   // Create Storage Space For The Texture

	// Load The Bitmap, Check For Errors, If Bitmap's Not Found Quit
	if ((TextureImage = SDL_LoadBMP("Data/Mud.bmp")) && FlipSurface(TextureImage))
	{
		Status = SDL_TRUE;              // Set The Status To TRUE

		glGenTextures(3, &texture[0]);  // Create Three Textures

		// Create Nearest Filtered Texture
		glBindTexture(GL_TEXTURE_2D, texture[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, TextureImage->w, TextureImage->h, 0, GL_BGR, GL_UNSIGNED_BYTE, TextureImage->pixels);

		// Create Linear Filtered Texture
		glBindTexture(GL_TEXTURE_2D, texture[1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, TextureImage->w, TextureImage->h, 0, GL_BGR, GL_UNSIGNED_BYTE, TextureImage->pixels);

		// Create MipMapped Texture
		glBindTexture(GL_TEXTURE_2D, texture[2]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, TextureImage->w, TextureImage->h, 0, GL_BGR, GL_UNSIGNED_BYTE, TextureImage->pixels);
	}
	SDL_DestroySurface(TextureImage);   // Free The Image Structure

	return Status;                      // Return The Status
}

static void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
	double h = 1.0 / tan(fovy * (SDL_PI_D / 180.0) * 0.5);
	double w = h / aspect;
	double invcliprng = 1.0 / (zFar - zNear);
	double z = -(zFar + zNear) * invcliprng;
	double z2 = -(2.0 * zFar * zNear) * invcliprng;
	double mtx[16] =
	{
		w,   0.0, 0.0, 0.0,
		0.0, h,   0.0, 0.0,
		0.0, 0.0, z,  -1.0,
		0.0, 0.0, z2,  0.0
	};
	glLoadMatrixd(mtx);
}

GLvoid ReSizeGLScene(GLsizei width, GLsizei height)      // Resize And Initialize The GL Window
{
	if (height == 0)                                     // Prevent A Divide By Zero By
	{
		height = 1;                                      // Making Height Equal One
	}

	glViewport(0, 0, width, height);                     // Reset The Current Viewport

	glMatrixMode(GL_PROJECTION);                         // Select The Projection Matrix
	glLoadIdentity();                                    // Reset The Projection Matrix

	// Calculate The Aspect Ratio Of The Window
	gluPerspective(45.0f, (GLfloat)width / (GLfloat)height, 0.1f, 100.0f);

	glMatrixMode(GL_MODELVIEW);                          // Select The Modelview Matrix
	glLoadIdentity();                                    // Reset The Modelview Matrix
}

int InitGL()                                             // All Setup For OpenGL Goes Here
{
	if (!LoadGLTextures())                               // Jump To Texture Loading Routine
	{
		return 0;                                        // If Texture Didn't Load Return FALSE
	}

	glEnable(GL_TEXTURE_2D);                             // Enable Texture Mapping
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);                   // Set The Blending Function For Translucency
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);                // This Will Clear The Background Color To Black
	glClearDepth(1.0);                                   // Enables Clearing Of The Depth Buffer
	glDepthFunc(GL_LESS);                                // The Type Of Depth Test To Do
	glEnable(GL_DEPTH_TEST);                             // Enables Depth Testing
	glShadeModel(GL_SMOOTH);                             // Enables Smooth Color Shading
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);   // Really Nice Perspective Calculations

	SetupWorld();

	return 1;                                            // Initialization Went OK
}

int DrawGLScene()                                        // Here's Where We Do All The Drawing
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Clear The Screen And The Depth Buffer
	glLoadIdentity();                                    // Reset The View

	GLfloat x_m, y_m, z_m, u_m, v_m;
	GLfloat xtrans = -xpos;
	GLfloat ztrans = -zpos;
	GLfloat ytrans = -walkbias - 0.25f;
	GLfloat sceneroty = 360.0f - yrot;

	int numtriangles;

	glRotatef(lookupdown, 1.0f, 0.0f, 0.0f);
	glRotatef(sceneroty, 0.0f, 1.0f, 0.0f);
	
	glTranslatef(xtrans, ytrans, ztrans);
	glBindTexture(GL_TEXTURE_2D, texture[filter]);
	
	numtriangles = sector1.numtriangles;
	
	// Process Each Triangle
	for (int loop_m = 0; loop_m < numtriangles; loop_m++)
	{
		glBegin(GL_TRIANGLES);
			glNormal3f(0.0f, 0.0f, 1.0f);
			x_m = sector1.triangle[loop_m].vertex[0].x;
			y_m = sector1.triangle[loop_m].vertex[0].y;
			z_m = sector1.triangle[loop_m].vertex[0].z;
			u_m = sector1.triangle[loop_m].vertex[0].u;
			v_m = sector1.triangle[loop_m].vertex[0].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);
			
			x_m = sector1.triangle[loop_m].vertex[1].x;
			y_m = sector1.triangle[loop_m].vertex[1].y;
			z_m = sector1.triangle[loop_m].vertex[1].z;
			u_m = sector1.triangle[loop_m].vertex[1].u;
			v_m = sector1.triangle[loop_m].vertex[1].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);
			
			x_m = sector1.triangle[loop_m].vertex[2].x;
			y_m = sector1.triangle[loop_m].vertex[2].y;
			z_m = sector1.triangle[loop_m].vertex[2].z;
			u_m = sector1.triangle[loop_m].vertex[2].u;
			v_m = sector1.triangle[loop_m].vertex[2].v;
			glTexCoord2f(u_m, v_m); glVertex3f(x_m, y_m, z_m);
		glEnd();
	}
	return 1;                                            // Everything Went OK
}

GLvoid KillGLWindow()                        // Properly Kill The Window
{
	if (fullscreen)                          // Are We In Fullscreen Mode?
	{
		SDL_SetWindowFullscreen(hWnd, 0);    // If So Switch Back To The Desktop
		SDL_ShowCursor();                    // Show Mouse Pointer
	}

	if (hRC)                                 // Do We Have A Rendering Context?
	{
		if (SDL_GL_MakeCurrent(hWnd, NULL))  // Are We Able To Release The DC And RC Contexts?
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SHUTDOWN ERROR", "Release Of RC Failed.", NULL);
		}

		SDL_GL_DeleteContext(hRC);           // Delete The RC
		hRC = NULL;                          // Set RC To NULL
	}

	SDL_DestroyWindow(hWnd);                 // Destroy The Window
	hWnd = NULL;                             // Set hWnd To NULL
}


/*  This Code Creates Our OpenGL Window.  Parameters Are:                   *
 *  title           - Title To Appear At The Top Of The Window              *
 *  width           - Width Of The GL Window Or Fullscreen Mode             *
 *  height          - Height Of The GL Window Or Fullscreen Mode            *
 *  bits            - Number Of Bits To Use For Color (8/16/24/32)          *
 *  fullscreenflag  - Use Fullscreen Mode (TRUE) Or Windowed Mode (FALSE)   */


bool CreateGLWindow(char* title, int width, int height, int bits, bool fullscreenflag)
{
	SDL_Rect WindowRect;                                  // Grabs Rectangle Upper Left / Lower Right Values
	WindowRect.x = 0;                                     // Set Left Value To 0
	WindowRect.w = width;                                 // Set Right Value To Requested Width
	WindowRect.y = 0;                                     // Set Top Value To 0
	WindowRect.h = height;                                // Set Bottom Value To Requested Height

	fullscreen = fullscreenflag;                          // Set The Global Fullscreen Flag

	// Create The Window
	if (!(hWnd = SDL_CreateWindow(title,
		WindowRect.w,                                     // Window Width
		WindowRect.h,                                     // Window Height
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY)))
	{
		KillGLWindow();                                   // Reset The Display
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Window Creation Error.", NULL);
		return false;                                     // Return FALSE
	}

	if (fullscreen)                                       // Attempt Fullscreen Mode?
	{
		if (SDL_SetWindowFullscreen(hWnd, SDL_TRUE) < 0)  // Try To Set Selected Mode And Get Results.
		{
			// If The Mode Fails, Offer Two Options.  Quit Or Use Windowed Mode.
			const SDL_MessageBoxData msgbox =
			{
				SDL_MESSAGEBOX_INFORMATION,
				hWnd,
				"NeHe GL",
				"The Requested Fullscreen Mode Is Not Supported By\nYour Video Card. Use Windowed Mode Instead?",
				2,
				yesnobttns,
				NULL
			};
			int bttnid = 0;
			SDL_ShowMessageBox(&msgbox, &bttnid);
			if (bttnid == 0)
			{
				fullscreen = false;                       // Windowed Mode Selected.  Fullscreen = FALSE
			}
			else
			{
				// Pop Up A Message Box Letting User Know The Program Is Closing.
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "ERROR", "Program Will Now Close.", NULL);
				return false;                             // Return FALSE
			}
		}
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);        // Must Support Double Buffering
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);          // Select Our Color Depth
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 0);            // Color Bits Ignored
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);          // No Alpha Buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);    // No Accumulation Buffer
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);      // Accumulation Bits Ignored
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);         // 16Bit Z-Buffer (Depth Buffer)
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);        // No Stencil Buffer


	if (!(hRC = SDL_GL_CreateContext(hWnd)))            // Are We Able To Get A Rendering Context?
	{
		KillGLWindow();                                 // Reset The Display
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Create A GL Rendering Context.", NULL);
		return false;                                   // Return FALSE
	}

	if (SDL_GL_MakeCurrent(hWnd, hRC))                  // Try To Activate The Rendering Context
	{
		KillGLWindow();                                 // Reset The Display
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Activate The GL Rendering Context.", NULL);
		return false;                                   // Return FALSE
	}

	SDL_ShowWindow(hWnd);
	SDL_GL_SetSwapInterval(1);
	ReSizeGLScene(width, height);                       // Set Up Our Perspective GL Screen

	if (!InitGL())                                      // Initialize Our Newly Created GL Window
	{
		KillGLWindow();                                 // Reset The Display
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Initialization Failed.", NULL);
		return false;                                   // Return FALSE
	}

	return true;                                        // Success
}

void WndProc(SDL_Event* uMsg)
{
	switch (uMsg->type)                                             // Check For Windows Messages
	{
		case SDL_EVENT_KEY_DOWN:                                    // Is A Key Being Held Down?
		{
			keys[uMsg->key.keysym.scancode] = true;                 // If So, Mark It As TRUE
			break;                                                  // Jump Back
		}

		case SDL_EVENT_KEY_UP:                                      // Has A Key Been Released?
		{
			keys[uMsg->key.keysym.scancode] = false;                // If So, Mark It As FALSE
			break;                                                  // Jump Back
		}

		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:                   // Resize The OpenGL Window
		{
			ReSizeGLScene(uMsg->window.data1, uMsg->window.data2);  // LoWord=Width, HiWord=Height
			break;                                                  // Jump Back
		}
	}
}

int main(int argc, char* argv[])
{
	SDL_Event msg;                                  // Windows Message Structure
	bool done = false;                              // Bool Variable To Exit Loop

	SDL_Init(SDL_INIT_VIDEO);

	// Ask The User Which Screen Mode They Prefer
	const SDL_MessageBoxData msgbox =
	{
		/* flags       */ SDL_MESSAGEBOX_INFORMATION,
		/* window      */ hWnd,
		/* title       */ "Start FullScreen?",
		/* message     */ "Would You Like To Run In Fullscreen Mode?",
		/* numbuttons  */ 2,
		/* buttons     */ yesnobttns,
		/* colorScheme */ NULL
	};
	int bttnid = 1;
	SDL_ShowMessageBox(&msgbox, &bttnid);
	if (bttnid == 1)
	{
		fullscreen = false;                         // Windowed Mode
	}

	// Create Our OpenGL Window
	if (!CreateGLWindow("Lionel Brits & NeHe's 3D World Tutorial", 640, 480, 16, fullscreen))
	{
		return 0;                                   // Quit If Window Was Not Created
	}

	while (!done)                                   // Loop That Runs While done=FALSE
	{
		if (SDL_PollEvent(&msg) > 0)                // Is There A Message Waiting?
		{
			if (msg.type == SDL_EVENT_QUIT)         // Have We Received A Quit Message?
			{
				done = true;                        // If So done=TRUE
			}
			else                                    // If Not, Deal With Window Messages
			{
				WndProc(&msg);
			}
		}
		else                                        // If There Are No Messages
		{
			// Draw The Scene.  Watch For ESC Key And Quit Messages From DrawGLScene()
			if ((active && !DrawGLScene()) || keys[SDL_SCANCODE_ESCAPE])  // Active?  Was There A Quit Received?
			{
				done = true;                        // ESC or DrawGLScene Signalled A Quit
			}
			else                                    // Not Time To Quit, Update Screen
			{
				SDL_GL_SwapWindow(hWnd);            // Swap Buffers (Double Buffering)
				if (keys[SDL_SCANCODE_B] && !bp)
				{
					bp = true;
					blend = !blend;
					if (!blend)
					{
						glDisable(GL_BLEND);
						glEnable(GL_DEPTH_TEST);
					}
					else
					{
						glEnable(GL_BLEND);
						glDisable(GL_DEPTH_TEST);
					}
				}
				if (!keys[SDL_SCANCODE_B])
				{
					bp = false;
				}

				if (keys[SDL_SCANCODE_F] && !fp)
				{
					fp = true;
					filter += 1;
					if (filter > 2)
					{
						filter = 0;
					}
				}
				if (!keys[SDL_SCANCODE_F])
				{
					fp = false;
				}

				if (keys[SDL_SCANCODE_PAGEUP])
				{
					z -= 0.02f;
				}

				if (keys[SDL_SCANCODE_PAGEDOWN])
				{
					z += 0.02f;
				}

				if (keys[SDL_SCANCODE_UP])
				{

					xpos -= (float)sin(heading * piover180) * 0.05f;
					zpos -= (float)cos(heading * piover180) * 0.05f;
					if (walkbiasangle >= 359.0f)
					{
						walkbiasangle = 0.0f;
					}
					else
					{
						walkbiasangle += 10;
					}
					walkbias = (float)sin(walkbiasangle * piover180) / 20.0f;
				}

				if (keys[SDL_SCANCODE_DOWN])
				{
					xpos += (float)sin(heading*piover180) * 0.05f;
					zpos += (float)cos(heading*piover180) * 0.05f;
					if (walkbiasangle <= 1.0f)
					{
						walkbiasangle = 359.0f;
					}
					else
					{
						walkbiasangle -= 10;
					}
					walkbias = (float)sin(walkbiasangle * piover180) / 20.0f;
				}

				if (keys[SDL_SCANCODE_RIGHT])
				{
					heading -= 1.0f;
					yrot = heading;
				}

				if (keys[SDL_SCANCODE_LEFT])
				{
					heading += 1.0f;
					yrot = heading;
				}

				if (keys[SDL_SCANCODE_PAGEUP])
				{
					lookupdown -= 1.0f;
				}

				if (keys[SDL_SCANCODE_PAGEDOWN])
				{
					lookupdown += 1.0f;
				}

				if (keys[SDL_SCANCODE_F1])          // Is F1 Being Pressed?
				{
					keys[SDL_SCANCODE_F1] = false;  // If So Make Key FALSE
					KillGLWindow();                 // Kill Our Current Window
					fullscreen = !fullscreen;       // Toggle Fullscreen / Windowed Mode
					// Recreate Our OpenGL Window
					if (!CreateGLWindow("Lionel Brits & NeHe's 3D World Tutorial", 640, 480, 16, fullscreen))
					{
						return 0;                   // Quit If Window Was Not Created
					}
				}
			}
		}
	}

	// Shutdown
	KillGLWindow();       // Kill The Window
	SDL_Quit();
	return EXIT_SUCCESS;  // Exit The Program
}
