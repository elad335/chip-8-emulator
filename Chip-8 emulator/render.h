#pragma once

#include "../GLEW/glew.h"
#include "../GLFW/glfw3.h"
#include "utils.h"

void InitWindow();
void KickChip8Framebuffer(void* pixels);
void KickFramebuffer(GLsizei width, GLsizei height, const void *pixels, GLenum type, GLint internalformat, GLenum format);

GLuint LoadShaders(const char* vertex_shader, const char* fragment_shader);
GLuint LoadShadersFromFiles(const wchar_t* vertex_file_path, const wchar_t* fragment_file_path);

// Provides one-timed object id creation interface (upon creation inited set to true)
struct GLhandler
{
	GLuint id = 0;
	bool inited = false;

	bool checkAndInit()
	{
		if (!inited)
		{
			inited = true;
			return true;
		}
		return false;
	}
};

GLuint load2DTexture(GLhandler& handler, GLsizei width, GLsizei height, const void *pixels, GLenum type, GLint internalformat, GLenum format);