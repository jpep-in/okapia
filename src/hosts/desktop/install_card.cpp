//
// install_card.cpp — what installing would change on a card, and doing it.
//
// The installer's install.rs: the plan, the verified copy, the backups of
// edited files, the removal of what the release dropped, the manifest last.
// Files are copied by reading and writing, never with the system's copy, so
// no extended attribute is carried over; what macOS still adds (its provenance,
// kept in a ._ file on FAT) is removed after each file.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static std::string Join (const std::string &Root, const std::string &Name)
{
    return Root + "/" + Name;
}

static bool IsFile (const std::string &Path)
{
    struct stat Info;
    return stat (Path.c_str (), &Info) == 0 && S_ISREG (Info.st_mode);
}

static bool ReadText (const std::string &Path, std::string *pOut)
{
    FILE *pFile = fopen (Path.c_str (), "rb");
    if (pFile == 0)
        return false;
    char Buffer[16384];
    size_t n;
    pOut->clear ();
    while ((n = fread (Buffer, 1, sizeof Buffer, pFile)) > 0)
        pOut->append (Buffer, n);
    fclose (pFile);
    return true;
}

// Every directory above a card path, made: firmware/ for the Wi-Fi firmware.
static bool MakeParents (const std::string &Path)
{
    for (size_t n = Path.find ('/', 1); n != std::string::npos; n = Path.find ('/', n + 1))
    {
        const std::string Dir = Path.substr (0, n);
        if (mkdir (Dir.c_str (), 0755) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

bool InstallReadCard (const std::string &Root, TManifest *pOut)
{
    std::string Text, Why;
    return ReadText (Join (Root, MANIFEST_FILE), &Text) && ManifestParse (Text, pOut, &Why);
}

TInstallPlan InstallPlan (const std::string &Root, const TManifest &Release)
{
    TInstallPlan P;
    TManifest Previous;
    const bool bPrevious = InstallReadCard (Root, &Previous);
    if (bPrevious)
        P.Installed = Previous.Version;
    auto PreviousHash = [&] (const std::string &Name) -> std::string
    {
        if (bPrevious)
            for (const TManifestEntry &E : Previous.Files)
                if (E.Name == Name)
                    return E.Sha256;
        return "";
    };
    for (const TManifestEntry &E : Release.Files)
    {
        TInstallItem Item;
        Item.Entry = E;
        const std::string Path = Join (Root, E.Name);
        std::string Hash;
        if (!IsFile (Path))
            Item.State = InstallMissing;
        else if (!Sha256File (Path, &Hash))
            Item.State = InstallDiffers;
        else if (strcasecmp (Hash.c_str (), E.Sha256.c_str ()) == 0)
            Item.State = InstallSame;
        else
        {
            // Changed by hand: different from this release and from the one
            // the card's manifest says was installed.
            const std::string Before = PreviousHash (E.Name);
            Item.State = !Before.empty () && strcasecmp (Before.c_str (), Hash.c_str ()) != 0
                       ? InstallEdited : InstallDiffers;
        }
        if (Item.State != InstallSame)
            P.nBytesToWrite += E.nSize;
        P.Items.push_back (Item);
    }
    if (bPrevious)
    {
        for (const TManifestEntry &Old : Previous.Files)
        {
            bool bKept = false;
            for (const TManifestEntry &E : Release.Files)
                bKept = bKept || E.Name == Old.Name;
            if (!bKept && IsFile (Join (Root, Old.Name)))
                P.Obsolete.push_back (Old.Name);
        }
    }
    bool bSame = bPrevious && Previous.Version == Release.Version && P.Obsolete.empty ();
    for (const TInstallItem &I : P.Items)
        bSame = bSame && I.State == InstallSame;
    P.bUpToDate = bSame;
    return P;
}

void DropAppleDouble (const std::string &Path)
{
    for (int nLevel = 0; nLevel < 2; nLevel++)
    {
        std::string Here = Path;
        for (int i = 0; i < nLevel; i++)
        {
            const size_t n = Here.rfind ('/');
            if (n == std::string::npos || n == 0)
                return;
            Here = Here.substr (0, n);
        }
        const size_t n = Here.rfind ('/');
        if (n == std::string::npos)
            return;
        const std::string Double = Here.substr (0, n + 1) + "._" + Here.substr (n + 1);
        FILE *pFile = fopen (Double.c_str (), "rb");
        if (pFile == 0)
            continue;
        unsigned char Magic[4] = { 0 };
        const bool bDouble = fread (Magic, 1, 4, pFile) == 4
                          && Magic[0] == 0x00 && Magic[1] == 0x05 && Magic[2] == 0x16 && Magic[3] == 0x07;
        fclose (pFile);
        if (bDouble)
            unlink (Double.c_str ());
    }
}

bool CopyVerified (const std::string &From, const std::string &To, const char *pSha256,
                   TInstallProgress *pProgress, void *pContext, std::string *pError)
{
    const std::string Temp = To + ".okapia-new";
    if (!MakeParents (To))
    {
        *pError = To + ": " + strerror (errno);
        return false;
    }
    FILE *pIn = fopen (From.c_str (), "rb");
    if (pIn == 0)
    {
        *pError = From + ": " + strerror (errno);
        return false;
    }
    fseek (pIn, 0, SEEK_END);
    const unsigned long long nTotal = (unsigned long long) ftell (pIn);
    fseek (pIn, 0, SEEK_SET);
    FILE *pOut = fopen (Temp.c_str (), "wb");
    if (pOut == 0)
    {
        *pError = Temp + ": " + strerror (errno);
        fclose (pIn);
        return false;
    }
    static unsigned char Buffer[1 << 20];
    unsigned long long nDone = 0;
    size_t n;
    bool bOK = true;
    while (bOK && (n = fread (Buffer, 1, sizeof Buffer, pIn)) > 0)
    {
        bOK = fwrite (Buffer, 1, n, pOut) == n;
        nDone += n;
        if (pProgress != 0)
            pProgress (pContext, To.c_str (), nDone, nTotal);
    }
    bOK = bOK && !ferror (pIn) && fflush (pOut) == 0 && fsync (fileno (pOut)) == 0;
    fclose (pIn);
    if (fclose (pOut) != 0)
        bOK = false;
    if (!bOK)
    {
        *pError = To + ": " + strerror (errno);
        unlink (Temp.c_str ());
        return false;
    }
    std::string Expected, Got;
    if (pSha256 != 0)
        Expected = pSha256;
    else if (!Sha256File (From, &Expected))
    {
        *pError = From + ": cannot be read again";
        unlink (Temp.c_str ());
        return false;
    }
    if (!Sha256File (Temp, &Got) || strcasecmp (Got.c_str (), Expected.c_str ()) != 0)
    {
        *pError = To + ": read back " + Got + ", expected " + Expected + " — the card may be failing";
        unlink (Temp.c_str ());
        return false;
    }
    if (rename (Temp.c_str (), To.c_str ()) != 0)
    {
        *pError = To + ": " + strerror (errno);
        unlink (Temp.c_str ());
        return false;
    }
    DropAppleDouble (Temp);
    DropAppleDouble (To);
    return true;
}

bool WriteAtomically (const std::string &To, const std::string &Bytes, std::string *pError)
{
    const std::string Temp = To + ".okapia-new";
    FILE *pOut = fopen (Temp.c_str (), "wb");
    bool bOK = pOut != 0 && fwrite (Bytes.data (), 1, Bytes.size (), pOut) == Bytes.size ()
            && fflush (pOut) == 0 && fsync (fileno (pOut)) == 0;
    if (pOut != 0 && fclose (pOut) != 0)
        bOK = false;
    if (!bOK || rename (Temp.c_str (), To.c_str ()) != 0)
    {
        *pError = To + ": " + strerror (errno);
        unlink (Temp.c_str ());
        return false;
    }
    DropAppleDouble (Temp);
    DropAppleDouble (To);
    return true;
}

bool InstallWrite (const std::string &Root, const TManifest &Release,
                   const std::map<std::string, std::string> &Files,
                   TInstallProgress *pProgress, void *pContext,
                   TInstallReport *pReport, std::string *pError)
{
    const std::string Refusal = ManifestRefusal (Release);
    if (!Refusal.empty ())
    {
        *pError = Refusal;
        return false;
    }
    const TInstallPlan Plan = InstallPlan (Root, Release);
    TManifest Previous;
    const bool bPrevious = InstallReadCard (Root, &Previous);

    // Every file this computer needs is there before the card is touched.
    for (const TInstallItem &I : Plan.Items)
    {
        if (I.State != InstallSame && Files.find (I.Entry.Name) == Files.end ())
        {
            *pError = "nowhere to get " + I.Entry.Name;
            return false;
        }
    }

    for (const TInstallItem &I : Plan.Items)
    {
        if (I.State == InstallSame)
            continue;
        const std::string Target = Join (Root, I.Entry.Name);
        if (I.State == InstallEdited)
        {
            if (!CopyVerified (Target, Target + ".bak", 0, 0, 0, pError))
                return false;
            pReport->BackedUp.push_back (I.Entry.Name + ".bak");
        }
        if (!CopyVerified (Files.at (I.Entry.Name), Target, I.Entry.Sha256.c_str (), pProgress, pContext,
                           pError))
            return false;
        pReport->Written.push_back (I.Entry.Name);
    }

    // What the last release owned and this one dropped, when untouched.
    for (const std::string &Name : Plan.Obsolete)
    {
        std::string Hash;
        bool bUntouched = false;
        if (bPrevious && Sha256File (Join (Root, Name), &Hash))
            for (const TManifestEntry &E : Previous.Files)
                bUntouched = bUntouched || (E.Name == Name && strcasecmp (E.Sha256.c_str (), Hash.c_str ()) == 0);
        if (bUntouched && SafeCardName (Name) && unlink (Join (Root, Name).c_str ()) == 0)
            pReport->Removed.push_back (Name);
        else
            pReport->KeptEdited.push_back (Name);
    }

    // The record, last.
    if (!WriteAtomically (Join (Root, MANIFEST_FILE), ManifestWrite (Release), pError))
        return false;
    sync ();
    return true;
}
