/*
 * Copyright © 2016 Rockchip Co., Ltd.
 * Jacob Chen, <jacob2.chen@rock-chips.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "rockchip_x11.h"
#include "rockchip_x11_gles.h"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <libdrm/drm_fourcc.h>

#define CHECKEGL { int e=glGetError(); if (e) fprintf(stderr, "\e[1;31m[EGL ERROR %s:%d]\e[0m %d(0x%x)\n", __FILE__, __LINE__, e, e);}

typedef enum
{
    SHADER_OES = 0,
} shader_type_t;

typedef struct
{
    GLuint program;
    GLuint vertex_shader;
    GLuint fragment_shader;

    /* standard locations, used in most shaders */
    GLint position_loc;
    GLint texcoord_loc;

    GLint texture[3];
} shader_ctx_t;

struct obj_surface2{
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    Display *x11_display;
    GLuint oes_tex;

    shader_ctx_t oes;
} obj_surface1;

const char *vertex_shader = "attribute vec4 vPosition;"
    "attribute vec2 aTexcoord;"
    "varying vec2 vTexcoord;"
    "void main(void) {"
    "   gl_Position = vPosition;"
    "   vTexcoord = vec2(aTexcoord.x, 1.0 - aTexcoord.y);"
    "}";


static const char* fragment_shaders[] = {
    /* OES_EGL */
    "#extension GL_OES_EGL_image_external : enable\n"
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform samplerExternalOES tex_external;"
    "void main() {"
    "  vec4 color = texture2D(tex_external, vTexcoord);"
    "  gl_FragColor = color;"
    "}"
};

/* load and compile a shader src into a shader program */
static GLuint
gl_load_shader (const char *shader_src,
                       GLenum type)
{
    GLuint shader = 0;
    GLint compiled;
    size_t src_len;

    /* create a shader object */
    shader = glCreateShader (type);
    if (shader == 0) {
        rk_info_msg ("Could not create shader object\n");
        return 0;
    }

    /* load source into shader object */
    src_len = strlen (shader_src);
    glShaderSource (shader, 1, (const GLchar**) &shader_src,
                    (const GLint*) &src_len);

    /* compile the shader */
    glCompileShader (shader);

    /* check compiler status */
    glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;

        glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &info_len);
        if(info_len > 1) {
            char *info_log = malloc (sizeof(char) * info_len);
            glGetShaderInfoLog (shader, info_len, NULL, info_log);

            rk_info_msg ("Failed to compile shader: %s\n", info_log);
            free (info_log);
        }

        glDeleteShader (shader);
        shader = 0;
    }

    return shader;
}

/*
 * Load vertex and fragment Shaders.
 * Vertex shader is a predefined default, fragment shader can be configured
 * through process_type */
static int
gl_load_shaders (shader_ctx_t *shader,
                 shader_type_t process_type)
{
    shader->vertex_shader = gl_load_shader (vertex_shader,
                                          GL_VERTEX_SHADER);
    if (!shader->vertex_shader)
        return -1;

    shader->fragment_shader = gl_load_shader (fragment_shaders[process_type],
                                            GL_FRAGMENT_SHADER);
    if (!shader->fragment_shader)
        return -1;

    return 0;
}

