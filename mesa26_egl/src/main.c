/*
 * \brief  Rendering test for Mesa 26 without display
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Creates EGL window surfaces on a plain memory buffer (the native window of
 * Genode's EGL platform is a pixel buffer), renders with OpenGL ES 2 (GLSL
 * shaders) and with desktop OpenGL (fixed function, display list), and
 * checks the pixels both with glReadPixels and in the window buffer after
 * eglSwapBuffers. Prints "mesa26_egl: PASS" or "mesa26_egl: FAIL".
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GL/gl.h>

enum { WIDTH = 256, HEIGHT = 192 };

static int failures;


static void check(int cond, char const *what)
{
	printf("mesa26_egl:   %s: %s\n", what, cond ? "ok" : "FAILED");
	if (!cond)
		failures++;
}


/* pixel of the window buffer: 32 bit, blue in the lowest byte */
static unsigned window_pixel(struct Genode_egl_window const *win, int x, int y)
{
	unsigned v;
	memcpy(&v, win->addr + 4*(y*win->width + x), 4);
	return v & 0xffffff;
}


/* pixel of the current read buffer as 0xrrggbb, y counted from the top */
static unsigned gl_pixel(int x, int y)
{
	unsigned char p[4] = { 0, 0, 0, 0 };
	glReadPixels(x, HEIGHT - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	return (unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2];
}


static int near(unsigned a, unsigned b)
{
	for (int shift = 0; shift < 24; shift += 8) {
		int const d = (int)((a >> shift) & 0xff) - (int)((b >> shift) & 0xff);
		if (d < -2 || d > 2)
			return 0;
	}
	return 1;
}


static EGLConfig choose_config(EGLDisplay dpy, EGLint renderable)
{
	EGLint const attribs[] = {
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, renderable,
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
		EGL_DEPTH_SIZE,      16,
		EGL_NONE
	};

	EGLConfig config = 0;
	EGLint    count  = 0;

	if (!eglChooseConfig(dpy, attribs, &config, 1, &count) || count < 1)
		return 0;

	return config;
}


static GLuint compile(GLenum type, char const *src)
{
	GLuint const shader = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[256] = "";
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("mesa26_egl:   shader log: %s\n", log);
	}
	check(ok, type == GL_VERTEX_SHADER ? "GLES2: vertex shader compiles"
	                                   : "GLES2: fragment shader compiles");
	return shader;
}


