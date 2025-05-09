/*
 *  This code was created by Lionel Brits & Jeff Molofee 2000.
 *  A HUGE thanks to Fredric Echols for cleaning up
 *  and optimizing the base code, making it more flexible!
 *  If you've found this code useful, please let me know.
 *  Visit my site at https://nehe.gamedev.net
 */

#include <stddef.h>
#include <stdbool.h>
#include <float.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include "matrix.h"

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
	SDL_assert(state && state->resdir && name);
	size_t resdirLen = SDL_strlen(state->resdir), nameLen = SDL_strlen(name);
	char *path = SDL_malloc(resdirLen + nameLen + 1);
	if (!path)
	{
		return NULL;
	}
	SDL_memcpy(path, state->resdir, resdirLen);
	SDL_memcpy(&path[resdirLen], name, nameLen);
	path[resdirLen + nameLen] = '\0';
	return path;
}

static SDL_IOStream * fopenResource(const APPSTATE *restrict state, const char *restrict name, const char *restrict mode)
{
	SDL_assert(state && name && mode);
	char *path = resourcePath(state, name);
	if (!path)
	{
		return NULL;
	}
	SDL_IOStream *f = SDL_IOFromFile(path, mode);
	SDL_free(path);
	return f;
}

static char * fgetsIO(char *restrict s, int n, SDL_IOStream *restrict f)
{
	char *p = s;
	for (--n; n > 0; --n)
	{
		Sint8 c;
		if (!SDL_ReadS8(f, &c))
			break;
		if (((*p++) = c) == '\n')
			break;
	}
	(*p) = '\0';
	return p != s ? s : NULL;
}

static void readstr(SDL_IOStream *restrict f, char *restrict string)
{
	do
	{
		fgetsIO(string, 255, f);
	} while (string[0] == '/' || string[0] == '\n');
}

static void SetupWorld(APPSTATE *state)
{
	float x, y, z, u, v;
	int numtriangles;
	char oneline[255];
	SDL_IOStream* filein = fopenResource(state, "Data/World.txt", "r");  // File to load world data from

	readstr(filein, oneline);
	SDL_sscanf(oneline, "NUMPOLLIES %d\n", &numtriangles);

	state->sector1.triangle = SDL_malloc(sizeof(TRIANGLE) * numtriangles);
	state->sector1.numtriangles = numtriangles;
	for (int loop = 0; loop < numtriangles; loop++)
	{
		for (int vert = 0; vert < 3; vert++)
		{
			readstr(filein, oneline);
			SDL_sscanf(oneline, "%f %f %f %f %f", &x, &y, &z, &u, &v);
			state->sector1.triangle[loop].vertex[vert].x = x;
			state->sector1.triangle[loop].vertex[vert].y = y;
			state->sector1.triangle[loop].vertex[vert].z = z;
			state->sector1.triangle[loop].vertex[vert].u = u;
			state->sector1.triangle[loop].vertex[vert].v = v;
		}
	}
	SDL_CloseIO(filein);
}

static bool CreateWorldMesh(APPSTATE *state)
{
	const int numtriangles = state->sector1.numtriangles;
	const size_t stride = sizeof(VERTEX) * 3;
	const Uint32 bufsize = stride * (Uint32)numtriangles;

	// Create vertex data buffer
	SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(state->dev, &(SDL_GPUBufferCreateInfo)
	{
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = bufsize,
		.props = 0
	});
	if (!buf)
	{
		return false;
	}

	// Create transfer buffer
	SDL_GPUTransferBuffer *xferbuf = SDL_CreateGPUTransferBuffer(state->dev, &(SDL_GPUTransferBufferCreateInfo)
	{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = bufsize,
		.props = 0
	});
	if (!xferbuf)
	{
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return false;
	}

	// Map transfer buffer and copy the vertex data
	Uint8 *map = SDL_MapGPUTransferBuffer(state->dev, xferbuf, false);
	if (!map)
	{
		SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return false;
	}
	for (int loop_m = 0; loop_m < numtriangles; loop_m++)
	{
		SDL_memcpy(&map[stride * loop_m], state->sector1.triangle[loop_m].vertex, stride);
	}
	SDL_UnmapGPUTransferBuffer(state->dev, xferbuf);

	// Upload the vertex data into the GPU buffer
	SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(state->dev);
	if (!cmdbuf)
	{
		SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);
		SDL_ReleaseGPUBuffer(state->dev, buf);
		return false;
	}
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmdbuf);
	const SDL_GPUTransferBufferLocation source = { .transfer_buffer = xferbuf, .offset = 0 };
	const SDL_GPUBufferRegion dest = { .buffer = buf, .offset = 0, .size = bufsize };
	SDL_UploadToGPUBuffer(pass, &source, &dest, false);
	SDL_EndGPUCopyPass(pass);
	SDL_SubmitGPUCommandBuffer(cmdbuf);
	SDL_ReleaseGPUTransferBuffer(state->dev, xferbuf);

	state->worldmesh = buf;
	return true;
}