int
gl_init_shader (shader_ctx_t *shader,
                shader_type_t process_type)
{
    int linked;
    GLint err;
    int ret;

    shader->program = glCreateProgram();
    if(!shader->program) {
        rk_info_msg("Could not create GL program\n");
        return -1;
    }

    /* load the shaders */
    ret = gl_load_shaders(shader, process_type);
    if(ret < 0) {
        rk_info_msg("Could not create GL shaders: %d\n", ret);
        return ret;
    }

    glAttachShader(shader->program, shader->vertex_shader);
    err = glGetError ();
    if (err != GL_NO_ERROR) {
        rk_info_msg ("Error while attaching the vertex shader: 0x%04x\n", err);
    }

    glAttachShader(shader->program, shader->fragment_shader);
    err = glGetError ();
    if (err != GL_NO_ERROR) {
        rk_info_msg ("Error while attaching the fragment shader: 0x%04x\n", err);
    }

    glBindAttribLocation(shader->program, 0, "vPosition");
    glLinkProgram(shader->program);

    /* check linker status */
    glGetProgramiv(shader->program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLint info_len = 0;
        rk_info_msg("Linker failure\n");

        glGetProgramiv(shader->program, GL_INFO_LOG_LENGTH, &info_len);
        if(info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetProgramInfoLog(shader->program, info_len, NULL, info_log);

            rk_info_msg("Failed to link GL program: %s\n", info_log);
            free(info_log);
        }

        glDeleteProgram(shader->program);
        return -1;
    }

    glUseProgram(shader->program);

    shader->position_loc = glGetAttribLocation(shader->program, "vPosition");
    shader->texcoord_loc = glGetAttribLocation(shader->program, "aTexcoord");


    switch(process_type) {
        case SHADER_OES:
            shader->texture[0] = glGetUniformLocation(shader->program, "tex_external");
        CHECKEGL
        break;
    }

    return 0;
}

void
gl_delete_shader(shader_ctx_t *shader)
{
    glDeleteShader (shader->vertex_shader);
    shader->vertex_shader = 0;

    glDeleteShader (shader->fragment_shader);
    shader->fragment_shader = 0;

    glDeleteProgram (shader->program);
    shader->program = 0;
}

GLuint
gl_create_texture(GLuint tex_filter)
{
    GLuint tex_id = 0;

    glGenTextures (1, &tex_id);
    if (!tex_id) {
        rk_info_msg ("Could not create texture\n");
        return 0;
    }
    glBindTexture (GL_TEXTURE_2D, tex_id);
    CHECKEGL

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex_filter);
    CHECKEGL
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, tex_filter);
    CHECKEGL

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECKEGL
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECKEGL

    return tex_id;
}