static void test_gles2(EGLDisplay dpy, struct Genode_egl_window *win)
{
	static char const * const vs =
		"attribute vec2 pos;\n"
		"void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";

	static char const * const fs =
		"precision mediump float;\n"
		"uniform vec4 color;\n"
		"void main() { gl_FragColor = color; }\n";

	static GLfloat const triangle[] = { -0.5f, -0.5f,  0.5f, -0.5f,  0.0f, 0.6f };
	static EGLint  const ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	eglBindAPI(EGL_OPENGL_ES_API);

	EGLConfig  const config  = choose_config(dpy, EGL_OPENGL_ES2_BIT);
	check(config != 0, "GLES2: configuration found");
	if (!config)
		return;

	EGLContext const ctx     = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
	EGLSurface const surface = eglCreateWindowSurface(dpy, config, win, NULL);

	check(ctx != EGL_NO_CONTEXT,     "GLES2: context created");
	check(surface != EGL_NO_SURFACE, "GLES2: window surface created");
	if (ctx == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE)
		return;

	check(eglMakeCurrent(dpy, surface, surface, ctx), "GLES2: make current");

	printf("mesa26_egl: GL_VERSION  = %s\n", (char const *)glGetString(GL_VERSION));
	printf("mesa26_egl: GL_RENDERER = %s\n", (char const *)glGetString(GL_RENDERER));
	printf("mesa26_egl: GL_SHADING_LANGUAGE_VERSION = %s\n",
	       (char const *)glGetString(GL_SHADING_LANGUAGE_VERSION));

	GLuint const program = glCreateProgram();
	GLint  linked = 0;

	glAttachShader(program, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(program, compile(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(program, 0, "pos");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	check(linked, "GLES2: program links");

	glUseProgram(program);
	glViewport(0, 0, WIDTH, HEIGHT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
	glEnableVertexAttribArray(0);

	/* two frames with different colors */
	static struct { GLfloat bg[3], fg[4]; unsigned bg_rgb, fg_rgb; } const frames[] = {
		{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, 0x0000ff, 0xff0000 },
		{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f }, 0x00ff00, 0xffff00 },
	};

	for (unsigned i = 0; i < sizeof(frames)/sizeof(frames[0]); i++) {

		memset(win->addr, 0x55, 4*WIDTH*HEIGHT);

		glClearColor(frames[i].bg[0], frames[i].bg[1], frames[i].bg[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUniform4fv(glGetUniformLocation(program, "color"), 1, frames[i].fg);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		check(glGetError() == GL_NO_ERROR, "GLES2: no GL error");

		unsigned const corner = gl_pixel(2, 2), center = gl_pixel(WIDTH/2, HEIGHT/2);

		check(eglSwapBuffers(dpy, surface), "GLES2: swap buffers");

		char msg[96];
		snprintf(msg, sizeof(msg), "GLES2: frame %u glReadPixels 0x%06x 0x%06x", i, corner, center);
		check(near(corner, frames[i].bg_rgb) && near(center, frames[i].fg_rgb), msg);

		snprintf(msg, sizeof(msg), "GLES2: frame %u window buffer 0x%06x 0x%06x 0x%06x", i,
		         window_pixel(win, 2, 2), window_pixel(win, WIDTH/2, HEIGHT/2),
		         window_pixel(win, WIDTH - 3, HEIGHT - 3));
		check(near(window_pixel(win, 2, 2), frames[i].bg_rgb) &&
		      near(window_pixel(win, WIDTH - 3, HEIGHT - 3), frames[i].bg_rgb) &&
		      near(window_pixel(win, WIDTH/2, HEIGHT/2), frames[i].fg_rgb), msg);
	}

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surface);
	eglDestroyContext(dpy, ctx);
}


static void test_gl(EGLDisplay dpy, struct Genode_egl_window *win)
{
	if (!eglBindAPI(EGL_OPENGL_API)) {
		check(0, "GL: desktop OpenGL available");
		return;
	}

	EGLConfig const config = choose_config(dpy, EGL_OPENGL_BIT);
	check(config != 0, "GL: configuration found");
	if (!config)
		return;

	EGLContext const ctx     = eglCreateContext(dpy, config, EGL_NO_CONTEXT, NULL);
	EGLSurface const surface = eglCreateWindowSurface(dpy, config, win, NULL);

	check(ctx != EGL_NO_CONTEXT,     "GL: context created");
	check(surface != EGL_NO_SURFACE, "GL: window surface created");
	if (ctx == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE)
		return;

	check(eglMakeCurrent(dpy, surface, surface, ctx), "GL: make current");

	printf("mesa26_egl: GL_VERSION  = %s\n", (char const *)glGetString(GL_VERSION));

	memset(win->addr, 0x55, 4*WIDTH*HEIGHT);

	glViewport(0, 0, WIDTH, HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_DEPTH_TEST);

	/* a quad in a display list, as the gears demo draws its geometry */
	GLuint const list = glGenLists(1);
	glNewList(list, GL_COMPILE);
	glBegin(GL_QUADS);
	glVertex3f(-0.5f, -0.5f, 0.0f);
	glVertex3f( 0.5f, -0.5f, 0.0f);
	glVertex3f( 0.5f,  0.5f, 0.0f);
	glVertex3f(-0.5f,  0.5f, 0.0f);
	glEnd();
	glEndList();

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glColor3f(0.0f, 1.0f, 1.0f);
	glCallList(list);

	/* behind the first quad, must stay hidden */
	glPushMatrix();
	glTranslatef(0.0f, 0.0f, -0.5f);
	glColor3f(1.0f, 0.0f, 0.0f);
	glCallList(list);
	glPopMatrix();

	check(glGetError() == GL_NO_ERROR, "GL: no GL error");

	unsigned const corner = gl_pixel(2, 2), center = gl_pixel(WIDTH/2, HEIGHT/2);

	check(eglSwapBuffers(dpy, surface), "GL: swap buffers");

	char msg[96];
	snprintf(msg, sizeof(msg), "GL: glReadPixels 0x%06x 0x%06x", corner, center);
	check(near(corner, 0x000000) && near(center, 0x00ffff), msg);

	snprintf(msg, sizeof(msg), "GL: window buffer 0x%06x 0x%06x",
	         window_pixel(win, 2, 2), window_pixel(win, WIDTH/2, HEIGHT/2));
	check(near(window_pixel(win, 2, 2), 0x000000) &&
	      near(window_pixel(win, WIDTH/2, HEIGHT/2), 0x00ffff), msg);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surface);
	eglDestroyContext(dpy, ctx);
}


int main(void)
{
	static struct Genode_egl_window win;

	win.width  = WIDTH;
	win.height = HEIGHT;
	win.type   = WINDOW;
	win.addr   = malloc(4*WIDTH*HEIGHT);

	EGLDisplay const dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;

	if (!win.addr || dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
		printf("mesa26_egl: eglInitialize failed (0x%x)\n", (unsigned)eglGetError());
		printf("mesa26_egl: FAIL\n");
		return 1;
	}

	printf("mesa26_egl: EGL %d.%d\n", (int)major, (int)minor);
	printf("mesa26_egl: EGL_VERSION = %s\n", eglQueryString(dpy, EGL_VERSION));
	printf("mesa26_egl: EGL_VENDOR  = %s\n", eglQueryString(dpy, EGL_VENDOR));
	printf("mesa26_egl: EGL_CLIENT_APIS = %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));

	test_gles2(dpy, &win);
	test_gl(dpy, &win);

	eglTerminate(dpy);

	printf("mesa26_egl: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
