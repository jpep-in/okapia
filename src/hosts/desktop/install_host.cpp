//
// install_host.cpp — the release and the preparing of a card, for the welcome
// page (okapia_host.h: HostRelease, HostCardPlan, HostPrepareStart/Status).
//
// Both run on a thread of their own, the welcome page polling them four times
// a second: a lookup on the network or a card being written must not stop the
// window. The release is GitHub's latest (OKAPIA_REPOSITORY), or a release on
// this computer when OKAPIA_RELEASE names one (the test mode's --release). What
// is downloaded is kept in the app's support folder, under releases/.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"
#include "okapia_card.h"
#include "okapia_desktop.h"
#include "okapia_host.h"

#include "okapia_version.h"

#include <SDL3/SDL.h>
#if defined (__APPLE__)
#include <sys/sysctl.h>
#endif
#include <time.h>

#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

static const char FROM[] = "install";

static std::mutex   s_Lock;
static bool         s_bLooking;
static THostRelease s_Release = { HostReleaseLooking, "", "", 0, {}, {} };
static TRelease     s_Source;
static THostPrepare s_Prepare;
static bool         s_bPreparing;

static void Copy (char *pOut, unsigned nSize, const std::string &s)
{
    snprintf (pOut, nSize, "%s", s.c_str ());
}

static void LookUp (void)
{
    TRelease R;
    std::string Why;
    const char *pLocal = getenv ("OKAPIA_RELEASE");
    const bool bOK = pLocal != 0 ? ReleaseFromLocal (pLocal, &R, &Why)
                                 : ReleaseFromGitHub (OKAPIA_REPOSITORY, &R, &Why);
    std::lock_guard<std::mutex> Guard (s_Lock);
    if (!bOK)
    {
        s_Release.State = Why == "no_release" ? HostReleaseNone : HostReleaseFailed;
        Copy (s_Release.Why, sizeof s_Release.Why, Why);
        HostLog (FROM, HostLogNotice, "Release: %s", Why.c_str ());
        return;
    }
    s_Source = R;
    s_Release.State = HostReleaseReady;
    Copy (s_Release.Version, sizeof s_Release.Version, R.Manifest.Version);
    const auto Boards = ManifestBoards (R.Manifest);
    s_Release.nBoards = 0;
    for (const auto &B : Boards)
    {
        if (s_Release.nBoards == HOST_BOARDS)
            break;
        Copy (s_Release.BoardId[s_Release.nBoards], sizeof s_Release.BoardId[0], B.first);
        Copy (s_Release.BoardName[s_Release.nBoards], sizeof s_Release.BoardName[0], B.second);
        s_Release.nBoards++;
    }
    HostLog (FROM, HostLogNotice, "Release: Okapia %s from %s, %u board(s)", R.Manifest.Version.c_str (),
             R.Description.c_str (), s_Release.nBoards);
}

// With s_Lock held.
static void StartLookUp (void)
{
    if (!s_bLooking)
    {
        s_bLooking = true;
        std::thread (LookUp).detach ();
    }
}

void HostRelease (THostRelease *pOut)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    StartLookUp ();
    *pOut = s_Release;
}

/*
 *  A newer Okapia for this computer (R5)
 *
 *  Once a day at most: the last answer is kept in the app's support folder
 *  (update: when, the version, its page, whether it is newer than this one)
 *  and used while less than a day old, so most starts touch no network. A
 *  failed lookup (offline) keeps nothing and says nothing. update-off in the
 *  same folder turns the check off (the Okapia menu's item).
 */
static THostUpdate s_Update = { HostUpdateChecking, "", "" };
static bool        s_bUpdateDecided;
static bool        s_bUpdateWaiting;     // for the lookup to finish
static bool        s_bUpdateForced;      // Check now: the cache not trusted

static std::string SupportFile (const char *pName)
{
    char *pPref = SDL_GetPrefPath ("Okapia", "Okapia");
    std::string Path = std::string (pPref != 0 ? pPref : "./") + pName;
    SDL_free (pPref);
    return Path;
}

