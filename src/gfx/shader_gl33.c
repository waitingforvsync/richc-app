/*
 * shader_gl33.c - OpenGL 3.3 implementation of shader.h.
 */

#include "richc/gfx/shader.h"
#include <glad/gl.h>
#include <stdio.h>

static GLuint compile_stage_(GLenum type, rc_str src, rc_arena *scratch)
{
    GLuint s = glCreateShader(type);
    const GLchar *ptr = src.data;
    GLint len = (GLint)src.len;
    glShaderSource(s, 1, &ptr, &len);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &log_len);
        char *log = rc_arena_alloc_type(scratch, char, (uint32_t)log_len);
        glGetShaderInfoLog(s, log_len, NULL, log);
        fprintf(stderr, "rc_shader: compile error:\n%s\n", log);
        RC_PANIC(0);
        return 0; /* unreachable */
    }
    return s;
}

rc_shader rc_shader_make(rc_str vert_src, rc_str frag_src, rc_arena scratch)
{
    GLuint vert = compile_stage_(GL_VERTEX_SHADER,   vert_src, &scratch);
    GLuint frag = compile_stage_(GL_FRAGMENT_SHADER, frag_src, &scratch);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        char *log = rc_arena_alloc_type(&scratch, char, (uint32_t)log_len);
        glGetProgramInfoLog(prog, log_len, NULL, log);
        fprintf(stderr, "rc_shader: link error:\n%s\n", log);
        RC_PANIC(0);
        return (rc_shader) { 0 }; /* unreachable */
    }

    glDetachShader(prog, vert);
    glDetachShader(prog, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    return (rc_shader) { prog };
}

void rc_shader_destroy(rc_shader sh)
{
    glDeleteProgram(sh.id);
}

rc_uniform_loc rc_shader_loc(rc_shader sh, const char *name)
{
    return (rc_uniform_loc) { glGetUniformLocation(sh.id, name) };
}

void rc_shader_bind(rc_shader sh)
{
    glUseProgram(sh.id);
}

void rc_shader_set_f32(rc_uniform_loc loc, float v)
{
    if (loc.loc < 0) return;
    glUniform1f(loc.loc, v);
}

void rc_shader_set_i32(rc_uniform_loc loc, int32_t v)
{
    if (loc.loc < 0) return;
    glUniform1i(loc.loc, v);
}

void rc_shader_set_vec2(rc_uniform_loc loc, rc_vec2f v)
{
    if (loc.loc < 0) return;
    glUniform2f(loc.loc, v.x, v.y);
}

void rc_shader_set_mat4(rc_uniform_loc loc, const float *m)
{
    if (loc.loc < 0) return;
    glUniformMatrix4fv(loc.loc, 1, GL_FALSE, m);
}
