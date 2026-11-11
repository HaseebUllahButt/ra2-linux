/* SDL mutexes keep the upstream mixer free of a Windows pthread dependency. */
#ifndef RA_PTHREAD_COMPAT_H
#define RA_PTHREAD_COMPAT_H
#include <SDL2/SDL.h>
typedef struct { SDL_mutex *mutex; SDL_SpinLock initialize; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {NULL,0}
static inline int pthread_mutex_lock(pthread_mutex_t *p)
{
    SDL_AtomicLock(&p->initialize);
    if (!p->mutex) p->mutex=SDL_CreateMutex();
    SDL_AtomicUnlock(&p->initialize);
    return p->mutex ? SDL_LockMutex(p->mutex) : -1;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *p) { return SDL_UnlockMutex(p->mutex); }
#endif