// This app's release number, "" for a development build; the test mode's
// --as-release pretends one.
static std::string SelfRelease (void)
{
    const char *pTest = getenv ("OKAPIA_AS_RELEASE");
    return pTest != 0 ? pTest : OKAPIA_RELEASE;
}

static const char *Platform (void)
{
#if defined (__APPLE__) && defined (__aarch64__)
    return "macos-arm64";
#elif defined (__APPLE__)
    return "macos-x86_64";
#elif defined (__linux__) && defined (__aarch64__)
    return "linux-arm64";
#elif defined (__linux__)
    return "linux-x86_64";
#elif defined (_WIN32)
    return "windows-x86_64";
#else
    return "";
#endif
}

static std::string SystemVersion (void)
{
#if defined (__APPLE__)
    char Version[32] = "";
    size_t nSize = sizeof Version;
    if (sysctlbyname ("kern.osproductversion", Version, &nSize, 0, 0) == 0)
        return Version;
#endif
    return "";
}

bool DesktopUpdateCheckOn (void)
{
    FILE *pFile = fopen (SupportFile ("update-off").c_str (), "r");
    if (pFile != 0)
    {
        fclose (pFile);
        return false;
    }
    return true;
}

void DesktopUpdateCheckSet (bool bOn)
{
    const std::string Off = SupportFile ("update-off");
    if (bOn)
    {
        remove (Off.c_str ());
    }
    else if (FILE *pFile = fopen (Off.c_str (), "w"))
    {
        fputs ("The check for a newer Okapia is off (Okapia menu).\n", pFile);
        fclose (pFile);
    }
    std::lock_guard<std::mutex> Guard (s_Lock);
    s_bUpdateDecided = false;           // asked again at the next poll
    s_bUpdateForced = bOn;
    if (bOn)
    {
        s_bLooking = false;             // a fresh lookup
        s_Release.State = HostReleaseLooking;
    }
    HostLog (FROM, HostLogNotice, "Update check %s", bOn ? "on" : "off");
}

void DesktopUpdateCheckNow (void)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    s_bUpdateDecided = false;
    s_bUpdateForced = true;
    s_bLooking = false;
    s_Release.State = HostReleaseLooking;
    HostLog (FROM, HostLogNotice, "Update check asked for now");
}

// With s_Lock held: what the release found says, kept for a day.
static void UpdateFromRelease (void)
{
    s_Update = THostUpdate ();
    s_Update.State = HostUpdateNone;
    if (s_Release.State != HostReleaseReady)
    {
        return;                         // offline, or nothing published: nothing said, nothing kept
    }
    const TManifest &M = s_Source.Manifest;
    const std::string Self = SelfRelease ();
    const TManifestApp *pApp = ManifestApp (M, Platform ());
    const std::string System = SystemVersion ();
    const bool bNewer = !Self.empty () && VersionLess (Self, M.Version) && pApp != 0
                     && (pApp->MinOs.empty () || System.empty () || !VersionLess (System, pApp->MinOs));
    const std::string Page = !M.Page.empty () ? M.Page
                           : std::string ("https://github.com/") + OKAPIA_REPOSITORY + "/releases/latest";
    Copy (s_Update.Version, sizeof s_Update.Version, M.Version);
    Copy (s_Update.Page, sizeof s_Update.Page, Page);
    s_Update.State = bNewer ? HostUpdateAvailable : HostUpdateNone;
    // A release on this computer (the test mode's --release) is not GitHub's
    // answer: it is not kept for the next start.
    FILE *pFile = getenv ("OKAPIA_RELEASE") == 0 ? fopen (SupportFile ("update").c_str (), "w") : 0;
    if (pFile != 0)
    {
        fprintf (pFile, "checked %lld\nversion %s\npage %s\nnewer %d\n", (long long) time (0),
                 M.Version.c_str (), Page.c_str (), bNewer ? 1 : 0);
        fclose (pFile);
    }
    HostLog (FROM, HostLogNotice, "Update: this is %s, the latest Okapia %s%s", Self.empty () ? "a development build" : Self.c_str (),
             M.Version.c_str (), bNewer ? ", newer: said" : (pApp == 0 ? ", no app for this platform" : ""));
}

