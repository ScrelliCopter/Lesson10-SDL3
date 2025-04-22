/*
 *  This code was created by Lionel Brits & Jeff Molofee 2000.
 *  A HUGE thanks to Fredric Echols for cleaning up
 *  and optimizing the base code, making it more flexible!
 *  If you've found this code useful, please let me know.
 *  Visit my site at https://nehe.gamedev.net
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

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

typedef float mat4f[16];
typedef float mat3f[9];

typedef struct tagAPPSTATE
{
	SDL_Window              *win;
	SDL_GPUDevice           *dev;
	SDL_GPUGraphicsPipeline *pso, *psoblend;

	const char *resdir;

	bool fullscreen, blend;

	mat4f projmtx;               // Projection matrix
	CAMERA camera;
	unsigned filter;             // Filtered texture selection
	unsigned depthtexw, depthtexh; // Width and height for the depth texture
	SDL_GPUTexture *depthtex;    // Texture used for depth testing
	SDL_GPUTexture *texture;     // World texture
	SDL_GPUSampler *samplers[3]; // Filtered samplers
	SDL_GPUBuffer *worldmesh;    // GPU world mesh

	SECTOR sector1;
} APPSTATE;

static char * resourcePath(const APPSTATE *restrict state, const char *restrict name)
{
	if (!state || !name)
	{
		return NULL;
	}
	size_t resdirLen = strlen(state->resdir), nameLen = strlen(name);
	char *path = malloc(resdirLen + nameLen + 1);
	if (!path)
	{
		return NULL;
	}
	memcpy(path, state->resdir, resdirLen);
	memcpy(&path[resdirLen], name, nameLen);
	path[resdirLen + nameLen] = '\0';
	return path;
}

static FILE * fopenResource(const APPSTATE *restrict state, const char *restrict name, const char* restrict mode)
{
	char *path = NULL;
	if (!mode || !(path = resourcePath(state, name)))
	{
		return NULL;
	}
	FILE *f = fopen(path, mode);
	free(path);
	return f;
}

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
	filein = fopenResource(state, "Data/World.txt", "r");  // File to load world data from

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

static SDL_GPUBuffer * CreateStaticMesh(APPSTATE *state, const VERTEX *vertices, size_t numvertices)
{
	const Uint32 bufsize = sizeof(VERTEX) * (Uint32)numvertices;

	// Create vertex data buffer
	const SDL_GPUBufferCreateInfo bufinfo =
	{
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = bufsize,
		.props = 0
	};
	SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(state->dev, &bufinfo);
	if (!buf)
	{
		return NULL;
	}

	// Create transfer buffer
	const SDL_GPUTransferBufferCreateInfo xferinfo =
	{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = bufsize,
		.props = 0
	};
	SDL_GPUTransferBuffer *xferbuf = SDL_CreateGPUTransferBuffer(state->dev, &xferinfo);
	if (!xferbuf)
	{
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return NULL;
	}

	// Map transfer buffer and copy the vertex data
	void *map = SDL_MapGPUTransferBuffer(state->dev, xferbuf, false);
	if (!map)
	{
		SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return NULL;
	}
	SDL_memcpy(map, (const void *)vertices, (size_t)bufsize);
	SDL_UnmapGPUTransferBuffer(state->dev, xferbuf);

	// Upload the vertex data into the GPU buffer
	SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(state->dev);
	if (!cmdbuf)
	{
		SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return NULL;
	}
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmdbuf);
	const SDL_GPUTransferBufferLocation source = { .transfer_buffer = xferbuf, .offset = 0 };
	const SDL_GPUBufferRegion dest = { .buffer = buf, .offset = 0, .size = bufsize };
	SDL_UploadToGPUBuffer(pass, &source, &dest, false);
	SDL_EndGPUCopyPass(pass);
	SDL_SubmitGPUCommandBuffer(cmdbuf);
	SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);

	return buf;
}

static bool CreateWorldMesh(APPSTATE *state)
{
	const int numtriangles = state->sector1.numtriangles;
	const size_t numvertices = 3 * (size_t)numtriangles;

	VERTEX *vertices = SDL_malloc(sizeof(VERTEX) * numvertices);
	if (!vertices)
	{
		return false;
	}

	size_t index = 0;
	for (int loop_m = 0; loop_m < numtriangles; loop_m++)
	{
		vertices[index++] = state->sector1.triangle[loop_m].vertex[0];
		vertices[index++] = state->sector1.triangle[loop_m].vertex[1];
		vertices[index++] = state->sector1.triangle[loop_m].vertex[2];
	}
	state->worldmesh = CreateStaticMesh(state, vertices, numvertices);
	SDL_free(vertices);
	return state->worldmesh != NULL;
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

static SDL_GPUShader * LoadShader(APPSTATE *state, const char *path, SDL_GPUShaderFormat format, bool isfragment)
{
	FILE *filein = fopenResource(state, path, "rb");
	if (!filein)
	{
		return false;
	}

	// Read shader code into a buffer
	long libsize; Uint8 *libdata;
	fseek(filein, 0, SEEK_END);
	if ((libsize = ftell(filein)) <= 0 ||
		!(libdata = SDL_malloc((size_t)libsize)))
	{
		fclose(filein);
		return NULL;
	}
	fseek(filein, 0, SEEK_SET);
	fread((void *)libdata, 1, libsize, filein);

	// Create shader object
	const SDL_GPUShaderCreateInfo desc =
	{
		.num_samplers = isfragment ? 1 : 0,
		.num_storage_textures = 0,
		.num_storage_buffers = 0,
		.num_uniform_buffers = isfragment ? 0 : 1,
		.format = format,
		.entrypoint = isfragment ? "FragmentMain" : "VertexMain",
		.code = libdata,
		.code_size = (size_t)libsize,
		.stage = isfragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX
	};
	SDL_GPUShader *shader = SDL_CreateGPUShader(state->dev, &desc);

	SDL_free(libdata);
	fclose(filein);
	return shader;
}

static bool LoadShaders(APPSTATE *state, SDL_GPUShader **vertexshader, SDL_GPUShader **fragmentshader)
{
	SDL_GPUShader *vtxshader = NULL, *frgshader = NULL;
	const SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_MSL;
	vtxshader = LoadShader(state, "Data/shader.vertex.metal", format, false);
	frgshader = LoadShader(state, "Data/shader.fragment.metal", format, true);

	if (!vtxshader || !frgshader)
	{
		if (vtxshader) SDL_ReleaseGPUShader(state->dev, vtxshader);
		if (frgshader) SDL_ReleaseGPUShader(state->dev, frgshader);
		return false;
	}

	*vertexshader = vtxshader;
	*fragmentshader = frgshader;
	return true;
}

static bool CreateDepthTexture(APPSTATE *state, unsigned width, unsigned height)
{
	if (state->depthtex)
	{
		SDL_ReleaseGPUTexture(state->dev, state->depthtex);
		state->depthtex = NULL;
	}

	SDL_GPUTexture *newtex = SDL_CreateGPUTexture(state->dev, &(SDL_GPUTextureCreateInfo)
	{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
		.width = width,
		.height = height,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.sample_count = SDL_GPU_SAMPLECOUNT_1,
		.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
		.props = 0
	});
	if (!newtex)
	{
		return false;
	}

	SDL_SetGPUTextureName(state->dev, newtex, "Depth Texture");
	state->depthtex  = newtex;
	state->depthtexw = width;
	state->depthtexh = height;
	return true;
}

static SDL_GPUTexture * CreateTextureFromSurface(APPSTATE *state, const SDL_Surface *image)
{
	const int width = image->w, height = image->h, depth = 1;
	const Uint32 datasize = 4 * width * height;

	// Convert the input surface into RGBA
	void *converted = SDL_malloc((size_t)datasize);
	if (!SDL_ConvertPixels(width, height,
		image->format, image->pixels, image->pitch,
		SDL_PIXELFORMAT_ABGR8888, converted, 4 * width))
	{
		SDL_free(converted);
		return NULL;
	}

	const SDL_GPUTextureCreateInfo info =
	{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.width = image->w,
		.height = image->h,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	};
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(state->dev, &info);

	// Create and copy image data to a transfer buffer
	SDL_GPUTransferBuffer *xferbuf = SDL_CreateGPUTransferBuffer(state->dev, &(SDL_GPUTransferBufferCreateInfo)
	{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = datasize
	});
	void *map = SDL_MapGPUTransferBuffer(state->dev, xferbuf, false);
	SDL_memcpy(map, converted, (size_t)datasize);
	SDL_UnmapGPUTransferBuffer(state->dev, xferbuf);

	// Upload the transfer data to the GPU resources
	SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(state->dev);
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmdbuf);
	const SDL_GPUTextureTransferInfo source = { .transfer_buffer = xferbuf, .offset = 0 };
	const SDL_GPUTextureRegion dest =
	{
		.texture = texture,
		.w = width,
		.h = height,
		.d = depth
	};
	SDL_UploadToGPUTexture(pass, &source, &dest, false);
	SDL_EndGPUCopyPass(pass);
	SDL_SubmitGPUCommandBuffer(cmdbuf);
	SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);
	SDL_free(converted);

	return texture;
}

static bool LoadTexture(APPSTATE *state)
{
	// Load & flip the bitmap
	const char *resname = "Data/Mud.bmp";
	char *path = resourcePath(state, resname);
	if (!path)
	{
		return false;
	}
	SDL_Surface *TextureImage = SDL_LoadBMP(path);
	free(path);
	if (!TextureImage || !FlipSurface(TextureImage))
	{
		SDL_DestroySurface(TextureImage);
		return false;
	}

	// Create texture
	state->texture = CreateTextureFromSurface(state, TextureImage);
	SDL_SetGPUTextureName(state->dev, state->texture, resname);

	// Free temporary surface
	SDL_DestroySurface(TextureImage);

	return state->texture != NULL;
}

static bool CreateGPUSamplers(APPSTATE *state)
{
	const SDL_GPUSamplerCreateInfo params[3] =
	{
		[0] =  // Nearest filtered
		{
			.min_filter = SDL_GPU_FILTER_NEAREST,
			.mag_filter = SDL_GPU_FILTER_NEAREST,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
		},
		[1] =  // Linear filtered
		{
			.min_filter = SDL_GPU_FILTER_LINEAR,
			.mag_filter = SDL_GPU_FILTER_LINEAR,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
		},
		[2] =  // MipMapped
		{
			.min_filter = SDL_GPU_FILTER_LINEAR,
			.mag_filter = SDL_GPU_FILTER_LINEAR,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
		}
	};

	for (unsigned i = 0; i < SDL_arraysize(state->samplers); ++i)
	{
		SDL_GPUSamplerCreateInfo info = params[i];
		info.address_mode_u = info.address_mode_v = info.address_mode_w
			= SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		if (!(state->samplers[i] = SDL_CreateGPUSampler(state->dev, &info)))
		{
			return false;
		}
	}

	return true;
}

#define M4_IDENTITY { \
	1, 0, 0, 0, \
	0, 1, 0, 0, \
	0, 0, 1, 0, \
	0, 0, 0, 1 }

static void MulMatrices(mat4f mtx, mat4f lhs, mat4f rhs)
{
	int i = 0;
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			float a = 0.f;
			for (int j = 0; j < 4; ++j)
			{
				a += lhs[j * 4 + row] * rhs[col * 4 + j];
			}
			mtx[i++] = a;
		}
	}
}

static void MakePerspective(mat4f m, float fovy, float aspect, float near, float far)
{
	const float h = 1.f / SDL_tanf(fovy * (SDL_PI_F / 180.f) * 0.5f);
	const float w = h / aspect;
	const float invcliprng = 1.f / (far - near);
	const float zh = -(far + near) * invcliprng;
	const float zl = -(2.f * far * near) * invcliprng;

	/*
	  [w  0  0  0]
	  [0  h  0  0]
	  [0  0 zh zl]
	  [0  0 -1  0]
	*/
	m[1] = m[2] = m[3] = m[4] = m[6] = m[7] = m[8] = m[9] = m[12] = m[13] = m[15] = 0.f;
	m[0]  =    w;
	m[5]  =    h;
	m[10] =   zh;
	m[14] =   zl;
	m[11] = -1.f;
}