extern int fd__;
VAStatus rockchip_PutSurface (
    VADisplay dpy,
    VASurfaceID surface,
    Drawable draw, /* X Drawable */
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx,
    short desty,
    unsigned short destw,
    unsigned short desth,
    VARectangle *cliprects, /* client supplied destination clip list */
    unsigned int number_cliprects, /* number of clip rects in the clip list */
    unsigned int flags /* PutSurface flags */
)
{
    VADriverContextP ctx = dpy;
    struct obj_surface2 *obj_surface = &obj_surface1;
    EGLint num_configs;
    EGLint major;
    EGLint minor;

    const EGLint configAttribs[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    rk_info_msg("first! %d\n", fd__);

    if (obj_surface->display == EGL_NO_DISPLAY) {
        obj_surface->display = eglGetDisplay((EGLNativeDisplayType)ctx->native_dpy);
        rk_info_msg("first!\n");
        if (obj_surface->display == EGL_NO_DISPLAY) {
            rk_info_msg("Could not get EGL display\n");
            return -1;
        }
        eglInitialize(obj_surface->display, &major, &minor);

        if (!eglChooseConfig(obj_surface->display, configAttribs, &obj_surface->config, 1,
                            &num_configs)) {
            rk_info_msg ("Could not choose EGL config\n");
            return -1;
        }
        if (num_configs != 1) {
            rk_info_msg("Did not get exactly one config, but %d\n",
                               num_configs);
        }
        obj_surface->surface = eglCreateWindowSurface(obj_surface->display, obj_surface->config,
                                         (EGLNativeWindowType)draw, NULL);
        if (obj_surface->surface == EGL_NO_SURFACE) {
             rk_info_msg ("Could not create EGL surface %x\n", eglGetError());
             return -1;
        }

        const EGLint contextAttribs[] =
        {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        obj_surface->context = eglCreateContext(obj_surface->display, obj_surface->config,
                                         EGL_NO_CONTEXT, contextAttribs);
        if (obj_surface->context == EGL_NO_CONTEXT) {
            rk_info_msg ("Could not create EGL context %x\n", eglGetError());
            return -1;
        }

        if (!eglMakeCurrent(obj_surface->display, obj_surface->surface,
                            obj_surface->surface, obj_surface->context)) {
            rk_info_msg ("Could not set EGL context to current %x\n", eglGetError());
            return -1;
        }



        if (gl_init_shader (&obj_surface->oes, SHADER_OES) < 0) {
            rk_info_msg ("Could not initialize shader\n");
            return -1;
        }

         /* oes tex */
         glGenTextures(1, &obj_surface->oes_tex);
         glBindTexture(GL_TEXTURE_EXTERNAL_OES, obj_surface->oes_tex);
         CHECKEGL
         glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         CHECKEGL
         glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         CHECKEGL
         glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         CHECKEGL
         glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         CHECKEGL

    }

        /* Do the GLES display of the video */
  static const float kVertices[] =
      { -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f, };
  static const float kTextureCoords[] = { 0, 1, 0, 0, 1, 1, 1, 0, };

        shader_ctx_t * shader = &obj_surface->oes;

        EGLint attrs[] = {
            EGL_WIDTH,                     0, EGL_HEIGHT,                    0,
            EGL_LINUX_DRM_FOURCC_EXT,      0, EGL_DMA_BUF_PLANE0_FD_EXT,     0,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0, EGL_DMA_BUF_PLANE0_PITCH_EXT,  0,
            EGL_DMA_BUF_PLANE1_FD_EXT,     0, EGL_DMA_BUF_PLANE1_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE1_PITCH_EXT,  0, EGL_YUV_COLOR_SPACE_HINT_EXT, 0,
            EGL_SAMPLE_RANGE_HINT_EXT, 0, EGL_NONE, };

        attrs[1] = srcw;
        attrs[3] = srch;
        attrs[5] = DRM_FORMAT_NV12;
        attrs[7]  = fd__;
        attrs[9]  = 0;
        attrs[11] = srcw;

        attrs[13] = fd__;
        attrs[15] = srcw * srch;
        attrs[17] = srcw;
        attrs[19] = EGL_ITU_REC601_EXT;
        attrs[21] = EGL_YUV_NARROW_RANGE_EXT;

        EGLImageKHR egl_image = eglCreateImageKHR(
                    obj_surface->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
        if (egl_image == EGL_NO_IMAGE_KHR)
            printf("failed egl image\n");

        glClear (GL_COLOR_BUFFER_BIT);
        CHECKEGL

        glUseProgram (shader->program);
        CHECKEGL

            glViewport(0, 0,
                srcw,
                srch);
            CHECKEGL

            glVertexAttribPointer (shader->position_loc, 2, GL_FLOAT,
                GL_FALSE, 0, kVertices);
            CHECKEGL
            glEnableVertexAttribArray (shader->position_loc);
            CHECKEGL

            glVertexAttribPointer (shader->texcoord_loc, 2, GL_FLOAT,
                GL_FALSE, 0, kTextureCoords);
            CHECKEGL
            glEnableVertexAttribArray (shader->texcoord_loc);
            CHECKEGL
            glUniform1i (shader->texture[0], 0);
            CHECKEGL


            glActiveTexture(GL_TEXTURE0);
            CHECKEGL
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, obj_surface->oes_tex);
            CHECKEGL
            glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, egl_image);
            CHECKEGL

            glActiveTexture(GL_TEXTURE0);
            CHECKEGL
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, obj_surface->oes_tex);
            CHECKEGL
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            CHECKEGL

            glUseProgram(0);
            CHECKEGL
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
            CHECKEGL

            eglSwapBuffers (obj_surface->display, obj_surface->surface);

            eglMakeCurrent(obj_surface->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            //eglDestroyImageKHR(q->device->egl.display, egl_image);



    return VA_STATUS_SUCCESS;
}