// The last answer, when less than a day old.
static bool UpdateFromCache (void)
{
    FILE *pFile = fopen (SupportFile ("update").c_str (), "r");
    if (pFile == 0)
    {
        return false;
    }
    long long nWhen = 0;
    char Version[32] = "", Page[192] = "";
    int nNewer = 0;
    const bool bRead = fscanf (pFile, "checked %lld\nversion %31s\npage %191s\nnewer %d", &nWhen, Version, Page,
                               &nNewer) == 4;
    fclose (pFile);
    const long long nAge = (long long) time (0) - nWhen;
    if (!bRead || nAge < 0 || nAge >= 24 * 3600)
    {
        return false;
    }
    s_Update = THostUpdate ();
    // Newer than this app then, and still: the app may have been replaced since.
    const std::string Self = SelfRelease ();
    const bool bNewer = nNewer != 0 && !Self.empty () && VersionLess (Self, Version);
    s_Update.State = bNewer ? HostUpdateAvailable : HostUpdateNone;
    Copy (s_Update.Version, sizeof s_Update.Version, Version);
    Copy (s_Update.Page, sizeof s_Update.Page, Page);
    return true;
}

void HostUpdate (THostUpdate *pOut)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    if (!s_bUpdateDecided && !s_bUpdateWaiting)
    {
        if (!DesktopUpdateCheckOn ())
        {
            s_Update = THostUpdate ();
            s_Update.State = HostUpdateOff;
            s_bUpdateDecided = true;
        }
        else if (!s_bUpdateForced && getenv ("OKAPIA_RELEASE") == 0 && UpdateFromCache ())
        {
            s_bUpdateDecided = true;
        }
        else
        {
            s_Update.State = HostUpdateChecking;
            s_bUpdateWaiting = true;
            StartLookUp ();
        }
    }
    if (s_bUpdateWaiting && s_Release.State != HostReleaseLooking)
    {
        UpdateFromRelease ();
        s_bUpdateWaiting = false;
        s_bUpdateForced = false;
        s_bUpdateDecided = true;
    }
    *pOut = s_Update;
}

static void SDLCALL OpenOnMainThread (void *pData)
{
    char *pUrl = (char *) pData;
    if (!SDL_OpenURL (pUrl))
    {
        HostLog (FROM, HostLogWarning, "The page could not be opened: %s", SDL_GetError ());
    }
    free (pUrl);
}

void HostOpenPage (const char *pUrl)
{
    // HTTPS only: the manifest is data from the network.
    if (strncmp (pUrl, "https://", 8) != 0)
    {
        HostLog (FROM, HostLogWarning, "Not opened, not an HTTPS page: %s", pUrl);
        return;
    }
    HostLog (FROM, HostLogNotice, "Opening %s", pUrl);
    char *pCopy = strdup (pUrl);
    if (pCopy != 0 && !SDL_RunOnMainThread (OpenOnMainThread, pCopy, false))
    {
        free (pCopy);
    }
}

