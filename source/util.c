/* util.c -- Linux utility functions
 *
 * The original Switch implementation provided nxlink logging, CPU boost,
 * thread suspension, and libnx TLS helpers.  Linux keeps the same small API
 * for the game-facing code, but uses ordinary POSIX facilities instead.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "config.h"

static FILE *g_log;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

void userAppInit(void) {
#ifdef DEBUG_LOG
  pthread_mutex_lock(&g_log_lock);
  if (!g_log)
    g_log = fopen(LOG_NAME, "w");
  pthread_mutex_unlock(&g_log_lock);
#endif
}

void userAppExit(void) {
  pthread_mutex_lock(&g_log_lock);
  if (g_log) {
    fclose(g_log);
    g_log = NULL;
  }
  pthread_mutex_unlock(&g_log_lock);
}

/* The Android game imports printf through the loader's import table. */
int debugPrintf(char *text, ...) {
#ifdef DEBUG_LOG
  va_list list;
  pthread_mutex_lock(&g_log_lock);

  if (!g_log)
    g_log = fopen(LOG_NAME, "w");

  va_start(list, text);
  if (g_log) {
    vfprintf(g_log, text, list);
    fflush(g_log);
  }
  va_end(list);

  va_start(list, text);
  vfprintf(stderr, text, list);
  va_end(list);
  pthread_mutex_unlock(&g_log_lock);
#else
  (void)text;
#endif
  return 0;
}

size_t strlcpy(char *dst, const char *src, size_t dst_size) {
  size_t src_len = strlen(src);
  if (dst_size != 0) {
    size_t copy_len = src_len < dst_size - 1 ? src_len : dst_size - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
  }
  return src_len;
}

/* Linux does not expose a portable userspace CPU-boost API. */
void cpu_boost(int on) {
  (void)on;
}

void set_thread_core(int core) {
  if (core < 0 || core >= CPU_SETSIZE)
    return;

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET((unsigned)core, &set);
  if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
    debugPrintf("affinity: pin to core %d failed: %s\n", core, strerror(errno));
}

/*
 * Android's AArch64 stack guard is read from TPIDR_EL0+0x28.  The Switch
 * version installs a private TPIDR_EL0 block.  Linux owns TPIDR_EL0 for the
 * pthread TLS ABI, so do not overwrite it here; return an aligned guard block
 * for callers that only need the expected memory layout.  The actual Linux
 * TLS/stack-guard bridge belongs in the Bionic compatibility layer.
 */
#define MAX_GAME_TLS_THREADS 64
#define GAME_TLS_SIZE 0x1000
#define GAME_TLS_GUARD UINT64_C(0x4242424242424242)

static uint8_t g_game_tls[MAX_GAME_TLS_THREADS][GAME_TLS_SIZE]
    __attribute__((aligned(GAME_TLS_SIZE)));
static unsigned g_game_tls_count;

void *game_tls_install(void) {
  unsigned slot = __atomic_fetch_add(&g_game_tls_count, 1, __ATOMIC_RELAXED);
  if (slot >= MAX_GAME_TLS_THREADS) {
    debugPrintf("TLS: exhausted %u dedicated game slots\n",
                MAX_GAME_TLS_THREADS);
    return NULL;
  }

  uint8_t *tls = g_game_tls[slot];
  memset(tls, 0, GAME_TLS_SIZE);
  const uint64_t guard = GAME_TLS_GUARD;
  memcpy(tls + 0x28, &guard, sizeof(guard));
  return tls;
}

#define MAX_TRACKED_THREADS 64
static pthread_t g_thread_handles[MAX_TRACKED_THREADS];
static int g_thread_count;
static pthread_mutex_t g_thread_lock = PTHREAD_MUTEX_INITIALIZER;

void thread_registry_add(void) {
  pthread_mutex_lock(&g_thread_lock);
  if (g_thread_count < MAX_TRACKED_THREADS)
    g_thread_handles[g_thread_count++] = pthread_self();
  pthread_mutex_unlock(&g_thread_lock);
}

void thread_registry_pause_others(void) {
  /* POSIX has no safe portable equivalent to Switch thread suspension. */
  pthread_mutex_lock(&g_thread_lock);
  int n = g_thread_count;
  pthread_mutex_unlock(&g_thread_lock);
  debugPrintf("EXIT: Linux thread suspension unavailable (%d tracked)\n", n);
}

int ret0(void) { return 0; }
int retm1(void) { return -1; }
