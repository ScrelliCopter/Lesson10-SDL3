#include "matrix.h"
#include <SDL3/SDL_stdinc.h>


void MulMatrices(mat4f mtx, const mat4f lhs, const mat4f rhs)
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

void MakePerspective(mat4f m, float fovy, float aspect, float near, float far)
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

static void MakeRotation(float m[9], float theta, float x, float y, float z)
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

void Rotate(mat4f m, float angle, float x, float y, float z)
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

void Translate(float m[16], float x, float y, float z)
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
