#include "requisition.h"
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int BackendPort(const char* logs, INTERNET_PORT* out)
{
    wchar_t path[32768], wide[32768];
    if (!logs || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, logs, -1, wide, 32768)) return 0;
    for (int parent = 0; parent < 2; ++parent)
    {
        int n = swprintf(path, 32768, parent ? L"%ls\\..\\backend\\ready.json" : L"%ls\\backend\\ready.json", wide);
        if (n < 0 || n >= 32768) return 0;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) continue;
        char data[8192];
        DWORD read = 0;
        int ok = ReadFile(file, data, sizeof data - 1, &read, NULL) && read < sizeof data - 1;
        CloseHandle(file);
        if (!ok) return 0;
        data[read] = 0;
        const char* urls = strstr(data, "\"urls\":[\"http://127.0.0.1:");
        if (!urls) return 0;
        urls += strlen("\"urls\":[\"http://127.0.0.1:");
        char* end;
        unsigned long port = strtoul(urls, &end, 10);
        if (end == urls || (*end != '"' && *end != '/') || !port || port > 65535) return 0;
        *out = (INTERNET_PORT)port;
        return 1;
    }
    return 0;
}

/* Find one complete JSON object, respecting nested price objects and quoted strings. */
static const char* ObjectEnd(const char* start)
{
    int depth = 0, quoted = 0, escape = 0;
    for (const char* p = start; *p; ++p)
    {
        if (quoted)
        {
            if (escape) escape = 0;
            else if (*p == '\\') escape = 1;
            else if (*p == '"') quoted = 0;
        }
        else if (*p == '"') quoted = 1;
        else if (*p == '{') ++depth;
        else if (*p == '}' && --depth == 0) return p + 1;
    }
    return NULL;
}

static int TokenPrice(const char* catalogue, uint32_t token, unsigned price)
{
    char identity[64], expected[96];
    snprintf(identity, sizeof identity, "\"mixId\":%u,", token);
    snprintf(expected, sizeof expected, "\"buyPrice\":[{\"mixId\":3608481516,\"amount\":%u}]", price);
    const char* item = strstr(catalogue, identity);
    if (!item) return 0;
    const char* start = item;
    while (start > catalogue && *start != '{') --start;
    const char* end = *start == '{' ? ObjectEnd(start) : NULL;
    const char* buy = strstr(item, expected);
    return end && buy && buy + strlen(expected) <= end;
}

/* Returns 1 only when the server's receipt confirms the price; -1 means POST may have committed. */
int SmrSpendRequisition(const char* logs, uint32_t token, unsigned amount, char* error, size_t error_size)
{
    INTERNET_PORT port;
    if (!BackendPort(logs, &port))
    {
        snprintf(error, error_size, "Cannot find this session's local backend.");
        return 0;
    }
    int result = 0, posted = 0;
    HINTERNET session = WinHttpOpen(L"SpendMyResources/0.7", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), connection = NULL, request = NULL;
    char* response = NULL;
    DWORD failure = 0;
    if (!session) { failure = GetLastError(); goto done; }
    if (!WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000)) { failure = GetLastError(); goto done; }
    connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
    if (!connection) { failure = GetLastError(); goto done; }
    response = malloc(4 * 1024 * 1024);
    if (!response) { snprintf(error, error_size, "Cannot allocate catalogue buffer."); goto done; }
    for (int phase = 0; phase < 2; ++phase)
    {
        request = WinHttpOpenRequest(connection, phase ? L"POST" : L"GET",
            phase ? L"/Progression/buy/v2" : L"/Progression/items", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!request) { failure = GetLastError(); goto done; }
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects))
        { failure = GetLastError(); goto done; }
        char body[96];
        int body_size = snprintf(body, sizeof body, "{\"mixId\":%u,\"amount\":1}", token);
        if (phase) posted = 1;
        if (!WinHttpSendRequest(request, phase ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
                phase ? (DWORD)-1 : 0, phase ? body : WINHTTP_NO_REQUEST_DATA,
                phase ? (DWORD)body_size : 0, phase ? (DWORD)body_size : 0, 0) ||
            !WinHttpReceiveResponse(request, NULL)) { failure = GetLastError(); goto done; }
        DWORD status = 0, status_size = sizeof status;
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
        { failure = GetLastError(); goto done; }
        if (status != 200)
        {
            snprintf(error, error_size, "%s returned HTTP %lu.", phase ? "Purchase" : "Catalogue", (unsigned long)status);
            if (phase && status >= 500) result = -1;
            goto done;
        }
        DWORD used = 0, read;
        do
        {
            if (used == 4 * 1024 * 1024 - 1) { snprintf(error, error_size, "Backend response too large."); result = phase ? -1 : 0; goto done; }
            if (!WinHttpReadData(request, response + used, 4 * 1024 * 1024 - 1 - used, &read))
            { failure = GetLastError(); goto done; }
            used += read;
        } while (read);
        response[used] = 0;
        WinHttpCloseHandle(request); request = NULL;
        if (!phase)
        {
            if (!TokenPrice(response, token, amount))
            { snprintf(error, error_size, "Token missing or its price is not exactly %u requisition.", amount); goto done; }
        }
        else
        {
            char receipt[160];
            int n = snprintf(receipt, sizeof receipt,
                "[{\"mixId\":%u,\"amount\":1},{\"mixId\":3608481516,\"amount\":-%u}]", token, amount);
            if (used < (DWORD)n || strncmp(response, receipt, (size_t)n) || strspn(response + n, " \r\n\t") != used - (size_t)n)
            { snprintf(error, error_size, "Unexpected purchase receipt; check the profile before trying again."); result = -1; goto done; }
            result = 1;
        }
    }
done:
    if (failure)
    {
        snprintf(error, error_size, "Backend request failed (Windows error %lu)%s.", (unsigned long)failure,
            posted ? "; outcome unknown, check the profile" : "");
        result = posted ? -1 : 0;
    }
    free(response);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return result;
}
