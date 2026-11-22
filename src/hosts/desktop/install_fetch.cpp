//
// install_fetch.cpp — the network: a release on GitHub, and its files.
//
// libcurl, the system's on macOS and Linux. HTTPS only, certificates checked,
// redirects followed (GitHub serves assets from another host), and a
// User-Agent that names the product and nothing else: no account, no address,
// nothing about the person or the machine goes into a request.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

static const char USER_AGENT[] = "Okapia";

namespace
{
struct TSink
{
    std::string       *pBody;
    FILE              *pFile;
    TInstallProgress  *pProgress;
    void              *pContext;
    const char        *pName;
};

size_t Write (char *pData, size_t nSize, size_t nCount, void *pUser)
{
    TSink *pSink = (TSink *) pUser;
    const size_t n = nSize * nCount;
    if (pSink->pBody != 0)
    {
        pSink->pBody->append (pData, n);
        return n;
    }
    return fwrite (pData, 1, n, pSink->pFile);
}

int Progress (void *pUser, curl_off_t nTotal, curl_off_t nNow, curl_off_t, curl_off_t)
{
    TSink *pSink = (TSink *) pUser;
    if (pSink->pProgress != 0 && nTotal > 0)
        pSink->pProgress (pSink->pContext, pSink->pName, (unsigned long long) nNow, (unsigned long long) nTotal);
    return 0;
}

bool Perform (const std::string &Url, TSink *pSink, const char *pAccept, std::string *pError, long *pnStatus)
{
    if (Url.compare (0, 8, "https://") != 0)
    {
        *pError = "not HTTPS: " + Url;
        return false;
    }
    static bool s_bInit;
    if (!s_bInit)
    {
        curl_global_init (CURL_GLOBAL_DEFAULT);
        s_bInit = true;
    }
    CURL *pCurl = curl_easy_init ();
    if (pCurl == 0)
    {
        *pError = "no network library";
        return false;
    }
    struct curl_slist *pHeaders = 0;
    if (pAccept != 0)
        pHeaders = curl_slist_append (pHeaders, (std::string ("Accept: ") + pAccept).c_str ());
    curl_easy_setopt (pCurl, CURLOPT_URL, Url.c_str ());
    curl_easy_setopt (pCurl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt (pCurl, CURLOPT_HTTPHEADER, pHeaders);
    curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt (pCurl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt (pCurl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt (pCurl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt (pCurl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt (pCurl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // a stalled transfer ends
    curl_easy_setopt (pCurl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, Write);
    curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, pSink);
    curl_easy_setopt (pCurl, CURLOPT_XFERINFOFUNCTION, Progress);
    curl_easy_setopt (pCurl, CURLOPT_XFERINFODATA, pSink);
    curl_easy_setopt (pCurl, CURLOPT_NOPROGRESS, 0L);
    const CURLcode Code = curl_easy_perform (pCurl);
    long nStatus = 0;
    curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nStatus);
    curl_slist_free_all (pHeaders);
    curl_easy_cleanup (pCurl);
    *pnStatus = nStatus;
    if (Code != CURLE_OK)
    {
        *pError = std::string ("network: ") + curl_easy_strerror (Code);
        return false;
    }
    return true;
}
}

bool HttpGet (const std::string &Url, std::string *pBody, std::string *pError)
{
    TSink Sink = { pBody, 0, 0, 0, "" };
    pBody->clear ();
    long nStatus = 0;
    if (!Perform (Url, &Sink, "application/vnd.github+json", pError, &nStatus))
        return false;
    if (nStatus != 200)
    {
        *pError = "HTTP " + std::to_string (nStatus) + " for " + Url;
        return false;
    }
    return true;
}

bool HttpDownload (const std::string &Url, const std::string &Path, TInstallProgress *pProgress,
                   void *pContext, std::string *pError)
{
    FILE *pFile = fopen (Path.c_str (), "wb");
    if (pFile == 0)
    {
        *pError = Path + ": cannot be written";
        return false;
    }
    const size_t nSlash = Path.rfind ('/');
    const std::string Name = nSlash == std::string::npos ? Path : Path.substr (nSlash + 1);
    TSink Sink = { 0, pFile, pProgress, pContext, Name.c_str () };
    long nStatus = 0;
    const bool bOK = Perform (Url, &Sink, "application/octet-stream", pError, &nStatus);
    const bool bClosed = fclose (pFile) == 0;
    if (!bOK || !bClosed || nStatus != 200)
    {
        if (bOK && nStatus != 200)
            *pError = "HTTP " + std::to_string (nStatus) + " for " + Url;
        unlink (Path.c_str ());
        return false;
    }
    return true;
}

bool ReleaseFromGitHub (const std::string &Repository, TRelease *pOut, std::string *pError)
{
    *pOut = TRelease ();
    std::string Body;
    const std::string Url = "https://api.github.com/repos/" + Repository + "/releases/latest";
    if (!HttpGet (Url, &Body, pError))
    {
        if (pError->find ("HTTP 404") == 0)
            *pError = "no_release";     // the repository has published nothing yet
        return false;
    }
    TJson J;
    std::string Why;
    if (!JsonParse (Body, &J, &Why))
    {
        *pError = "GitHub's answer is not JSON: " + Why;
        return false;
    }
    const std::string Tag = J.Text ("tag_name");
    std::string ManifestUrl;
    if (const TJson *pAssets = J.Get ("assets"))
    {
        for (const TJson &A : pAssets->Items)
        {
            const std::string Name = A.Text ("name"), Where = A.Text ("browser_download_url");
            if (Name.empty () || Where.empty ())
                continue;
            pOut->Assets[Name] = Where;
            if (Name == MANIFEST_FILE)
                ManifestUrl = Where;
        }
    }
    if (ManifestUrl.empty ())
    {
        *pError = "release " + Tag + " has no " + MANIFEST_FILE;
        return false;
    }
    std::string Text;
    if (!HttpGet (ManifestUrl, &Text, pError) || !ManifestParse (Text, &pOut->Manifest, pError))
        return false;
    if (pOut->Manifest.Page.empty ())
        pOut->Manifest.Page = J.Text ("html_url");
    pOut->Description = Repository + " " + Tag;
    return true;
}