void HostCardPlan (const char *pBoardId, THostCardPlan *pOut)
{
    memset (pOut, 0, sizeof *pOut);
    TManifest OnCard;
    if (InstallReadCard (CardPath ("/"), &OnCard))
    {
        Copy (pOut->Installed, sizeof pOut->Installed, OnCard.Version);
        Copy (pOut->Board, sizeof pOut->Board, OnCard.Board);
    }
    if (pBoardId == 0)
    {
        if (pOut->Board[0] == '\0')
        {
            return;                     // not a card Okapia was installed on
        }
        pBoardId = pOut->Board;
    }
    TManifest Board;
    {
        std::lock_guard<std::mutex> Guard (s_Lock);
        // A release in a later format, or asking a newer installer: only an
        // update of Okapia prepares this card (docs/contributing/release.md).
        pOut->bRefused = s_Release.State == HostReleaseReady && !ManifestRefusal (s_Source.Manifest).empty ();
        if (pOut->bRefused || s_Release.State != HostReleaseReady || !ManifestForBoard (s_Source.Manifest, pBoardId, &Board))
        {
            return;
        }
    }
    if (!ManifestRefusal (Board).empty ())
    {
        pOut->bRefused = true;
        return;
    }
    const TInstallPlan Plan = InstallPlan (CardPath ("/"), Board);
    Copy (pOut->Installed, sizeof pOut->Installed, Plan.Installed);
    pOut->bUpToDate = Plan.bUpToDate;
    pOut->nBytes = Plan.nBytesToWrite;
    pOut->bKnown = true;
}

static void Progress (void *pContext, const char *pName, unsigned long long nDone, unsigned long long nTotal)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    s_Prepare.State = (THostPrepareState) (uintptr_t) pContext;
    const char *pSlash = strrchr (pName, '/');
    Copy (s_Prepare.Name, sizeof s_Prepare.Name, pSlash != 0 ? pSlash + 1 : pName);
    s_Prepare.nPercent = nTotal != 0 ? (unsigned) (nDone * 100 / nTotal) : 0;
}

static void Prepare (std::string Root, std::string BoardId)
{
    TRelease R;
    TManifest Board;
    {
        std::lock_guard<std::mutex> Guard (s_Lock);
        R = s_Source;
    }
    std::string Why;
    bool bOK = ManifestForBoard (R.Manifest, BoardId, &Board);
    if (!bOK)
        Why = "no board " + BoardId + " in this release";
    if (bOK)
    {
        Why = ManifestRefusal (Board);
        bOK = Why.empty ();
    }
    char *pPref = SDL_GetPrefPath ("Okapia", "Okapia");
    const std::string Cache = std::string (pPref != 0 ? pPref : "/tmp/") + "releases";
    SDL_free (pPref);
    std::map<std::string, std::string> Files;
    if (bOK)
        bOK = ReleaseFetch (R, Board, Cache, &Files, Progress, (void *) (uintptr_t) HostPrepareFetching, &Why);
    TInstallReport Report;
    if (bOK)
        bOK = InstallWrite (Root, Board, Files, Progress, (void *) (uintptr_t) HostPrepareWriting, &Report, &Why);
    std::lock_guard<std::mutex> Guard (s_Lock);
    s_Prepare.State = bOK ? HostPrepareDone : HostPrepareFailed;
    s_Prepare.nPercent = bOK ? 100 : s_Prepare.nPercent;
    Copy (s_Prepare.Why, sizeof s_Prepare.Why, Why);
    s_bPreparing = false;
    if (bOK)
        HostLog (FROM, HostLogNotice, "Prepared %s for %s: Okapia %s, %zu written, %zu kept as .bak, %zu removed",
                 Root.c_str (), BoardId.c_str (), Board.Version.c_str (), Report.Written.size (),
                 Report.BackedUp.size (), Report.Removed.size ());
    else
        HostLog (FROM, HostLogWarning, "Preparing %s failed: %s", Root.c_str (), Why.c_str ());
}

bool HostPrepareStart (const char *pBoardId)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    if (s_bPreparing || s_Release.State != HostReleaseReady)
        return false;
    s_bPreparing = true;
    s_Prepare = THostPrepare ();
    s_Prepare.State = HostPrepareFetching;
    std::thread (Prepare, std::string (CardPath ("/")), std::string (pBoardId)).detach ();
    return true;
}

void HostPrepareStatus (THostPrepare *pOut)
{
    std::lock_guard<std::mutex> Guard (s_Lock);
    *pOut = s_Prepare;
}
