// AI dialog backend: configuration, worker thread, and the two LLM clients
// (Anthropic API and any OpenAI-compatible local server such as Ollama or
// llama.cpp). Everything network-related runs on a detached SDL thread; the
// game loop only polls an atomic state flag.
#ifdef PORTABLE

#include "global.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "ai/ai_backend.h"
#include "ai/ai_http.h"
#include "ai/cJSON.h"

#define AI_BACKEND_OFF 0
#define AI_BACKEND_ANTHROPIC 1
#define AI_BACKEND_LOCAL 2

#define AI_MAX_REPLY 512
#define AI_MEMORY_SLOTS 8

struct AiConfig
{
    int backend;
    char anthropicKey[256];
    char anthropicModel[64];
    int anthropicThinking; // 0 = disabled (fast), 1 = model default
    char localBaseUrl[256];
    char localModel[64];
    char localKey[256];
    char language[64]; // e.g. "Italian" — all NPC dialog is generated in it
    char style[256];   // global flavor directive applied to every NPC
    int timeoutMs;
    int maxTokens;
};

// One remembered exchange per NPC so repeat conversations stay coherent.
struct AiMemorySlot
{
    char npcKey[32];
    char lastReply[AI_MAX_REPLY];
    u32 age;
};

static struct AiConfig sConfig;
static bool32 sConfigLoaded = FALSE;

static SDL_atomic_t sState;                 // AI_REPLY_*
static AiDialogRequest sActiveRequest;      // owned by worker while pending
static char sReply[AI_MAX_REPLY];           // written by worker, read after DONE
static struct AiMemorySlot sMemory[AI_MEMORY_SLOTS]; // worker thread only
static u32 sMemoryClock;

static void TrimNewline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static void LoadConfig(void)
{
    FILE *f;
    char line[512];
    const char *envKey;

    if (sConfigLoaded)
        return;
    sConfigLoaded = TRUE;

    memset(&sConfig, 0, sizeof(sConfig));
    sConfig.backend = AI_BACKEND_OFF;
    strcpy(sConfig.anthropicModel, "claude-opus-5");
    strcpy(sConfig.localBaseUrl, "http://127.0.0.1:11434/v1");
    strcpy(sConfig.localModel, "qwen2.5:1.5b");
    sConfig.timeoutMs = 10000;
    sConfig.maxTokens = 200;
    sConfig.anthropicThinking = 0;

    f = fopen("ai_dialog.cfg", "r");
    if (f != NULL)
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char *eq, *key, *val;
            if (line[0] == '#' || line[0] == '\n')
                continue;
            eq = strchr(line, '=');
            if (eq == NULL)
                continue;
            *eq = '\0';
            key = line;
            val = eq + 1;
            TrimNewline(val);

            if (strcmp(key, "backend") == 0)
            {
                if (strcmp(val, "anthropic") == 0) sConfig.backend = AI_BACKEND_ANTHROPIC;
                else if (strcmp(val, "local") == 0) sConfig.backend = AI_BACKEND_LOCAL;
                else sConfig.backend = AI_BACKEND_OFF;
            }
            else if (strcmp(key, "anthropic_key") == 0) snprintf(sConfig.anthropicKey, sizeof(sConfig.anthropicKey), "%s", val);
            else if (strcmp(key, "anthropic_model") == 0) snprintf(sConfig.anthropicModel, sizeof(sConfig.anthropicModel), "%s", val);
            else if (strcmp(key, "anthropic_thinking") == 0) sConfig.anthropicThinking = (strcmp(val, "disabled") != 0);
            else if (strcmp(key, "local_base_url") == 0) snprintf(sConfig.localBaseUrl, sizeof(sConfig.localBaseUrl), "%s", val);
            else if (strcmp(key, "local_model") == 0) snprintf(sConfig.localModel, sizeof(sConfig.localModel), "%s", val);
            else if (strcmp(key, "local_key") == 0) snprintf(sConfig.localKey, sizeof(sConfig.localKey), "%s", val);
            else if (strcmp(key, "language") == 0) snprintf(sConfig.language, sizeof(sConfig.language), "%s", val);
            else if (strcmp(key, "style") == 0) snprintf(sConfig.style, sizeof(sConfig.style), "%s", val);
            else if (strcmp(key, "timeout_ms") == 0) sConfig.timeoutMs = atoi(val);
            else if (strcmp(key, "max_tokens") == 0) sConfig.maxTokens = atoi(val);
        }
        fclose(f);
    }

    envKey = getenv("ANTHROPIC_API_KEY");
    if (envKey != NULL && envKey[0] != '\0' && sConfig.anthropicKey[0] == '\0')
        snprintf(sConfig.anthropicKey, sizeof(sConfig.anthropicKey), "%s", envKey);

    // AI_DIALOG_STYLE overrides the config's style= for quick experimentation.
    {
        const char *envStyle = getenv("AI_DIALOG_STYLE");
        if (envStyle != NULL && envStyle[0] != '\0')
            snprintf(sConfig.style, sizeof(sConfig.style), "%s", envStyle);
    }

    // No config file but a key in the environment: default to Anthropic.
    if (f == NULL && sConfig.anthropicKey[0] != '\0')
        sConfig.backend = AI_BACKEND_ANTHROPIC;

    if (sConfig.backend == AI_BACKEND_ANTHROPIC && sConfig.anthropicKey[0] == '\0')
        sConfig.backend = AI_BACKEND_OFF;
    if (sConfig.timeoutMs < 1000)
        sConfig.timeoutMs = 1000;
}