static void MakeRotation(mat3f m, float theta, float x, float y, float z)
{
	const float c = SDL_cosf(theta), s = SDL_sinf(theta);
	const float rc = 1.f - c;
	const float rcx = x * rc, rcy = y * rc, rcz = z * rc;
	const float sx = x * s, sy = y * s, sz = z * s;

	m[0] = rcx * x + c;
	m[3] = rcx * y - sz;
	m[6] = rcx * z + sy;

	m[1] = rcy * x + sz;
	m[4] = rcy * y + c;
	m[7] = rcy * z - sx;

	m[2] = rcz * x - sy;
	m[5] = rcz * y + sx;
	m[8] = rcz * z + c;
}

static void Rotate(mat4f m, float angle, float x, float y, float z)
{
	// Treat inputs like glRotatef
	const float theta = angle * SDL_PI_F / 180.f;
	const float axismag = SDL_sqrtf(x * x + y * y + z * z);
	if (SDL_fabsf(axismag - 1.f) > SDL_FLT_EPSILON)
	{
		x /= axismag;
		y /= axismag;
		z /= axismag;
	}

	// Set up temporaries
	float tmp[12], r[9];
	SDL_memcpy(tmp, m, sizeof(float) * 12);
	MakeRotation(r, theta, x, y, z);

	// Partial matrix multiplication
	m[0]  = r[0] * tmp[0] + r[1] * tmp[4] + r[2] * tmp[8];
	m[1]  = r[0] * tmp[1] + r[1] * tmp[5] + r[2] * tmp[9];
	m[2]  = r[0] * tmp[2] + r[1] * tmp[6] + r[2] * tmp[10];
	m[3]  = r[0] * tmp[3] + r[1] * tmp[7] + r[2] * tmp[11];
	m[4]  = r[3] * tmp[0] + r[4] * tmp[4] + r[5] * tmp[8];
	m[5]  = r[3] * tmp[1] + r[4] * tmp[5] + r[5] * tmp[9];
	m[6]  = r[3] * tmp[2] + r[4] * tmp[6] + r[5] * tmp[10];
	m[7]  = r[3] * tmp[3] + r[4] * tmp[7] + r[5] * tmp[11];
	m[8]  = r[6] * tmp[0] + r[7] * tmp[4] + r[8] * tmp[8];
	m[9]  = r[6] * tmp[1] + r[7] * tmp[5] + r[8] * tmp[9];
	m[10] = r[6] * tmp[2] + r[7] * tmp[6] + r[8] * tmp[10];
	m[11] = r[6] * tmp[3] + r[7] * tmp[7] + r[8] * tmp[11];
}