static bool FlipSurface(SDL_Surface *surface)
{
	if (!surface || !SDL_LockSurface(surface))
	{
		return false;
	}

	const int pitch = surface->pitch;
	const int numrows = surface->h;
	unsigned char *pixels = surface->pixels;
	unsigned char *tmprow = SDL_malloc(sizeof(unsigned char) * pitch);

	unsigned char *row1 = pixels;
	unsigned char *row2 = pixels + (numrows - 1) * pitch;
	for (int i = 0; i < numrows / 2; ++i)
	{
		// Swap rows
		SDL_memcpy(tmprow, row1, pitch);
		SDL_memcpy(row1, row2, pitch);
		SDL_memcpy(row2, tmprow, pitch);

		row1 += pitch;
		row2 -= pitch;
	}

	SDL_free(tmprow);
	SDL_UnlockSurface(surface);
	return true;
}

typedef struct tagBLOB
{
	uint8_t *data;
	size_t size;
} BLOB;

static BLOB ReadBlob(APPSTATE *state, const char *path)
{
	SDL_IOStream *filein = fopenResource(state, path, "rb");
	if (!filein)
	{
		return (BLOB){ NULL, 0U };
	}

	// Allocate a buffer of the size of the file
	Sint64 size; Uint8 *data;
	SDL_SeekIO(filein, 0, SDL_IO_SEEK_END);
	if ((size = SDL_TellIO(filein)) <= 0 ||
		!(data = SDL_malloc((size_t)size)))
	{
		SDL_CloseIO(filein);
		return (BLOB){ NULL, 0U };
	}
	SDL_SeekIO(filein, 0, SDL_IO_SEEK_SET);

	// Read the file contents into the buffer
	const size_t read = SDL_ReadIO(filein, data, (size_t)size);
	SDL_CloseIO(filein);
	if (read != (size_t)size)
	{
		SDL_free(data);
		return (BLOB){ NULL, 0U };
	}

	return (BLOB){ data, read };
}

static SDL_GPUShader * LoadShaderBlob(APPSTATE *state, const BLOB lib,
	SDL_GPUShaderFormat format, const char *entrypoint, bool isfragment)
{
	if (!lib.data)
	{
		return NULL;
	}

	// Create shader object
	SDL_GPUShader *shader = SDL_CreateGPUShader(state->dev, &(SDL_GPUShaderCreateInfo)
	{
		.num_samplers = isfragment ? 1 : 0,
		.num_storage_textures = 0,
		.num_storage_buffers = 0,
		.num_uniform_buffers = isfragment ? 0 : 1,
		.format = format,
		.entrypoint = entrypoint,
		.code = lib.data,
		.code_size = lib.size,
		.stage = isfragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX
	});
	return shader;
}

static SDL_GPUShader * LoadShader(APPSTATE *state, const char *path,
	SDL_GPUShaderFormat format, const char *entrypoint, bool isfragment)
{
	BLOB lib = ReadBlob(state, path);
	SDL_GPUShader *shader = LoadShaderBlob(state, lib, format, entrypoint, isfragment);
	SDL_free(lib.data);
	return shader;
}

