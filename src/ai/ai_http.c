// Minimal blocking HTTP client for the AI dialog mod.
// macOS/Linux: libcurl (ships with the OS on macOS).
// Windows: WinHTTP (part of the OS, link -lwinhttp).
#ifdef PORTABLE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ai/ai_http.h"

#ifndef _WIN32

#include <curl/curl.h>

struct ResponseBuf
{
    char *data;
    size_t len;
};

static size_t WriteCb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    struct ResponseBuf *buf = (struct ResponseBuf *)userp;
    char *grown = realloc(buf->data, buf->len + total + 1);

    if (grown == NULL)
        return 0;
    buf->data = grown;
    memcpy(buf->data + buf->len, contents, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

char *AiHttp_Post(const char *url, const char *const *headers, int numHeaders,
                  const char *body, int timeoutMs, long *httpStatus)
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *headerList = NULL;
    struct ResponseBuf buf = { NULL, 0 };
    int i;

    *httpStatus = 0;
    curl = curl_easy_init();
    if (curl == NULL)
        return NULL;

    for (i = 0; i < numHeaders; i++)
        headerList = curl_slist_append(headerList, headers[i]);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)(timeoutMs / 2));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pokeemerald-ai-dialog/1.0");

    res = curl_easy_perform(curl);
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpStatus);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

#else // _WIN32

#include <windows.h>
#include <winhttp.h>

static WCHAR *ToWide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = malloc(n * sizeof(WCHAR));
    if (w != NULL)
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

char *AiHttp_Post(const char *url, const char *const *headers, int numHeaders,
                  const char *body, int timeoutMs, long *httpStatus)
{
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    URL_COMPONENTS uc;
    WCHAR *wUrl = NULL, *wHeaders = NULL;
    WCHAR host[256], path[1024];
    char *result = NULL;
    size_t resultLen = 0;
    char headerBuf[1024];
    int i;
    BOOL ok;

    *httpStatus = 0;
    wUrl = ToWide(url);
    if (wUrl == NULL)
        return NULL;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wUrl, 0, 0, &uc))
        goto done;

    hSession = WinHttpOpen(L"pokeemerald-ai-dialog/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL)
        goto done;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (hConnect == NULL)
        goto done;

    hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (hRequest == NULL)
        goto done;

    headerBuf[0] = '\0';
    for (i = 0; i < numHeaders; i++)
    {
        strncat(headerBuf, headers[i], sizeof(headerBuf) - strlen(headerBuf) - 3);
        strncat(headerBuf, "\r\n", sizeof(headerBuf) - strlen(headerBuf) - 1);
    }
    wHeaders = ToWide(headerBuf);

    ok = WinHttpSendRequest(hRequest, wHeaders, (DWORD)-1,
                            (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0);
    if (!ok || !WinHttpReceiveResponse(hRequest, NULL))
        goto done;

    {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        *httpStatus = (long)statusCode;
    }

    for (;;)
    {
        DWORD avail = 0, read = 0;
        char *grown;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;
        grown = realloc(result, resultLen + avail + 1);
        if (grown == NULL)
            break;
        result = grown;
        if (!WinHttpReadData(hRequest, result + resultLen, avail, &read))
            break;
        resultLen += read;
        result[resultLen] = '\0';
    }

done:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    free(wUrl);
    free(wHeaders);
    return result;
}

#endif // _WIN32
#endif // PORTABLE
