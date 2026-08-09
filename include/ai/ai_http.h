#ifndef GUARD_AI_HTTP_H
#define GUARD_AI_HTTP_H

// Blocking HTTP POST. Only ever called from the AI worker thread, never
// from the game loop. Returns a malloc'd, NUL-terminated response body
// (caller frees) or NULL on transport error/timeout. httpStatus receives
// the response status code (0 on transport error).
char *AiHttp_Post(const char *url, const char *const *headers, int numHeaders,
                  const char *body, int timeoutMs, long *httpStatus);

#endif // GUARD_AI_HTTP_H