static bool LoadShaders(APPSTATE *state, SDL_GPUShader **vertexshader, SDL_GPUShader **fragmentshader)
{
	SDL_GPUShader *vtxshader = NULL, *frgshader = NULL;

	const SDL_GPUShaderFormat availableformats = SDL_GetGPUShaderFormats(state->dev);

	if (availableformats & SDL_GPU_SHADERFORMAT_METALLIB)  // Apple Metal
	{
		BLOB mtllib = ReadBlob(state, "Data/Shaders/Shader.metallib");
		vtxshader = LoadShaderBlob(state, mtllib, SDL_GPU_SHADERFORMAT_METALLIB, "VertexMain", false);
		frgshader = LoadShaderBlob(state, mtllib, SDL_GPU_SHADERFORMAT_METALLIB, "FragmentMain", true);
		SDL_free(mtllib.data);
	}
	else if (availableformats & SDL_GPU_SHADERFORMAT_SPIRV)  // Vulkan
	{
		vtxshader = LoadShader(state, "Data/Shaders/Shader.vertex.spv", SDL_GPU_SHADERFORMAT_SPIRV, "main", false);
		frgshader = LoadShader(state, "Data/Shaders/Shader.fragment.spv", SDL_GPU_SHADERFORMAT_SPIRV, "main", true);
	}
	else if (availableformats & SDL_GPU_SHADERFORMAT_DXIL)  // Direct3D 12 Shader Model 6.0
	{
		vtxshader = LoadShader(state, "Data/Shaders/Shader.vertex.dxb", SDL_GPU_SHADERFORMAT_DXIL, "VertexMain", false);
		frgshader = LoadShader(state, "Data/Shaders/Shader.fragment.dxb", SDL_GPU_SHADERFORMAT_DXIL, "FragmentMain", true);
	}
	else if (availableformats & SDL_GPU_SHADERFORMAT_DXBC)  // Direct3D 12 Shader Model 5.1
	{
		vtxshader = LoadShader(state, "Data/Shaders/Shader.vertex.fxb", SDL_GPU_SHADERFORMAT_DXBC, "VertexMain", false);
		frgshader = LoadShader(state, "Data/Shaders/Shader.fragment.fxb", SDL_GPU_SHADERFORMAT_DXBC, "FragmentMain", true);
	}

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

	SDL_PropertiesID texprops = SDL_CreateProperties();
	if (texprops == 0)
	{
		return false;
	}
	// Workaround for https://github.com/libsdl-org/SDL/issues/10758
	SDL_SetFloatProperty(texprops, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, 1.f);

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
		.props = texprops
	});
	SDL_DestroyProperties(texprops);
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

static SDL_GPUTexture * CreateTextureFromSurface(APPSTATE *state, const SDL_Surface *image, bool genmips)
{
	const int width = image->w, height = image->h, depth = 1;
	const Uint32 datasize = 4 * width * height;

	// Convert the input surface into RGBA
	void *converted = SDL_malloc(datasize);
	if (!SDL_ConvertPixels(width, height,
		image->format, image->pixels, image->pitch,
		SDL_PIXELFORMAT_ABGR8888, converted, 4 * width))
	{
		SDL_free(converted);
		return NULL;
	}

	SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	int levels = 1;
	if (genmips)
	{
		usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
		// Calculate the number of mipmap levels the texture should store
		const int max = width > height ? width : height;
		// AKA: for (int i = max; i > 1; ++levels, i /= 2);
		// AKA: floor(log₂(max(𝑤,ℎ)) + 1
		levels = SDL_MostSignificantBitIndex32(max) + 1;
	}

	SDL_GPUTexture *texture = SDL_CreateGPUTexture(state->dev, &(SDL_GPUTextureCreateInfo)
	{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.width = width,
		.height = height,
		.layer_count_or_depth = 1,
		.num_levels = (Uint32)levels,
		.usage = usage,
	});

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

	if (genmips)
	{
		SDL_GenerateMipmapsForGPUTexture(cmdbuf, texture);
	}

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
	SDL_free(path);
	if (!TextureImage || !FlipSurface(TextureImage))
	{
		SDL_DestroySurface(TextureImage);
		return false;
	}

	// Create texture
	state->texture = CreateTextureFromSurface(state, TextureImage, true);
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
			.max_lod = 0.f
		},
		[1] =  // Linear filtered
		{
			.min_filter = SDL_GPU_FILTER_LINEAR,
			.mag_filter = SDL_GPU_FILTER_LINEAR,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
			.max_lod = 0.f
		},
		[2] =  // MipMapped
		{
			.min_filter = SDL_GPU_FILTER_LINEAR,
			.mag_filter = SDL_GPU_FILTER_LINEAR,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
			.max_lod = FLT_MAX
		}
	};

	for (unsigned i = 0; i < SDL_arraysize(state->samplers); ++i)
	{
		SDL_GPUSamplerCreateInfo info = params[i];
		info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		SDL_GPUSampler *sampler = SDL_CreateGPUSampler(state->dev, &info);
		if (!sampler)
		{
			return false;
		}
		state->samplers[i] = sampler;
	}

	return true;
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

	const float aspect = (float)width / (float)height;        // Calculate aspect ratio
	MakePerspective(state->projmtx, 45.0f, aspect, 0.1f, 100.0f);  // Setup perspective matrix
}

