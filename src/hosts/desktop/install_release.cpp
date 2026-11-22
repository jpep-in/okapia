//
// install_release.cpp — where a release comes from, and its zips.
//
// The installer's release.rs and fetch.rs without the network: a release on
// this computer (a folder with its manifest and zips, that manifest itself,
// one board's files unzipped, or one board's zip), the zips read with miniz
// (external/miniz, MIT), and a board's files brought to this computer and
// checked against the manifest before anything touches a card. The network's
// half is install_fetch.cpp.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "miniz.h"

static bool IsDir (const std::string &Path)
{
    struct stat Info;
    return stat (Path.c_str (), &Info) == 0 && S_ISDIR (Info.st_mode);
}

static bool IsFile (const std::string &Path)
{
    struct stat Info;
    return stat (Path.c_str (), &Info) == 0 && S_ISREG (Info.st_mode);
}

static bool EndsWith (const std::string &s, const char *pTail)
{
    const size_t n = strlen (pTail);
    return s.size () >= n && strcasecmp (s.c_str () + s.size () - n, pTail) == 0;
}

static bool ReadFile (const std::string &Path, std::string *pOut)
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

/*
 *  Zips
 */
bool ZipReadText (const std::string &Zip, const std::string &Name, std::string *pOut, std::string *pError)
{
    mz_zip_archive Archive;
    memset (&Archive, 0, sizeof Archive);
    if (!mz_zip_reader_init_file (&Archive, Zip.c_str (), 0))
    {
        *pError = Zip + ": not a zip";
        return false;
    }
    size_t nSize = 0;
    void *pData = mz_zip_reader_extract_file_to_heap (&Archive, Name.c_str (), &nSize, 0);
    mz_zip_reader_end (&Archive);
    if (pData == 0)
    {
        *pError = Zip + ": no " + Name + " in it";
        return false;
    }
    pOut->assign ((const char *) pData, nSize);
    mz_free (pData);
    return true;
}

namespace
{
struct TWriteTo
{
    FILE              *pFile;
    TInstallProgress  *pProgress;
    void              *pContext;
    const char        *pName;
    unsigned long long nTotal;
};

size_t WriteCallback (void *pOpaque, mz_uint64 nOffset, const void *pBuffer, size_t n)
{
    TWriteTo *pTo = (TWriteTo *) pOpaque;
    if (fwrite (pBuffer, 1, n, pTo->pFile) != n)
        return 0;
    if (pTo->pProgress != 0)
        pTo->pProgress (pTo->pContext, pTo->pName, nOffset + n, pTo->nTotal);
    return n;
}
}

bool ZipExtract (const std::string &Zip, const std::vector<TManifestEntry> &Entries, const std::string &Dir,
                 std::map<std::string, std::string> *pFiles, TInstallProgress *pProgress, void *pContext,
                 std::string *pError)
{
    mz_zip_archive Archive;
    memset (&Archive, 0, sizeof Archive);
    if (!mz_zip_reader_init_file (&Archive, Zip.c_str (), 0))
    {
        *pError = Zip + ": not a zip";
        return false;
    }
    bool bOK = true;
    for (const TManifestEntry &E : Entries)
    {
        if (!SafeCardName (E.Name))
        {
            *pError = "unsafe name " + E.Name;
            bOK = false;
            break;
        }
        const int nIndex = mz_zip_reader_locate_file (&Archive, E.Name.c_str (), 0, 0);
        if (nIndex < 0)
        {
            *pError = Zip + ": " + E.Name + " is named by the manifest but not in the zip";
            bOK = false;
            break;
        }
        const std::string Path = Dir + "/" + E.Name;
        if (!MakeParents (Path))
        {
            *pError = Path + ": " + strerror (errno);
            bOK = false;
            break;
        }
        TWriteTo To = { fopen (Path.c_str (), "wb"), pProgress, pContext, E.Name.c_str (), E.nSize };
        if (To.pFile == 0)
        {
            *pError = Path + ": " + strerror (errno);
            bOK = false;
            break;
        }
        const bool bExtracted = mz_zip_reader_extract_to_callback (&Archive, (mz_uint) nIndex, WriteCallback,
                                                                   &To, 0) != 0;
        if (fclose (To.pFile) != 0 || !bExtracted)
        {
            *pError = Zip + ": " + E.Name + " could not be unzipped";
            bOK = false;
            break;
        }
        std::string Hash;
        if (!Sha256File (Path, &Hash) || strcasecmp (Hash.c_str (), E.Sha256.c_str ()) != 0)
        {
            *pError = E.Name + ": not what the manifest says (SHA-256)";
            unlink (Path.c_str ());
            bOK = false;
            break;
        }
        (*pFiles)[E.Name] = Path;
    }
    mz_zip_reader_end (&Archive);
    return bOK;
}

