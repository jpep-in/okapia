//
// install_manifest.cpp — JSON, SHA-256 and okapia-manifest.json.
//
// The installer's manifest.rs and the parts of fetch.rs that need no network.
// The JSON reader is the subset a manifest uses, strict: objects, arrays,
// strings with their escapes, numbers, true, false, null.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  JSON
 */
const TJson *TJson::Get (const char *pKey) const
{
    for (const auto &M : Members)
        if (M.first == pKey)
            return &M.second;
    return 0;
}

std::string TJson::Text (const char *pKey, const char *pDefault) const
{
    const TJson *p = Get (pKey);
    return p != 0 && p->Type == String ? p->s : std::string (pDefault);
}

double TJson::Num (const char *pKey, double fDefault) const
{
    const TJson *p = Get (pKey);
    return p != 0 && p->Type == Number ? p->f : fDefault;
}

namespace
{
struct TReader
{
    const char *p, *pEnd;
    std::string Error;

    void Space ()
    {
        while (p < pEnd && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
    }
    bool Fail (const char *pWhy)
    {
        if (Error.empty ())
            Error = pWhy;
        return false;
    }
    static void Utf8 (std::string *pOut, unsigned c)
    {
        if (c < 0x80)
            *pOut += (char) c;
        else if (c < 0x800)
        {
            *pOut += (char) (0xC0 | c >> 6);
            *pOut += (char) (0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            *pOut += (char) (0xE0 | c >> 12);
            *pOut += (char) (0x80 | (c >> 6 & 0x3F));
            *pOut += (char) (0x80 | (c & 0x3F));
        }
        else
        {
            *pOut += (char) (0xF0 | c >> 18);
            *pOut += (char) (0x80 | (c >> 12 & 0x3F));
            *pOut += (char) (0x80 | (c >> 6 & 0x3F));
            *pOut += (char) (0x80 | (c & 0x3F));
        }
    }
    bool Hex4 (unsigned *pOut)
    {
        if (pEnd - p < 4)
            return Fail ("a \\u escape cut short");
        unsigned v = 0;
        for (int i = 0; i < 4; i++)
        {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned) (c - 'A' + 10);
            else return Fail ("a \\u escape that is not hexadecimal");
        }
        *pOut = v;
        return true;
    }
    bool Str (std::string *pOut)
    {
        p++;                            // the opening quote
        while (p < pEnd && *p != '"')
        {
            if ((unsigned char) *p < 0x20)
                return Fail ("a control character in a string");
            if (*p != '\\')
            {
                *pOut += *p++;
                continue;
            }
            if (++p >= pEnd)
                return Fail ("a string cut short");
            const char c = *p++;
            switch (c)
            {
            case '"': case '\\': case '/': *pOut += c; break;
            case 'b': *pOut += '\b'; break;
            case 'f': *pOut += '\f'; break;
            case 'n': *pOut += '\n'; break;
            case 'r': *pOut += '\r'; break;
            case 't': *pOut += '\t'; break;
            case 'u':
            {
                unsigned u;
                if (!Hex4 (&u))
                    return false;
                if (u >= 0xD800 && u < 0xDC00 && pEnd - p >= 6 && p[0] == '\\' && p[1] == 'u')
                {
                    p += 2;
                    unsigned v;
                    if (!Hex4 (&v))
                        return false;
                    u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
                }
                Utf8 (pOut, u);
                break;
            }
            default:
                return Fail ("an unknown escape in a string");
            }
        }
        if (p >= pEnd)
            return Fail ("a string cut short");
        p++;
        return true;
    }
    bool Value (TJson *pOut, int nDepth)
    {
        if (nDepth > 64)
            return Fail ("nested too deep");
        Space ();
        if (p >= pEnd)
            return Fail ("the text ends where a value was due");
        const char c = *p;
        if (c == '{')
        {
            pOut->Type = TJson::Object;
            p++;
            Space ();
            if (p < pEnd && *p == '}')
            {
                p++;
                return true;
            }
            for (;;)
            {
                Space ();
                if (p >= pEnd || *p != '"')
                    return Fail ("an object key that is not a string");
                std::string Key;
                if (!Str (&Key))
                    return false;
                Space ();
                if (p >= pEnd || *p != ':')
                    return Fail ("a key without a colon");
                p++;
                pOut->Members.emplace_back (Key, TJson ());
                if (!Value (&pOut->Members.back ().second, nDepth + 1))
                    return false;
                Space ();
                if (p < pEnd && *p == ',') { p++; continue; }
                if (p < pEnd && *p == '}') { p++; return true; }
                return Fail ("an object not closed");
            }
        }
        if (c == '[')
        {
            pOut->Type = TJson::Array;
            p++;
            Space ();
            if (p < pEnd && *p == ']')
            {
                p++;
                return true;
            }
            for (;;)
            {
                pOut->Items.emplace_back ();
                if (!Value (&pOut->Items.back (), nDepth + 1))
                    return false;
                Space ();
                if (p < pEnd && *p == ',') { p++; continue; }
                if (p < pEnd && *p == ']') { p++; return true; }
                return Fail ("an array not closed");
            }
        }
        if (c == '"')
        {
            pOut->Type = TJson::String;
            return Str (&pOut->s);
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            char *pStop = 0;
            const std::string Number (p, (size_t) (pEnd - p < 64 ? pEnd - p : 64));
            pOut->f = strtod (Number.c_str (), &pStop);
            if (pStop == Number.c_str ())
                return Fail ("a number that is not one");
            p += pStop - Number.c_str ();
            pOut->Type = TJson::Number;
            return true;
        }
        static const struct { const char *pWord; TJson::TType Type; bool b; } Words[] =
            { { "true", TJson::Bool, true }, { "false", TJson::Bool, false }, { "null", TJson::Null, false } };
        for (const auto &W : Words)
        {
            const size_t n = strlen (W.pWord);
            if ((size_t) (pEnd - p) >= n && strncmp (p, W.pWord, n) == 0)
            {
                p += n;
                pOut->Type = W.Type;
                pOut->b = W.b;
                return true;
            }
        }
        return Fail ("a value that is not JSON");
    }
};
}

bool JsonParse (const std::string &Text, TJson *pOut, std::string *pError)
{
    TReader R { Text.data (), Text.data () + Text.size (), std::string () };
    *pOut = TJson ();
    bool bOK = R.Value (pOut, 0);
    R.Space ();
    if (bOK && R.p != R.pEnd)
        bOK = R.Fail ("something after the value");
    if (!bOK && pError != 0)
        *pError = R.Error;
    return bOK;
}

std::string JsonQuote (const std::string &s)
{
    std::string Out = "\"";
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  Out += "\\\""; break;
        case '\\': Out += "\\\\"; break;
        case '\n': Out += "\\n"; break;
        case '\r': Out += "\\r"; break;
        case '\t': Out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char Esc[8];
                snprintf (Esc, sizeof Esc, "\\u%04x", c);
                Out += Esc;
            }
            else
                Out += (char) c;
        }
    }
    return Out + "\"";
}