bool32 AiBackend_IsEnabled(void)
{
    LoadConfig();
    return sConfig.backend != AI_BACKEND_OFF;
}

int AiBackend_GetTimeoutMs(void)
{
    LoadConfig();
    return sConfig.timeoutMs + 2000; // margin over the HTTP timeout
}

// ---------------------------------------------------------------------------
// Worker-thread side
// ---------------------------------------------------------------------------

static struct AiMemorySlot *FindMemory(const char *npcKey, bool32 create)
{
    int i, oldest = 0;

    for (i = 0; i < AI_MEMORY_SLOTS; i++)
    {
        if (sMemory[i].npcKey[0] != '\0' && strcmp(sMemory[i].npcKey, npcKey) == 0)
            return &sMemory[i];
        if (sMemory[i].age < sMemory[oldest].age)
            oldest = i;
    }
    if (!create)
        return NULL;
    memset(&sMemory[oldest], 0, sizeof(sMemory[oldest]));
    snprintf(sMemory[oldest].npcKey, sizeof(sMemory[oldest].npcKey), "%s", npcKey);
    return &sMemory[oldest];
}

static void BuildPrompts(const AiDialogRequest *req, char *sysOut, int sysSize,
                         char *usrOut, int usrSize)
{
    const struct AiMemorySlot *mem = FindMemory(req->npcKey, FALSE);

    int n = snprintf(sysOut, sysSize,
        "You are writing one line of NPC dialog for Pokemon Emerald. "
        "Reply with only the spoken line: at most 2 short sentences and 150 characters. "
        "Prefer plain ASCII, no quotes around the line, no emoji, no stage directions. "
        "The scripted line is the NPC's canonical knowledge: keep its meaning, facts and "
        "any directions intact, but rephrase it freshly with personality fitting the speaker. "
        "Never break character or mention being an AI.");

    // Generate all dialog in a chosen language (e.g. "Italian"). Note: only
    // NPC dialog is affected; the game's menus/UI stay English.
    if (n > 0 && n < sysSize && sConfig.language[0] != '\0')
        n += snprintf(sysOut + n, sysSize - n,
                 " Write the line in %s, natural and idiomatic, not a literal translation.",
                 sConfig.language);

    // Global style directive, applied to every NPC (e.g. "speak in Sicilian",
    // "everyone is furious and shouting"). Given strong weight in the prompt.
    if (n > 0 && n < sysSize && sConfig.style[0] != '\0')
        snprintf(sysOut + n, sysSize - n,
                 " IMPORTANT STYLE — apply this to the line no matter what: %s",
                 sConfig.style);

    snprintf(usrOut, usrSize,
        "Location: %s\n"
        "Speaker: %s\n"
        "Player's name: %s\n"
        "Scripted line: \"%s\"\n"
        "%s%s%s"
        "Write the speaker's line.",
        req->mapName[0] ? req->mapName : "Hoenn",
        req->persona[0] ? req->persona : "a villager",
        req->playerName[0] ? req->playerName : "the player",
        req->originalText,
        (mem != NULL && mem->lastReply[0]) ? "Last time this NPC said: \"" : "",
        (mem != NULL && mem->lastReply[0]) ? mem->lastReply : "",
        (mem != NULL && mem->lastReply[0]) ? "\" (say something that doesn't repeat it verbatim)\n" : "");
}