static void Translate(float m[16], float x, float y, float z)
{
	/*
	  m = { [1 0 0 x]
	        [0 1 0 y]
	        [0 0 1 z]
	        [0 0 0 1] } * m
	*/
	m[12] += x * m[0] + y * m[4] + z * m[8];
	m[13] += x * m[1] + y * m[5] + z * m[9];
	m[14] += x * m[2] + y * m[6] + z * m[10];
	m[15] += x * m[3] + y * m[7] + z * m[11];
}

/*  Recalculate projection matrix         *
 *  width   - Width of the framebuffer    *
 *  height  - Height of the framebuffer   */
static void ReSizeScene(APPSTATE *state, int width, int height)
{
	if (height == 0)                                    // Prevent division-by-zero by ensuring height is non-zero
	{
		height = 1;
	}

	float aspect = (float)width / (float)height;        // Calculate aspect ratio
	MakePerspective(state->projmtx, 45.0f, aspect, 0.1f, 100.0f);  // Setup perspective matrix
}

static SDL_GPUGraphicsPipeline *MakePipeline(APPSTATE *state, SDL_GPUShader *vtxshader, SDL_GPUShader *frgshader, bool blend)
{
	const SDL_GPUColorTargetBlendState blendstate =
	{
		.enable_blend = true,
		.color_blend_op = SDL_GPU_BLENDOP_ADD,
		.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
		.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
		.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
		.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
		.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE
	};
	const SDL_GPUColorTargetBlendState noblend = { .enable_blend = false };

	const SDL_GPUVertexAttribute vtxattribs[2] =
	{
		{
			.location = 0,
			.buffer_slot = 0,
			.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
			.offset = offsetof(VERTEX, x)
		},
		{
			.location = 1,
			.buffer_slot = 0,
			.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
			.offset = offsetof(VERTEX, u)
		}
	};

	const bool hasdepthtest = blend ? false : true;

	const SDL_GPUGraphicsPipelineCreateInfo info =
	{
		.vertex_shader = vtxshader,
		.fragment_shader = frgshader,
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.vertex_input_state =
		{
			.num_vertex_buffers = 1,
			.vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription)
			{
				.slot = 0,
				.pitch = sizeof(VERTEX),
				.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
			},
			.num_vertex_attributes = SDL_arraysize(vtxattribs),
			.vertex_attributes = vtxattribs
		},
		.rasterizer_state =
		{
			.fill_mode = SDL_GPU_FILLMODE_FILL,
			.cull_mode = SDL_GPU_CULLMODE_NONE,
			.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE  // Right-handed coordinates
		},
		.depth_stencil_state =
		{
			.compare_op = SDL_GPU_COMPAREOP_LESS,  // Pass if pixel depth value tests less than the depth buffer value
			.enable_depth_test = hasdepthtest,             // Enable depth testing
			.enable_depth_write = hasdepthtest
		},
		.target_info =
		{
			.num_color_targets = 1,
			.color_target_descriptions = &(SDL_GPUColorTargetDescription)
			{
				.format = SDL_GetGPUSwapchainTextureFormat(state->dev, state->win),
				.blend_state = blend ? blendstate : noblend  // Set the blending function for translucency
			},
			.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
			.has_depth_stencil_target = true
		}
	};
	return SDL_CreateGPUGraphicsPipeline(state->dev, &info);
}

