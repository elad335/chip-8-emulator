#include "render.h"
#include "emucore.h"

// static vertex array ID
GLuint sVertexArrayID;
GLuint sVertexbuffer, sTexCoordsbuffer;

GLFWwindow* window{};

// Triangle strip forming a rectangle
const GLfloat s_vertex_buffer_data[] =
{
   -1.f, -1.f, 0.f,
   1.f, -1.f, 0.f,
   -1.f, 1.f, 0.f,
   1.f, 1.f, 0.f,
};

const GLfloat s_vertex_buffer_tex_data[] =
{
   0.f, 1.f,
   1.f, 1.f,
   0.f, 0.f,
   1.f, 0.f,
};

void InitWindow()
{
	if (!glfwInit())
	{
		printf("Failed to initialize GLFW\n");
		hwBpx();
		return;
	}

	//glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We don't want the old OpenGL 

	// Open a window and create its OpenGL context
	window = glfwCreateWindow(64 * 16, 32 * 16, "Chip-8 emulator", NULL, NULL);
	if (!window)
	{
		printf("Failed to open GLFW window!\n");
		glfwTerminate();
		hwBpx();
		return;
	}

	// Set callback on closing window
	glfwSetWindowCloseCallback(window, [](GLFWwindow* wnd)
	{
		glfwMakeContextCurrent(NULL); // Unuse currect context
		glfwDestroyWindow(wnd); // Free context
		glfwTerminate(); // GLFW cleanup
		g_state.terminate = true; // Signal timers thread
		g_state.hwtimers->join(); // join
		exit(0); // Actually exit
	});

	glfwMakeContextCurrent(window); // Initialize GLEW

	glewExperimental = true; // Needed in core profile
	if (glewInit() != GLEW_OK)
	{
		printf("Failed to initialize GLEW\n");
		hwBpx();
		return;
	}

	// Hide console
	ShowWindow(GetConsoleWindow(), SW_HIDE);

	glGenVertexArrays(1, &sVertexArrayID);
	glBindVertexArray(sVertexArrayID);

	// Generate 1 buffer, put the resulting identifier in vertexbuffer
	glGenBuffers(1, &sVertexbuffer);
	// Bind vertex buffer ID
	glBindBuffer(GL_ARRAY_BUFFER, sVertexbuffer);
	// Actually bind its data
	glBufferData(GL_ARRAY_BUFFER, sizeof(s_vertex_buffer_data), s_vertex_buffer_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sTexCoordsbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, sTexCoordsbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(s_vertex_buffer_tex_data), s_vertex_buffer_tex_data, GL_STATIC_DRAW);
}

// No real need to load from files
const char* s_default_vertex_shader =
"#version 330 core\n"

// Input vertex data
"layout(location = 0) in vec3 vertexPosition_modelspace;\n"
// Input texture coordinates
"layout(location = 1) in vec2 vertexUV;\n"

"out vec2 UV;"

"void main() {\n"
"gl_Position = vec4(vertexPosition_modelspace, 1.0);\n"
"UV = vertexUV;\n"
"}"
;

const char* s_default_fragment_shader =
"#version 330 core\n"

// Interpolated values from the vertex shaders
"in vec2 UV;\n"

// Ouput data
"out vec3 color;\n"

// Values that stay constant for the whole mesh.
"uniform sampler2D TextureSampler;\n"

"void main() {\n"

// Output color = tex[UV].red -> rgb
"	color = vec3(texture(TextureSampler, UV).r);\n"
"}"
;

GLuint LoadShaders(const char* vertex_shader, const char* fragment_shader)
{
	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	glShaderSource(VertexShaderID, 1, &vertex_shader, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0)
	{
		std::vector<char> VertexShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		printf("%s\n", &VertexShaderErrorMessage[0]);
		hwBpx();
	}

	// Compile Fragment Shader
	glShaderSource(FragmentShaderID, 1, &fragment_shader, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0)
	{
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		printf("%s\n", &FragmentShaderErrorMessage[0]);
		hwBpx();
	}

	// Link the program
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0)
	{
		std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		printf("%s\n", &ProgramErrorMessage[0]);
		hwBpx();
	}

	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return ProgramID;
}

