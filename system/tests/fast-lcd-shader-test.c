#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCALE = 3, CORE_WIDTH = 160, CORE_HEIGHT = 144, TEX_SIZE = 256,
       WIDTH = CORE_WIDTH * SCALE, HEIGHT = CORE_HEIGHT * SCALE };

static GLuint compile(GLenum kind, const char *header, const char *source)
{
    GLuint shader = glCreateShader(kind);
    const char *parts[] = {header, source};
    glShaderSource(shader, 2, parts, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fputs(log, stderr);
        exit(2);
    }
    return shader;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <installed zfast_lcd.glsl>\n", argv[0]);
        return 2;
    }
    FILE *fp = fopen(argv[1], "rb");
    assert(fp);
    char source[16384];
    size_t size = fread(source, 1, sizeof source - 1, fp);
    assert(feof(fp) && !ferror(fp));
    fclose(fp);
    source[size] = 0;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglInitialize(display, NULL, NULL) && eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, cfg_attr, &config, 1, &count) && count == 1);
    const EGLint pbuf_attr[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
    const EGLint context_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuf_attr);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attr);
    assert(eglMakeCurrent(display, surface, surface, context));
    printf("renderer: %s\n", (const char *)glGetString(GL_RENDERER));

    GLuint program = glCreateProgram();
    glAttachShader(program, compile(GL_VERTEX_SHADER, "#version 100\n#define VERTEX\n", source));
    glAttachShader(program, compile(GL_FRAGMENT_SHADER, "#version 100\n#define FRAGMENT\n", source));
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096];
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fputs(log, stderr);
        return 2;
    }
    glUseProgram(program);

    static unsigned char texels[TEX_SIZE * TEX_SIZE * 4];
    memset(texels, 255, sizeof texels);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glUniform2f(glGetUniformLocation(program, "TextureSize"),
                TEX_SIZE, TEX_SIZE);
    glUniform2f(glGetUniformLocation(program, "InputSize"), CORE_WIDTH, CORE_HEIGHT);
    glUniform2f(glGetUniformLocation(program, "OrigInputSize"), CORE_WIDTH, CORE_HEIGHT);
    const GLfloat matrix[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(program, "MVPMatrix"), 1, GL_FALSE, matrix);
    const GLfloat pos[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    const float u = (float)CORE_WIDTH / TEX_SIZE;
    const float v = (float)CORE_HEIGHT / TEX_SIZE;
    const GLfloat uv[] = {0, 0, u, 0, 0, v, u, v};
    const GLint vert = glGetAttribLocation(program, "VertexCoord");
    const GLint tex = glGetAttribLocation(program, "TexCoord");
    glEnableVertexAttribArray(vert);
    glEnableVertexAttribArray(tex);
    glVertexAttribPointer(vert, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(tex, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_DITHER);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    static unsigned char image[HEIGHT * WIDTH][4];
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(glGetError() == GL_NO_ERROR);

    // For a solid texture every 3x3 cell should have the same brightness at each phase.
    // The shader's default parameters produce 255 at the center and 213 on the border.
    int span = 0;
    int incorrect = 0;
    for (int dy = 0; dy < SCALE; ++dy) {
        for (int dx = 0; dx < SCALE; ++dx) {
            int min = 255, max = 0;
            for (int y = dy; y < HEIGHT; y += SCALE) {
                for (int x = dx; x < WIDTH; x += SCALE) {
                    int v = image[y * WIDTH + x][0];
                    if (v < min) min = v;
                    if (v > max) max = v;
                }
            }
            printf("phase %d,%d: %d-%d\n", dx, dy, min, max);
            const int expected = dx == 1 && dy == 1 ? 255 : 213;
            if (min < expected - 1 || max > expected + 1) incorrect = 1;
            if (max - min > span) span = max - min;
        }
    }
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    if (span > 1) fprintf(stderr, "white viewport has a %d-level phase variation\n", span);
    return incorrect;
}
