// Link shims: the vitasdk image's libSDL2.a (SDL_vitagles_vgl.c) references two symbols that the pinned vitaGL
// revision (2bdbe89, see docker/vita-sdk.Dockerfile) does not export. They are only reached through SDL's own
// GL context path (SDL_GL_GetProcAddress / SDL_GL_SetSwapInterval), which this port never uses: rendering goes
// straight to vitaGL (src/d3d8, src/vita). Fail loudly-but-safely if that ever changes.
#include <stddef.h>
void *vglGetProcAddress(const char *name) { (void)name; return NULL; }
unsigned eglSwapInterval(void *display, int interval) { (void)display; (void)interval; return 0; /* EGL_FALSE */ }