/*
 *  SHA-256 (FIPS 180-4)
 */
namespace
{
struct TSha256
{
    uint32_t H[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char Block[64];
    unsigned nBlock = 0;
    uint64_t nBits = 0;

    static uint32_t Ror (uint32_t x, int n) { return x >> n | x << (32 - n); }

    void Compress (const unsigned char *b)
    {
        static const uint32_t K[64] =
        {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
        uint32_t W[64];
        for (int i = 0; i < 16; i++)
            W[i] = (uint32_t) b[4 * i] << 24 | (uint32_t) b[4 * i + 1] << 16
                 | (uint32_t) b[4 * i + 2] << 8 | b[4 * i + 3];
        for (int i = 16; i < 64; i++)
        {
            const uint32_t s0 = Ror (W[i - 15], 7) ^ Ror (W[i - 15], 18) ^ (W[i - 15] >> 3);
            const uint32_t s1 = Ror (W[i - 2], 17) ^ Ror (W[i - 2], 19) ^ (W[i - 2] >> 10);
            W[i] = W[i - 16] + s0 + W[i - 7] + s1;
        }
        uint32_t a = H[0], bb = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
        for (int i = 0; i < 64; i++)
        {
            const uint32_t S1 = Ror (e, 6) ^ Ror (e, 11) ^ Ror (e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + S1 + ch + K[i] + W[i];
            const uint32_t S0 = Ror (a, 2) ^ Ror (a, 13) ^ Ror (a, 22);
            const uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        H[0] += a; H[1] += bb; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }
    void Add (const void *pData, size_t n)
    {
        const unsigned char *p = (const unsigned char *) pData;
        nBits += (uint64_t) n * 8;
        while (n > 0)
        {
            const size_t nTake = 64 - nBlock < n ? 64 - nBlock : n;
            memcpy (Block + nBlock, p, nTake);
            nBlock += (unsigned) nTake;
            p += nTake;
            n -= nTake;
            if (nBlock == 64)
            {
                Compress (Block);
                nBlock = 0;
            }
        }
    }
    std::string Hex ()
    {
        const uint64_t nLength = nBits;
        const unsigned char One = 0x80, Zero = 0;
        Add (&One, 1);
        while (nBlock != 56)
            Add (&Zero, 1);
        unsigned char Length[8];
        for (int i = 0; i < 8; i++)
            Length[i] = (unsigned char) (nLength >> (56 - 8 * i));
        Add (Length, 8);
        char Out[65];
        for (int i = 0; i < 8; i++)
            snprintf (Out + 8 * i, 9, "%08x", H[i]);
        return std::string (Out, 64);
    }
};
}

std::string Sha256Hex (const void *pData, size_t nBytes)
{
    TSha256 S;
    S.Add (pData, nBytes);
    return S.Hex ();
}

bool Sha256File (const std::string &Path, std::string *pHex)
{
    FILE *pFile = fopen (Path.c_str (), "rb");
    if (pFile == 0)
        return false;
    TSha256 S;
    static unsigned char Buffer[1 << 20];
    size_t n;
    while ((n = fread (Buffer, 1, sizeof Buffer, pFile)) > 0)
        S.Add (Buffer, n);
    const bool bOK = !ferror (pFile);
    fclose (pFile);
    if (bOK)
        *pHex = S.Hex ();
    return bOK;
}

/*
 *  The manifest
 */
bool VersionLess (const std::string &A, const std::string &B)
{
    const char *a = A.c_str (), *b = B.c_str ();
    for (;;)
    {
        const unsigned long x = strtoul (a, (char **) &a, 10);
        const unsigned long y = strtoul (b, (char **) &b, 10);
        if (x != y)
            return x < y;
        if (*a != '.' && *b != '.')
            return false;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

// One name, for the parts of the manifest that become a folder or a file name
// on the host (the release's version, a board's id, a zip): letters, digits,
// '.', '-', '_' and '+' (a build of a modified tree), not starting with '.'. A manifest is downloaded, so
// "../../LaunchAgents" must not reach a path.
bool SafeComponent (const std::string &Name)
{
    if (Name.empty () || Name.size () > 64 || Name[0] == '.')
        return false;
    for (char c : Name)
    {
        const bool bOK = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                      || c == '.' || c == '-' || c == '_' || c == '+';
        if (!bOK)
            return false;
    }
    return true;
}

bool SafeCardName (const std::string &Name)
{
    if (Name.empty () || Name[0] == '/' || Name[0] == '.' || Name.find ('\\') != std::string::npos
        || Name.find ('\0') != std::string::npos)
        return false;
    size_t nAt = 0;
    while (nAt <= Name.size ())
    {
        size_t nSlash = Name.find ('/', nAt);
        if (nSlash == std::string::npos)
            nSlash = Name.size ();
        const std::string Part = Name.substr (nAt, nSlash - nAt);
        if (Part.empty () || Part == "." || Part == "..")
            return false;
        nAt = nSlash + 1;
    }
    return true;
}

static bool Entries (const TJson *pFiles, std::vector<TManifestEntry> *pOut, std::string *pError)
{
    if (pFiles == 0)
        return true;
    if (pFiles->Type != TJson::Array)
    {
        *pError = "files is not a list";
        return false;
    }
    for (const TJson &F : pFiles->Items)
    {
        TManifestEntry E;
        E.Name = F.Text ("name");
        E.nSize = (unsigned long long) F.Num ("size");
        E.Sha256 = F.Text ("sha256");
        E.Component = F.Text ("component");
        if (!SafeCardName (E.Name) || E.Name == MANIFEST_FILE)
        {
            *pError = "unsafe file name in the manifest: " + E.Name;
            return false;
        }
        pOut->push_back (E);
    }
    return true;
}

bool ManifestParse (const std::string &Text, TManifest *pOut, std::string *pError)
{
    TJson J;
    std::string Why;
    if (!JsonParse (Text, &J, &Why) || J.Type != TJson::Object)
    {
        *pError = "not a manifest: " + (Why.empty () ? std::string ("not an object") : Why);
        return false;
    }
    *pOut = TManifest ();
    pOut->nSchema = (unsigned) J.Num ("schema");
    pOut->bNewerSchema = pOut->nSchema > MANIFEST_SCHEMA;

    // The frozen core, read whatever the schema says: the release's number and
    // page, and its apps. Every Okapia ever published reads them, so a release
    // in a later format can still tell an old app that it exists and give it
    // the app to update to (docs/contributing/release.md, The frozen core).
    if (const TJson *pOkapia = J.Get ("okapia"))
    {
        pOut->Version = pOkapia->Text ("version");
        pOut->Build   = pOkapia->Text ("build");
        pOut->Page    = pOkapia->Text ("page");
    }
    if (const TJson *pApps = J.Get ("apps"))
    {
        for (const TJson &A : pApps->Items)
        {
            TManifestApp App;
            App.Platform = A.Text ("platform");
            App.Archive  = A.Text ("archive");
            App.nSize    = (unsigned long long) A.Num ("size");
            App.Sha256   = A.Text ("sha256");
            App.MinOs    = A.Text ("min_os");
            App.Team     = A.Text ("team");
            if (!SafeCardName (App.Archive))
            {
                *pError = "unsafe archive name in the manifest: " + App.Archive;
                return false;
            }
            pOut->Apps.push_back (App);
        }
    }
    if (pOut->Version.empty ())
    {
        *pError = "a manifest with no okapia.version";
        return false;
    }
    if (!SafeComponent (pOut->Version))
    {
        *pError = "unsafe version in the manifest: " + pOut->Version;
        return false;
    }
    if (pOut->bNewerSchema)
    {
        // The rest may mean something else in that format: not read, and no
        // card is prepared from it (ManifestRefusal) until Okapia is updated.
        return true;
    }

    // What this schema adds to the core: the boards and their files.
    pOut->InstallerMin = J.Text ("installer_min", "0");
    if (const TJson *pOkapia = J.Get ("okapia"))
    {
        pOut->Board     = pOkapia->Text ("board");
        pOut->BoardName = pOkapia->Text ("board_name");
        pOut->Notes     = pOkapia->Text ("notes");
    }
    pOut->Archive = J.Text ("archive");
    if ((!pOut->Board.empty () && !SafeComponent (pOut->Board))
        || (!pOut->Archive.empty () && !SafeComponent (pOut->Archive)))
    {
        *pError = "unsafe board or archive name in the manifest";
        return false;
    }
    if (!Entries (J.Get ("files"), &pOut->Files, pError))
        return false;
    if (const TJson *pBoards = J.Get ("boards"))
    {
        for (const TJson &B : pBoards->Items)
        {
            TManifestBoard Board;
            Board.Id = B.Text ("id");
            Board.Name = B.Text ("name", Board.Id.c_str ());
            Board.Archive = B.Text ("archive");
            Board.InstallerMin = B.Text ("installer_min");
            if (!SafeComponent (Board.Id) || (!Board.Archive.empty () && !SafeComponent (Board.Archive)))
            {
                *pError = "unsafe board id or archive name in the manifest: " + Board.Id;
                return false;
            }
            if (!Entries (B.Get ("files"), &Board.Files, pError))
                return false;
            pOut->Boards.push_back (Board);
        }
    }
    if (pOut->Version.empty ())
    {
        *pError = "a manifest with no okapia.version";
        return false;
    }
    return true;
}

bool ManifestForBoard (const TManifest &M, const std::string &Id, TManifest *pOut)
{
    if (M.bNewerSchema)
    {
        return false;                   // its boards were not read: an Okapia update first
    }
    if (M.Boards.empty ())
    {
        // Already resolved (a card's copy, or one board's zip): for its board,
        // or for any when it names none.
        if (!M.Board.empty () && M.Board != Id)
            return false;
        *pOut = M;
        pOut->Board = Id;
        return true;
    }
    for (const TManifestBoard &B : M.Boards)
    {
        if (B.Id != Id)
            continue;
        *pOut = M;
        pOut->Boards.clear ();
        pOut->Files = B.Files;
        pOut->Archive = B.Archive;
        pOut->Board = B.Id;
        pOut->BoardName = B.Name;
        if (!B.InstallerMin.empty () && VersionLess (pOut->InstallerMin, B.InstallerMin))
            pOut->InstallerMin = B.InstallerMin;
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> ManifestBoards (const TManifest &M)
{
    std::vector<std::pair<std::string, std::string>> Out;
    if (M.Boards.empty ())
    {
        const std::string Id = M.Board.empty () ? "pi4" : M.Board;
        Out.emplace_back (Id, M.BoardName.empty () ? Id : M.BoardName);
        return Out;
    }
    for (const TManifestBoard &B : M.Boards)
        Out.emplace_back (B.Id, B.Name);
    return Out;
}

std::string ManifestRefusal (const TManifest &M)
{
    if (M.bNewerSchema)
        return "schema";
    if (VersionLess (INSTALLER_VERSION, M.InstallerMin))
        return "installer:" + M.InstallerMin;
    return "";
}

const TManifestApp *ManifestApp (const TManifest &M, const std::string &Platform)
{
    for (const TManifestApp &A : M.Apps)
        if (A.Platform == Platform)
            return &A;
    return 0;
}

// The card's copy: resolved to its board, without the release's apps, which a
// card has no use for.
std::string ManifestWrite (const TManifest &M)
{
    std::string s = std::string ("{\n  \"schema\": 1")
                  + ",\n  \"installer_min\": " + JsonQuote (M.InstallerMin)
                  + ",\n  \"okapia\": {\n    \"version\": " + JsonQuote (M.Version);
    if (!M.Build.empty ())
        s += ",\n    \"build\": " + JsonQuote (M.Build);
    if (!M.Board.empty ())
        s += ",\n    \"board\": " + JsonQuote (M.Board);
    if (!M.BoardName.empty ())
        s += ",\n    \"board_name\": " + JsonQuote (M.BoardName);
    if (!M.Notes.empty ())
        s += ",\n    \"notes\": " + JsonQuote (M.Notes);
    s += "\n  },\n  \"files\": [";
    for (size_t i = 0; i < M.Files.size (); i++)
    {
        const TManifestEntry &E = M.Files[i];
        s += (i == 0 ? "\n" : ",\n");
        s += "    { \"name\": " + JsonQuote (E.Name) + ", \"size\": " + std::to_string (E.nSize)
           + ", \"sha256\": " + JsonQuote (E.Sha256);
        if (!E.Component.empty ())
            s += ", \"component\": " + JsonQuote (E.Component);
        s += " }";
    }
    s += M.Files.empty () ? "]" : "\n  ]";
    if (!M.Archive.empty ())
        s += ",\n  \"archive\": " + JsonQuote (M.Archive);
    return s + "\n}\n";
}