// Strip anything that would break the text box: tags, quotes, whitespace runs.
static void SanitizeReply(char *reply)
{
    char *src = reply, *dst = reply;
    int inTag = 0;
    size_t n;

    while (*src != '\0')
    {
        char c = *src++;
        if (c == '<') { inTag = 1; continue; }
        if (c == '>') { inTag = 0; continue; }
        if (inTag) continue;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && dst > reply && dst[-1] == ' ') continue;
        *dst++ = c;
    }
    *dst = '\0';
    TrimNewline(reply);
    n = strlen(reply);
    if (n >= 2 && reply[0] == '"' && reply[n - 1] == '"')
    {
        memmove(reply, reply + 1, n - 2);
        reply[n - 2] = '\0';
    }
    while (reply[0] == ' ')
        memmove(reply, reply + 1, strlen(reply));
}

static bool32 CallAnthropic(const char *sys, const char *usr, char *out, int outSize)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages, *msg, *thinking;
    char *body;
    char authHeader[320];
    const char *headers[3];
    char *resp;
    long status = 0;
    bool32 success = FALSE;

    cJSON_AddStringToObject(root, "model", sConfig.anthropicModel);
    cJSON_AddNumberToObject(root, "max_tokens", sConfig.maxTokens);
    if (!sConfig.anthropicThinking)
    {
        // Skip extended thinking for snappy dialog latency.
        thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "disabled");
        cJSON_AddItemToObject(root, "thinking", thinking);
    }
    cJSON_AddStringToObject(root, "system", sys);
    messages = cJSON_CreateArray();
    msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", usr);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(root, "messages", messages);
    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL)
        return FALSE;

    snprintf(authHeader, sizeof(authHeader), "x-api-key: %s", sConfig.anthropicKey);
    headers[0] = authHeader;
    headers[1] = "anthropic-version: 2023-06-01";
    headers[2] = "content-type: application/json";

    resp = AiHttp_Post("https://api.anthropic.com/v1/messages", headers, 3, body,
                       sConfig.timeoutMs, &status);
    free(body);
    if (resp == NULL)
        return FALSE;

    if (status == 200)
    {
        cJSON *json = cJSON_Parse(resp);
        if (json != NULL)
        {
            const cJSON *stop = cJSON_GetObjectItemCaseSensitive(json, "stop_reason");
            const cJSON *content = cJSON_GetObjectItemCaseSensitive(json, "content");
            // A refusal falls through with no text and we show the scripted line.
            if (!(cJSON_IsString(stop) && strcmp(stop->valuestring, "refusal") == 0)
                && cJSON_IsArray(content))
            {
                const cJSON *block;
                cJSON_ArrayForEach(block, content)
                {
                    const cJSON *type = cJSON_GetObjectItemCaseSensitive(block, "type");
                    const cJSON *text = cJSON_GetObjectItemCaseSensitive(block, "text");
                    if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0
                        && cJSON_IsString(text) && text->valuestring[0] != '\0')
                    {
                        snprintf(out, outSize, "%s", text->valuestring);
                        success = TRUE;
                        break;
                    }
                }
            }
            cJSON_Delete(json);
        }
    }
    free(resp);
    return success;
}

