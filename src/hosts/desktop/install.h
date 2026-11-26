//
// install.h — putting a release of Okapia on an SD card, from the welcome page.
//
// The installer's core (okapia-installer core/src: manifest.rs, fetch.rs,
// install.rs), in C++ on the desktop host. Desktop only: a board does not
// install itself. Standard C++ and POSIX files, no SDL, so tests/host runs it.
//
// The rules, the installer's:
//
// - Everything that changes with Okapia arrives as data, in each release's
//   okapia-manifest.json: the boards it is built for, each with its zip and its
//   files (name on the card, size, SHA-256). A board a release adds appears in
//   the welcome page's For menu without a new Okapia.
// - Only the files the manifest names are written. Disk images, ROMs, the
//   preferences, the PRAM, the logs and shared/ are never touched.
// - Everything is fetched and verified before the card is written.
// - Each file goes next to its place as <name>.okapia-new, flushed, read back
//   and checked, then renamed: a card pulled out mid-way holds the old file or
//   the new one, never half of one under the real name.
// - A file of Okapia's edited by hand since the last install (config.txt) is
//   kept as <name>.bak first.
// - The card's copy of the manifest, resolved to its board, goes last: it is
//   the record that the install finished.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_install_h
#define _okapia_install_h

#include <map>
#include <string>
#include <vector>

/*
 *  JSON, as much as a manifest needs
 */
struct TJson
{
    enum TType { Null, Bool, Number, String, Array, Object } Type = Null;
    bool        b = false;
    double      f = 0;
    std::string s;
    std::vector<TJson> Items;                          // Array
    std::vector<std::pair<std::string, TJson>> Members; // Object, in order

    const TJson *Get (const char *pKey) const;         // 0 when absent
    std::string  Text (const char *pKey, const char *pDefault = "") const;
    double       Num (const char *pKey, double fDefault = 0) const;
};

// False, and why in *pError, when the text is not JSON.
bool JsonParse (const std::string &Text, TJson *pOut, std::string *pError);
// A string quoted for JSON.
std::string JsonQuote (const std::string &s);

/*
 *  SHA-256
 */
std::string Sha256Hex (const void *pData, size_t nBytes);
// False when the file cannot be read.
bool Sha256File (const std::string &Path, std::string *pHex);

/*
 *  The manifest
 */
// The format this Okapia reads: okapia-installer's, with the desktop builds
// (apps), the build identifier and the release page; never published before
// them, so they belong to schema 1 (docs/contributing/release.md).
static const unsigned MANIFEST_SCHEMA = 1;
static const char MANIFEST_FILE[] = "okapia-manifest.json";
// The installer protocol this Okapia speaks: a release asking a newer one in
// installer_min is refused with one message.
static const char INSTALLER_VERSION[] = "1.0.0";

struct TManifestEntry
{
    std::string        Name;            // path on the card, '/' separated
    unsigned long long nSize = 0;
    std::string        Sha256;
    std::string        Component;       // "Okapia", "Raspberry Pi firmware"
};

struct TManifestBoard
{
    std::string Id;                     // "pi4"
    std::string Name;                   // "Raspberry Pi 4", the same in every language
    std::string Archive;                // the zip holding its files
    std::string InstallerMin;
    std::vector<TManifestEntry> Files;
};

// The desktop app of a release, for one platform ("macos-arm64").
struct TManifestApp
{
    std::string        Platform;
    std::string        Archive;
    unsigned long long nSize = 0;
    std::string        Sha256;
    std::string        MinOs;           // "14.0": not offered to an older system
    std::string        Team;            // the Developer ID's Team ID the app must carry
};

struct TManifest
{
    unsigned    nSchema = 0;
    // A later format than this Okapia reads: only the frozen core was read
    // (Version, Build, Page, Apps); no card is prepared from it, the app can
    // still update itself to the release that understands it.
    bool        bNewerSchema = false;
    std::string InstallerMin;
    std::string Version;                // okapia.version: the release number, 26.11.1
    std::string Build;                  // okapia.build: 261121-8b8bf89
    std::string Page;                   // okapia.page: the release's web page
    std::string Board, BoardName;       // a resolved manifest's board
    std::string Notes;
    std::vector<TManifestBoard> Boards; // the release's; empty when resolved
    std::vector<TManifestEntry> Files;  // resolved: what goes on the card
    std::string Archive;                // resolved: the zip of those files
    std::vector<TManifestApp> Apps;     // the release's desktop builds
};

// The app for this platform in a release, or 0.
const TManifestApp *ManifestApp (const TManifest &M, const std::string &Platform);

// False, and why, when the text is not a manifest: not JSON, no version, or a
// file name that would leave the card. A later schema is read, its frozen core
// only (bNewerSchema).
bool ManifestParse (const std::string &Text, TManifest *pOut, std::string *pError);
// The release resolved to one board; false when it has no such board.
bool ManifestForBoard (const TManifest &M, const std::string &Id, TManifest *pOut);
// The boards to offer, id and name: the release's, or the one it is resolved to.
std::vector<std::pair<std::string, std::string>> ManifestBoards (const TManifest &M);
// Why this Okapia may not install it ("" when it may).
std::string ManifestRefusal (const TManifest &M);
// The card's copy: resolved, written last.
std::string ManifestWrite (const TManifest &M);