/*
 *  Releases on this computer
 */
bool ReleaseFromLocal (const std::string &Path, TRelease *pOut, std::string *pError)
{
    *pOut = TRelease ();
    std::string Text;
    if (IsDir (Path) || (IsFile (Path) && EndsWith (Path, MANIFEST_FILE)))
    {
        const std::string Dir = IsDir (Path) ? Path : Path.substr (0, Path.rfind ('/'));
        if (!ReadFile (Dir + "/" + MANIFEST_FILE, &Text))
        {
            *pError = Dir + ": no " + MANIFEST_FILE;
            return false;
        }
        pOut->Dir = Dir;
        pOut->Description = Dir;
    }
    else if (IsFile (Path) && EndsWith (Path, ".zip"))
    {
        // One board's zip: its manifest is inside, resolved to that board.
        if (!ZipReadText (Path, MANIFEST_FILE, &Text, pError))
            return false;
        pOut->Zip = Path;
        pOut->Description = Path;
    }
    else
    {
        *pError = Path + ": not a release (a folder, its manifest, or a zip)";
        return false;
    }
    return ManifestParse (Text, &pOut->Manifest, pError);
}

bool ReleaseFetch (const TRelease &R, const TManifest &Board, const std::string &Cache,
                   std::map<std::string, std::string> *pFiles, TInstallProgress *pProgress, void *pContext,
                   std::string *pError)
{
    pFiles->clear ();
    std::string Zip;
    if (!R.Zip.empty ())
        Zip = R.Zip;
    else if (!R.Dir.empty () && !Board.Archive.empty () && IsFile (R.Dir + "/" + Board.Archive))
        Zip = R.Dir + "/" + Board.Archive;
    else if (!R.Dir.empty ())
    {
        // A release unzipped: the files themselves, checked when they are copied.
        for (const TManifestEntry &E : Board.Files)
        {
            const std::string Path = R.Dir + "/" + E.Name;
            if (!IsFile (Path))
            {
                *pError = E.Name + " is named by the manifest but missing from the folder";
                return false;
            }
            (*pFiles)[E.Name] = Path;
        }
        return true;
    }
    else
    {
        // A GitHub release: its board's zip, downloaded once into the cache.
        const auto Asset = R.Assets.find (Board.Archive);
        if (Board.Archive.empty () || Asset == R.Assets.end ())
        {
            *pError = R.Description + " lacks the asset " + Board.Archive;
            return false;
        }
        if (!MakeParents (Cache + "/x"))
        {
            *pError = Cache + ": " + strerror (errno);
            return false;
        }
        Zip = Cache + "/" + Board.Archive;
        const std::string Partial = Zip + ".part";
        if (!IsFile (Zip))
        {
            if (!HttpDownload (Asset->second, Partial, pProgress, pContext, pError))
                return false;
            if (rename (Partial.c_str (), Zip.c_str ()) != 0)
            {
                *pError = Zip + ": " + strerror (errno);
                return false;
            }
        }
    }
    const std::string Into = Cache + "/" + (Board.Version.empty () ? std::string ("files") : Board.Version)
                           + "-" + Board.Board;
    if (!ZipExtract (Zip, Board.Files, Into, pFiles, pProgress, pContext, pError))
    {
        // A cached zip that does not hold what it should is fetched again next time.
        if (!R.Assets.empty ())
            unlink (Zip.c_str ());
        return false;
    }
    return true;
}