static bool32 CallLocal(const char *sys, const char *usr, char *out, int outSize)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages, *msg;
    char *body;
    char url[320];
    char authHeader[320];
    const char *headers[2];
    int numHeaders = 1;
    char *resp;
    long status = 0;
    bool32 success = FALSE;

    cJSON_AddStringToObject(root, "model", sConfig.localModel);
    cJSON_AddNumberToObject(root, "max_tokens", sConfig.maxTokens);
    messages = cJSON_CreateArray();
    msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "system");
    cJSON_AddStringToObject(msg, "content", sys);
    cJSON_AddItemToArray(messages, msg);
    msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", usr);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(root, "messages", messages);
    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL)
        return FALSE;

    snprintf(url, sizeof(url), "%s/chat/completions", sConfig.localBaseUrl);
    headers[0] = "content-type: application/json";
    if (sConfig.localKey[0] != '\0')
    {
        snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", sConfig.localKey);
        headers[numHeaders++] = authHeader;
    }

    resp = AiHttp_Post(url, headers, numHeaders, body, sConfig.timeoutMs, &status);
    free(body);
    if (resp == NULL)
        return FALSE;

    if (status == 200)
    {
        cJSON *json = cJSON_Parse(resp);
        if (json != NULL)
        {
            const cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
            const cJSON *first = cJSON_GetArrayItem(choices, 0);
            if (first != NULL)
            {
                const cJSON *message = cJSON_GetObjectItemCaseSensitive(first, "message");
                const cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
                if (cJSON_IsString(content) && content->valuestring[0] != '\0')
                {
                    snprintf(out, outSize, "%s", content->valuestring);
                    success = TRUE;
                }
            }
            cJSON_Delete(json);
        }
    }
    free(resp);
    return success;
}

static int WorkerMain(void *unused)
{
    char sys[512], usr[1200];
    char reply[AI_MAX_REPLY];
    bool32 ok = FALSE;

    BuildPrompts(&sActiveRequest, sys, sizeof(sys), usr, sizeof(usr));
    reply[0] = '\0';

    if (sConfig.backend == AI_BACKEND_ANTHROPIC)
        ok = CallAnthropic(sys, usr, reply, sizeof(reply));
    else if (sConfig.backend == AI_BACKEND_LOCAL)
        ok = CallLocal(sys, usr, reply, sizeof(reply));

    if (ok)
    {
        SanitizeReply(reply);
        ok = reply[0] != '\0';
    }

    if (ok)
    {
        struct AiMemorySlot *mem = FindMemory(sActiveRequest.npcKey, TRUE);
        snprintf(mem->lastReply, sizeof(mem->lastReply), "%s", reply);
        mem->age = ++sMemoryClock;
        snprintf(sReply, sizeof(sReply), "%s", reply);
        SDL_AtomicSet(&sState, AI_REPLY_DONE);
    }
    else
    {
        SDL_AtomicSet(&sState, AI_REPLY_ERROR);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Game-loop side
// ---------------------------------------------------------------------------

static bool32 sBusy = FALSE;

bool32 AiBackend_Submit(const AiDialogRequest *req)
{
    SDL_Thread *thread;

    if (!AiBackend_IsEnabled())
        return FALSE;
    if (sBusy && SDL_AtomicGet(&sState) == AI_REPLY_PENDING)
        return FALSE;

    sActiveRequest = *req;
    sReply[0] = '\0';
    SDL_AtomicSet(&sState, AI_REPLY_PENDING);
    sBusy = TRUE;

    thread = SDL_CreateThread(WorkerMain, "AiDialogWorker", NULL);
    if (thread == NULL)
    {
        sBusy = FALSE;
        SDL_AtomicSet(&sState, AI_REPLY_ERROR);
        return FALSE;
    }
    SDL_DetachThread(thread);
    return TRUE;
}

int AiBackend_Poll(char *replyOut, int replySize)
{
    int state = SDL_AtomicGet(&sState);

    if (state == AI_REPLY_DONE)
    {
        snprintf(replyOut, replySize, "%s", sReply);
        sBusy = FALSE;
    }
    else if (state == AI_REPLY_ERROR)
    {
        sBusy = FALSE;
    }
    return state;
}

#endif // PORTABLE