GLuint LoadShadersFromFiles(const wchar_t* vertex_file_path, const wchar_t* fragment_file_path)
{
	// Read the Vertex Shader code from the file
	std::string VertexShaderCode;
	std::ifstream VertexShaderStream(vertex_file_path, std::ios::in);
	if (VertexShaderStream.is_open())
	{
		std::stringstream sstr;
		sstr << VertexShaderStream.rdbuf();
		VertexShaderCode = sstr.str();
		VertexShaderStream.close();
	}
	else
	{
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		wprintf(L"Failed to read vertex shader: %s\n", vertex_file_path);
		hwBpx();
		return 0;
	}

	// Read the Fragment Shader code from the file
	std::string FragmentShaderCode;
	std::ifstream FragmentShaderStream(fragment_file_path, std::ios::in);
	if (FragmentShaderStream.is_open())
	{
		std::stringstream sstr;
		sstr << FragmentShaderStream.rdbuf();
		FragmentShaderCode = sstr.str();
		FragmentShaderStream.close();
	}
	else
	{
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		wprintf(L"Failed to read fragement shader: %s.\n", fragment_file_path);
		hwBpx();
		return 0;
	}

	return LoadShaders(VertexShaderCode.c_str(), FragmentShaderCode.c_str());
}

GLuint load2DTexture(GLhandler& handler, GLsizei width, GLsizei height, const void *pixels, GLenum type, GLint internalformat, GLenum format)
{
	if (handler.checkAndInit())
	{
		glGenTextures(1, &handler.id);
	}

	// Bind texture id
	glBindTexture(GL_TEXTURE_2D, handler.id);

	// Bind raw texture data
	glTexImage2D(GL_TEXTURE_2D, 0, internalformat, width, height, 0, format, type, pixels);

	// NEAREST provides the most suitable filtering here
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	// Set wrapping mode
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glGenerateMipmap(GL_TEXTURE_2D);

	return handler.id;
}

// Intermediate buffer for swizzled buffer translation
static u8 interBuffer[emu_state::y_size_ex * emu_state::x_size_ex]{};

void KickChip8Framebuffer(void* pixels)
{
	// Translate swizzled buffer into raw rgba buffer

	// Y0 is the original's buffer, y1 is the destination's
	for (u32 y0 = 0, y1 = 0; y1 < emu_state::y_size * emu_state::x_size; y1 += emu_state::x_size, y0 += emu_state::y_stride)
	{
		// TODO: This can be further optimzed with asmjit
		std::memcpy(interBuffer + y1, (u8*)pixels + y0, emu_state::x_size);
	}

	// Here we bind red channel only, but modify it in the fragment shader into black and white!
	KickFramebuffer(emu_state::x_size, emu_state::y_size, interBuffer, GL_UNSIGNED_BYTE, GL_R8, GL_RED);
}

void KickSChip8Framebuffer(void* pixels)
{
	for (u32 y0 = 0, y1 = 0; y1 < emu_state::y_size_ex * emu_state::x_size_ex; y1 += emu_state::x_size_ex, y0 += emu_state::y_stride)
	{
		// TODO: This can be further optimzed with asmjit
		std::memcpy(interBuffer + y1, (u8*)pixels + y0, emu_state::x_size_ex);
	}

	// Here we bind red channel only, but modify it in the fragment shader into black and white!
	KickFramebuffer(emu_state::x_size_ex, emu_state::y_size_ex, interBuffer, GL_UNSIGNED_BYTE, GL_R8, GL_RED);
}

// TODO: Investigate vulkan implemntation
void KickFramebuffer(GLsizei width, GLsizei height, const void *pixels, GLenum type, GLint internalformat, GLenum format)
{
	static GLhandler Program;

	// 1st attribute buffer : vertices
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, sVertexbuffer);
	glVertexAttribPointer(
		0,                  // attribute 0 as in the sahder
		3,                  // size, 3 coords per vertex
		GL_FLOAT,           // type
		GL_FALSE,           // normalized?
		0,                  // stride
		(void*)0            // array buffer offset
	);

	// 2st attribute buffer : texture coordinates
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, sTexCoordsbuffer);
	glVertexAttribPointer(
		1,                  // attribute 1 as in the shader
		2,                  // size, 2 coords per vertex
		GL_FLOAT,           // type
		GL_FALSE,           // normalized?
		0,                  // stride
		(void*)0            // array buffer offset
	);

	if (Program.checkAndInit())
	{
		// Compile the program once
		Program.id = LoadShaders(s_default_vertex_shader, s_default_fragment_shader);
	}

	glUseProgram(Program.id);

	static GLhandler texId;
	load2DTexture(texId, width, height, pixels, type, internalformat, format);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glfwSwapBuffers(window);

	glfwPollEvents(); // TODO: It must be executed unrelated to draws, possibly in a saperate thread
}