static SDL_GPUGraphicsPipeline *MakePipeline(APPSTATE *state,
	SDL_GPUShader *vtxshader, SDL_GPUShader *frgshader, bool blend)
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

static bool DrawScene(APPSTATE *state)
{
	SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(state->dev);

	SDL_GPUTexture* backbuftex = NULL;
	Uint32 backbufw, backbufh;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, state->win, &backbuftex, &backbufw, &backbufh) || !backbuftex)
	{
		SDL_CancelGPUCommandBuffer(cmdbuf);
		return false;
	}

	const float xtrans = -state->camera.xpos;
	const float ztrans = -state->camera.zpos;
	const float ytrans = -state->camera.walkbias - 0.25f;
	const float sceneroty = 360.0f - state->camera.yrot;

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
	SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding)
	{
		.texture = state->texture,
		.sampler = state->samplers[state->filter]
	}, 1);
	const Uint32 numvertices = 3u * state->sector1.numtriangles;
	SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding)
	{
		.buffer = state->worldmesh, .offset = 0
	}, 1);
	SDL_DrawGPUPrimitives(pass, numvertices, 1, 0, 0);

	SDL_EndGPURenderPass(pass);
	SDL_SubmitGPUCommandBuffer(cmdbuf);
	return true;
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
			const int bttnid = ShowYesNoMessageBox(state->win, BTTN_YES, "NeHe SDL_GPU",
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

	const SDL_GPUShaderFormat supportedformats =
		SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_SPIRV |
		SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_DXBC;
	if (!(state->dev = SDL_CreateGPUDevice(supportedformats, true, NULL)))  // Create rendering device
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
	APPSTATE *state = appstate;
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
	APPSTATE *state = appstate;
	if (!DrawScene(state))             // Draw the scene
	{
		return SDL_APP_CONTINUE;
	}

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
		state->camera.xpos -= SDL_sinf(state->camera.heading * piover180) * 0.05f;
		state->camera.zpos -= SDL_cosf(state->camera.heading * piover180) * 0.05f;
		if (state->camera.walkbiasangle >= 359.0f)
		{
			state->camera.walkbiasangle = 0.0f;
		}
		else
		{
			state->camera.walkbiasangle += 10;
		}
		state->camera.walkbias = SDL_sinf(state->camera.walkbiasangle * piover180) / 20.0f;
	}

	if (keys[SDL_SCANCODE_DOWN])
	{
		state->camera.xpos += SDL_sinf(state->camera.heading * piover180) * 0.05f;
		state->camera.zpos += SDL_cosf(state->camera.heading * piover180) * 0.05f;
		if (state->camera.walkbiasangle <= 1.0f)
		{
			state->camera.walkbiasangle = 359.0f;
		}
		else
		{
			state->camera.walkbiasangle -= 10;
		}
		state->camera.walkbias = SDL_sinf(state->camera.walkbiasangle * piover180) / 20.0f;
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
	(void)argc; (void)argv;  // Unused parameters

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		return SDL_APP_FAILURE;
	}

	APPSTATE *state = *appstate = SDL_malloc(sizeof(APPSTATE));
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
	const int bttnid = ShowYesNoMessageBox(state->win, BTTN_NO, "Start FullScreen?",
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
	(void)result;  // Unused parameters

	if (appstate)
	{
		APPSTATE *state = appstate;
		SDL_free(state->sector1.triangle);
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
		SDL_free(state);
	}

	SDL_Quit();
}
