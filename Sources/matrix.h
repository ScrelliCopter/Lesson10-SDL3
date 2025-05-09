#ifndef MATRIX_H
#define MATRIX_H

typedef float mat4f[16];

#define M4_IDENTITY { \
	1, 0, 0, 0, \
	0, 1, 0, 0, \
	0, 0, 1, 0, \
	0, 0, 0, 1 }

void MulMatrices(mat4f mtx, const mat4f lhs, const mat4f rhs);
void MakePerspective(mat4f m, float fovy, float aspect, float near, float far);
void Rotate(mat4f m, float angle, float x, float y, float z);
void Translate(float m[16], float x, float y, float z);

#endif//MATRIX_H