static bool InitGPU(APPSTATE *state)
{
	if (!LoadTexture(state))                          // Load texture
	{
		return false;
	}
	if (!CreateGPUSamplers(state))                    // Create texture samplers
	{
		return false;
	}
	SDL_GPUShader *vtxshader, *frgshader;
	if (!LoadShaders(state, &vtxshader, &frgshader))  // Load shaders
	{
		return false;
	}

	state->pso = MakePipeline(state, vtxshader, frgshader, false);
	state->psoblend = MakePipeline(state, vtxshader, frgshader, true);
	SDL_ReleaseGPUShader(state->dev, frgshader);
	SDL_ReleaseGPUShader(state->dev, vtxshader);
	if (!state->pso || !state->psoblend)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUGraphicsPipeline(): %s", SDL_GetError());
		return false;
	}

	unsigned backbufw, backbufh;
	SDL_GetWindowSizeInPixels(state->win, (int *)&backbufw, (int *)&backbufh);
	if (!CreateDepthTexture(state, backbufw, backbufh))
	{
		return false;
	}

	SetupWorld(state);
	if (!CreateWorldMesh(state))
	{
		return false;
	}

	return true;
}

static void DrawScene(APPSTATE *state)
{
	SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(state->dev);

	SDL_GPUTexture* backbuftex = NULL;
	Uint32 backbufw, backbufh;
	SDL_AcquireGPUSwapchainTexture(cmdbuf, state->win, &backbuftex, &backbufw, &backbufh);

	if (!backbuftex)
	{
		SDL_CancelGPUCommandBuffer(cmdbuf);
		return;
	}

	float xtrans = -state->camera.xpos;
	float ztrans = -state->camera.zpos;
	float ytrans = -state->camera.walkbias - 0.25f;
	float sceneroty = 360.0f - state->camera.yrot;

	mat4f modelview = M4_IDENTITY;
	Rotate(modelview, state->camera.lookupdown, 1.0f, 0.0f, 0.0f);
	Rotate(modelview, sceneroty, 0.0f, 1.0f, 0.0f);
	Translate(modelview, xtrans, ytrans, ztrans);

	mat4f viewproj;
	MulMatrices(viewproj, state->projmtx, modelview);
	SDL_PushGPUVertexUniformData(cmdbuf, 0, &viewproj, sizeof(mat4f));

	if (!state->depthtex || state->depthtexw != backbufw || state->depthtexh != backbufh)
	{
		CreateDepthTexture(state, backbufw, backbufh);
	}

	SDL_GPUColorTargetInfo colorinfo;
	SDL_zero(colorinfo);
	colorinfo.texture = backbuftex;
	colorinfo.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 0.0f };  // Set the background clear color to black
	colorinfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorinfo.store_op = SDL_GPU_STOREOP_STORE;

	SDL_GPUDepthStencilTargetInfo depthinfo;
	SDL_zero(depthinfo);
	depthinfo.texture = state->depthtex;
	depthinfo.clear_depth = 1.0f;  // Ensure depth buffer clears to furthest value
	depthinfo.load_op = SDL_GPU_LOADOP_CLEAR;
	depthinfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
	depthinfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depthinfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depthinfo.cycle = true;

	// Draw world
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &colorinfo, 1, &depthinfo);
	SDL_BindGPUGraphicsPipeline(pass, state->blend ? state->psoblend : state->pso);
	SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
			.texture = state->texture,
			.sampler = state->samplers[state->filter]
		}, 1);
	Uint32 numvertices = 3u * state->sector1.numtriangles;
	SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){
			.buffer = state->worldmesh, .offset = 0
		}, 1);
	SDL_DrawGPUPrimitives(pass, numvertices, 1, 0, 0);

	SDL_EndGPURenderPass(pass);
	SDL_SubmitGPUCommandBuffer(cmdbuf);
}