bool VersionLess (const std::string &A, const std::string &B);   // "0.9.1" < "0.10.0"
bool SafeCardName (const std::string &Name);   // relative, no "..", no '\\', no leading '.'
bool SafeComponent (const std::string &Name);  // one name: [A-Za-z0-9._+-], no leading '.'

/*
 *  What installing would do, and doing it
 */
enum TInstallState
{
    InstallMissing,
    InstallDiffers,                     // Okapia's file, older
    InstallEdited,                      // differs from both releases: changed by hand
    InstallSame
};

struct TInstallItem
{
    TManifestEntry Entry;
    TInstallState  State = InstallMissing;
};

struct TInstallPlan
{
    std::vector<TInstallItem> Items;
    std::vector<std::string>  Obsolete;  // the last release's, gone from this one
    unsigned long long nBytesToWrite = 0;
    bool               bUpToDate = false;
    std::string        Installed;        // the version on the card, "" when none
};

// The card's manifest, when Okapia was installed there.
bool InstallReadCard (const std::string &Root, TManifest *pOut);
// Compares a resolved release with what the card holds.
TInstallPlan InstallPlan (const std::string &Root, const TManifest &Release);

typedef void TInstallProgress (void *pContext, const char *pName, unsigned long long nDone,
                               unsigned long long nTotal);

struct TInstallReport
{
    std::vector<std::string> Written, BackedUp, Removed, KeptEdited;
};

// Writes a resolved release onto the card. Files: for each entry's name, the
// local file already fetched and verified. False and why on the first
// failure; what was renamed into place before it stays, the rest does not
// exist, and the card's manifest is the previous one.
bool InstallWrite (const std::string &Root, const TManifest &Release,
                   const std::map<std::string, std::string> &Files,
                   TInstallProgress *pProgress, void *pContext,
                   TInstallReport *pReport, std::string *pError);

// Copy through <to>.okapia-new, flush, read back against the hash (the
// source's when 0), rename. Removes the ._ AppleDouble macOS leaves on FAT.
bool CopyVerified (const std::string &From, const std::string &To, const char *pSha256,
                   TInstallProgress *pProgress, void *pContext, std::string *pError);
bool WriteAtomically (const std::string &To, const std::string &Bytes, std::string *pError);
// The "._name" beside a path, removed only when it starts with the AppleDouble
// magic; its parent's too, since creating firmware/ leaves ._firmware.
void DropAppleDouble (const std::string &Path);

/*
 *  Zips: a board's files as a release carries them (install_release.cpp, miniz)
 */
// One member's bytes: the manifest a board's zip carries.
bool ZipReadText (const std::string &Zip, const std::string &Name, std::string *pOut, std::string *pError);
// The members the manifest names, written into Dir under their card names and
// each checked against its SHA-256. *pFiles: card name → local path.
bool ZipExtract (const std::string &Zip, const std::vector<TManifestEntry> &Entries, const std::string &Dir,
                 std::map<std::string, std::string> *pFiles, TInstallProgress *pProgress, void *pContext,
                 std::string *pError);

/*
 *  Where a release comes from (the installer's release.rs)
 */
// The repository releases are read from: the one address built in.
static const char OKAPIA_REPOSITORY[] = "jpep-in/okapia";

struct TRelease
{
    TManifest   Manifest;               // as published: every board, every app
    std::string Description;            // "jpep-in/okapia v26.11.1", a folder's path
    std::string Dir;                    // a release folder on this computer
    std::string Zip;                    // one board's zip, chosen as it is
    std::map<std::string, std::string> Assets;  // a GitHub release: asset name → URL
};

// A folder holding okapia-manifest.json (and its zips, or one board's files
// unzipped), that manifest itself, or one board's zip.
bool ReleaseFromLocal (const std::string &Path, TRelease *pOut, std::string *pError);
// The latest release of a GitHub repository: the network (install_fetch.cpp).
bool ReleaseFromGitHub (const std::string &Repository, TRelease *pOut, std::string *pError);
// One board's files on this computer, verified: from the release's zip
// (downloaded into Cache first when it is remote), or a folder's own files.
bool ReleaseFetch (const TRelease &R, const TManifest &Board, const std::string &Cache,
                   std::map<std::string, std::string> *pFiles, TInstallProgress *pProgress, void *pContext,
                   std::string *pError);

/*
 *  The network (install_fetch.cpp, libcurl): HTTPS, a generic User-Agent,
 *  nothing about the person or the machine
 */
bool HttpGet (const std::string &Url, std::string *pBody, std::string *pError);
bool HttpDownload (const std::string &Url, const std::string &Path, TInstallProgress *pProgress,
                   void *pContext, std::string *pError);

#endif
