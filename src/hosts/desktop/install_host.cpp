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
#include "okapia_host.h"

#include <SDL3/SDL.h>

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

void HostRelease (THostRelease *pOut)
{
    {
        std::lock_guard<std::mutex> Guard (s_Lock);
        if (!s_bLooking)
        {
            s_bLooking = true;
            std::thread (LookUp).detach ();
        }
        *pOut = s_Release;
    }
}

void HostCardPlan (const char *pBoardId, THostCardPlan *pOut)
{
    memset (pOut, 0, sizeof *pOut);
    TManifest Board;
    {
        std::lock_guard<std::mutex> Guard (s_Lock);
        // A release in a later format, or asking a newer installer: only an
        // update of Okapia prepares this card (docs/contributing/release.md).
        pOut->bRefused = s_Release.State == HostReleaseReady && !ManifestRefusal (s_Source.Manifest).empty ();
        if (pOut->bRefused || s_Release.State != HostReleaseReady || !ManifestForBoard (s_Source.Manifest, pBoardId, &Board))
        {
            TManifest OnCard;
            if (InstallReadCard (CardPath ("/"), &OnCard))
                Copy (pOut->Installed, sizeof pOut->Installed, OnCard.Version);
            return;
        }
    }
    if (!ManifestRefusal (Board).empty ())
    {
        pOut->bRefused = true;
        TManifest OnCard;
        if (InstallReadCard (CardPath ("/"), &OnCard))
            Copy (pOut->Installed, sizeof pOut->Installed, OnCard.Version);
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