static void KillGPUWindow(APPSTATE *state)
{
	// Restore windowed state & cursor visibility
	if (state->fullscreen)
	{
		SDL_SetWindowFullscreen(state->win, false);
		SDL_ShowCursor();
	}

	// Release and delete rendering context
	if (state->dev)
	{
		SDL_ReleaseWindowFromGPUDevice(state->dev, state->win);
		SDL_DestroyGPUDevice(state->dev);
		state->dev = NULL;
	}

	SDL_DestroyWindow(state->win);
	state->win = NULL;
}


/*  This code creates our SDL window, parameters are:                       *
 *  title           - Title to appear at the top of the window              *
 *  width           - Width of the SDL window or fullscreen mode            *
 *  height          - Height of the SDL window or fullscreen mode           *
 *  fullscreenflag  - Use fullscreen mode (true) Or windowed mode (false)   */
static bool CreateGPUWindow(APPSTATE *state, char *title, int width, int height, bool fullscreenflag)
{
	state->fullscreen = fullscreenflag;

	if (!(state->win = SDL_CreateWindow(title, width, height,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY)))
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
			int bttnid = ShowYesNoMessageBox(state->win, BTTN_YES, "NeHe SDL_GPU",
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

	if (!(state->dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, NULL)))  // Create rendering device
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Create A GPU Rendering Context.", NULL);
		return false;
	}

	if (!SDL_ClaimWindowForGPUDevice(state->dev, state->win))  // Attach GPU device to window
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", "Can't Activate The GPU Rendering Context.", NULL);
		return false;
	}

	SDL_ShowWindow(state->win);
	SDL_SetGPUSwapchainParameters(state->dev, state->win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);  // Enable VSync
	ReSizeScene(state, width, height);                   // Set up our viewport and perspective

	if (!InitGPU(state))                                    // Initialize the scene
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
		ReSizeScene(state, event->window.data1, event->window.data2);  // data1=Backbuffer Width, data2=Backbuffer Height
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
	DrawScene(state);             // Draw the scene

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
		.dev = NULL,
		.pso = NULL,

		.resdir = SDL_GetBasePath(),

		.fullscreen = false,
		.blend = false,  // Blending off

		.projmtx = M4_IDENTITY,
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
		.depthtexw = 0,
		.depthtexh = 0,
		.depthtex = NULL,
		.texture = NULL,
		.samplers = { NULL, NULL, NULL },
		.worldmesh = NULL,
		.sector1 = (SECTOR){ .numtriangles = 0, .triangle = NULL }
	};

	// Ask the user if they would like to start in fullscreen or windowed mode
	int bttnid = ShowYesNoMessageBox(state->win, BTTN_NO, "Start FullScreen?",
		"Would You Like To Run In Fullscreen Mode?");

	// Create our SDL window
	const bool wantfullscreen = (bttnid == BTTN_YES);
	if (!CreateGPUWindow(state, "Lionel Brits & NeHe's 3D World Tutorial", 640, 480, wantfullscreen))
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
		if (state->dev)
		{
			SDL_ReleaseGPUBuffer(state->dev, state->worldmesh);
			SDL_ReleaseGPUTexture(state->dev, state->depthtex);
			for (int i = SDL_arraysize(state->samplers); --i > 0;)
			{
				SDL_ReleaseGPUSampler(state->dev, state->samplers[i]);
			}
			SDL_ReleaseGPUTexture(state->dev, state->texture);
			SDL_ReleaseGPUGraphicsPipeline(state->dev, state->psoblend);
			SDL_ReleaseGPUGraphicsPipeline(state->dev, state->pso);
		}
		KillGPUWindow(state);
		free(state);
	}

	SDL_Quit();
}
