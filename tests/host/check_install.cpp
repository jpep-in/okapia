//
// check_install.cpp — putting a release on a card (src/hosts/desktop/install*.cpp).
//
// The installer's cases (okapia-installer core/src/manifest.rs, install.rs):
// JSON as a manifest uses it, SHA-256 against its published vectors, a
// release resolved to a board and refused when too new, and an install into a
// folder standing in for a card: missing, same, older and edited files, the
// backup, the removal of what the release dropped, the manifest last. Then the
// demonstration release of okapia-installer, when it is beside this tree.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "install.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <initializer_list>

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s  %s\n", bOK ? "ok  " : "FAIL", pWhat);
    if (!bOK)
        s_nFailures++;
}

static void Write (const std::string &Path, const std::string &Text)
{
    FILE *f = fopen (Path.c_str (), "wb");
    fwrite (Text.data (), 1, Text.size (), f);
    fclose (f);
}

static std::string Read (const std::string &Path)
{
    std::string s;
    FILE *f = fopen (Path.c_str (), "rb");
    if (f == 0)
        return s;
    char b[4096];
    size_t n;
    while ((n = fread (b, 1, sizeof b, f)) > 0)
        s.append (b, n);
    fclose (f);
    return s;
}

static bool Exists (const std::string &Path)
{
    struct stat Info;
    return stat (Path.c_str (), &Info) == 0;
}

static std::string Entry (const char *pName, const std::string &Data)
{
    return std::string ("{\"name\":\"") + pName + "\",\"size\":" + std::to_string (Data.size ())
         + ",\"sha256\":\"" + Sha256Hex (Data.data (), Data.size ()) + "\"}";
}

int main ()
{
    printf ("=== JSON ===\n");
    {
        TJson J;
        std::string Why;
        Expect (JsonParse ("{\"a\": [1, -2.5e1, true, null, \"\\u00e9\\n\\ud83d\\ude00\"], \"b\": {}}", &J, &Why)
                && J.Get ("a")->Items.size () == 5 && J.Get ("a")->Items[1].f == -25
                && J.Get ("a")->Items[4].s == "\xC3\xA9\n\xF0\x9F\x98\x80",
                "objects, arrays, numbers, words and escapes, surrogate pairs included");
        Expect (!JsonParse ("{\"a\": 1,}", &J, &Why) && !JsonParse ("[1 2]", &J, &Why)
                && !JsonParse ("{\"a\": 1} x", &J, &Why), "a trailing comma, a missing one, and what follows the value are refused");
        TJson Back;
        Expect (JsonParse (JsonQuote ("say \"é\"\\\t"), &Back, &Why) && Back.s == "say \"é\"\\\t",
                "a string quoted comes back as it was");
    }

    printf ("=== SHA-256 ===\n");
    Expect (Sha256Hex ("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "the FIPS 180-2 vector for \"abc\"");
    Expect (Sha256Hex ("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "and for nothing");
    {
        const std::string Long (1000000, 'a');
        Expect (Sha256Hex (Long.data (), Long.size ())
                == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "and for a million a's");
    }

    printf ("=== the manifest ===\n");
    {
        Expect (VersionLess ("0.9.1", "0.10.0") && !VersionLess ("1.0.0", "1.0") && VersionLess ("1", "1.0.1"),
                "versions compare by their numbers");
        Expect (SafeCardName ("firmware/brcmfmac43455-sdio.bin") && !SafeCardName ("../x") && !SafeCardName ("/x")
                && !SafeCardName ("a/../b") && !SafeCardName (".hidden") && !SafeCardName ("a\\b"),
                "a card name stays on the card");
        TManifest M, R;
        std::string Why;
        const std::string Text = "{\"schema\":1,\"installer_min\":\"0.1.0\",\"okapia\":{\"version\":\"261121-8b8bf89\"},"
            "\"boards\":[{\"id\":\"pi4\",\"name\":\"Raspberry Pi 4\",\"archive\":\"okapia-rpi4.zip\",\"files\":["
            + Entry ("kernel8-rpi4.img", "k") + "]},{\"id\":\"pi5\",\"name\":\"Raspberry Pi 5\",\"installer_min\":\"9.0\","
            "\"files\":[]}]}";
        Expect (ManifestParse (Text, &M, &Why) && M.Boards.size () == 2, "a release with two boards reads");
        const auto Boards = ManifestBoards (M);
        Expect (Boards.size () == 2 && Boards[1].second == "Raspberry Pi 5",
                "its boards are offered by name, a board the release adds included");
        Expect (ManifestForBoard (M, "pi4", &R) && R.Board == "pi4" && R.BoardName == "Raspberry Pi 4"
                && R.Files.size () == 1 && R.Archive == "okapia-rpi4.zip" && ManifestRefusal (R).empty (),
                "resolved to the Pi 4: its files, its zip, nothing refused");
        Expect (ManifestForBoard (M, "pi5", &R) && ManifestRefusal (R) == "installer:9.0",
                "a board needing a newer Okapia is refused for that reason");
        Expect (!ManifestForBoard (M, "pi3", &R), "a board the release lacks is not offered");
        // The version, a board's id and a zip's name become host paths
        // (ReleaseFetch's cache): a manifest that climbs out of it is refused.
        Expect (SafeComponent ("26.11.1") && SafeComponent ("261121-8b8bf89+ab12") && SafeComponent ("okapia-rpi4.zip") && !SafeComponent ("../x")
                && !SafeComponent ("x/..") && !SafeComponent (".") && !SafeComponent ("") && !SafeComponent ("a/b"),
                "a name the manifest gives is one safe component");
        Expect (!ManifestParse ("{\"schema\":1,\"okapia\":{\"version\":\"../../../../LaunchAgents\"},\"boards\":[]}",
                                &R, &Why),
                "a version that climbs out of the cache is refused");
        Expect (!ManifestParse ("{\"schema\":1,\"okapia\":{\"version\":\"26.11.1\"},"
                                "\"boards\":[{\"id\":\"x/..\",\"files\":[]}]}", &R, &Why),
                "a board id that climbs out of the cache is refused");
        Expect (!ManifestParse ("{\"schema\":1,\"okapia\":{\"version\":\"26.11.1\"},"
                                "\"boards\":[{\"id\":\"pi4\",\"archive\":\"../a.zip\",\"files\":[]}]}", &R, &Why),
                "a board's zip outside the cache is refused");
        // The frozen core: whatever a later schema changes, an Okapia of today
        // still learns that the release exists and which app to update to.
        TManifest Later;
        Expect (ManifestParse ("{\"schema\":9,\"okapia\":{\"version\":\"27.3.1\",\"page\":\"https://x/v27.3.1\"},"
                               "\"boards\":\"a shape this Okapia cannot read\",\"files\":42,"
                               "\"apps\":[{\"platform\":\"macos-arm64\",\"archive\":\"Okapia-27.3.1-macos-arm64.zip\","
                               "\"size\":7,\"sha256\":\"cd\",\"min_os\":\"14.0\",\"team\":\"ABCDE12345\"}]}",
                               &Later, &Why)
                && Later.bNewerSchema && Later.Version == "27.3.1" && Later.Page == "https://x/v27.3.1"
                && ManifestApp (Later, "macos-arm64") != 0 && ManifestApp (Later, "macos-arm64")->nSize == 7,
                "a later schema still gives its version, its page and its apps");
        Expect (ManifestRefusal (Later) == "schema" && !ManifestForBoard (Later, "pi4", &R),
                "a later schema prepares no card: Okapia is updated first");
        Expect (!ManifestParse ("{\"schema\":9,\"okapia\":{}}", &Later, &Why),
                "a later schema without the frozen version is not a manifest");
        TManifest V2;
        Expect (ManifestParse ("{\"schema\":1,\"installer_min\":\"1.0.0\",\"unknown\":[1,{}],"
                               "\"okapia\":{\"version\":\"26.11.1\",\"build\":\"261121-8b8bf89\",\"page\":\"https://x/v26.11.1\"},"
                               "\"apps\":[{\"platform\":\"macos-arm64\",\"archive\":\"Okapia-26.11.1-macos-arm64.zip\","
                               "\"size\":5,\"sha256\":\"ab\",\"min_os\":\"14.0\",\"team\":\"ABCDE12345\"}]}", &V2, &Why)
                && V2.Build == "261121-8b8bf89" && ManifestApp (V2, "macos-arm64") != 0
                && ManifestApp (V2, "macos-arm64")->Team == "ABCDE12345" && ManifestApp (V2, "linux-x86_64") == 0,
                "the release's build, its page, its apps; a key it does not know is ignored");
        Expect (ManifestWrite (V2).find ("\"schema\": 1") != std::string::npos,
                "a card's copy is written as schema 1");
        Expect (!ManifestParse ("{\"schema\":1,\"okapia\":{\"version\":\"x\"},\"files\":["
                                + Entry ("../escape", "x") + "]}", &M, &Why),
                "a file name that would leave the card is refused");
        TManifest Back;
        ManifestParse (Text, &M, &Why);
        ManifestForBoard (M, "pi4", &R);
        Expect (ManifestParse (ManifestWrite (R), &Back, &Why) && Back.Board == "pi4" && Back.Files.size () == 1
                && Back.Files[0].Sha256 == R.Files[0].Sha256, "the card's copy reads back resolved");
    }

    printf ("=== an install into a folder ===\n");
    {
        char Dir[] = "/tmp/okapiainstallXXXXXX";
        char Src[] = "/tmp/okapiasourceXXXXXX";
        if (mkdtemp (Dir) == 0 || mkdtemp (Src) == 0)
        {
            Expect (false, "two temporary folders");
            return 1;
        }
        const std::string Card = Dir, From = Src;
        const std::string Kernel1 = "kernel one", Kernel2 = "kernel two", Config1 = "config one",
                          Config2 = "config two", Old = "dropped", Wifi = "wifi";
        // The first release: kernel, config, a file the next one drops.
        std::string Text1 = "{\"schema\":1,\"installer_min\":\"0.1.0\",\"okapia\":{\"version\":\"1\",\"board\":\"pi4\"},"
            "\"files\":[" + Entry ("kernel8-rpi4.img", Kernel1) + "," + Entry ("config.txt", Config1) + ","
            + Entry ("old.bin", Old) + "]}";
        TManifest M1, M2;
        std::string Why;
        ManifestParse (Text1, &M1, &Why);
        Write (From + "/k1", Kernel1);
        Write (From + "/c1", Config1);
        Write (From + "/o", Old);
        TInstallPlan P = InstallPlan (Card, M1);
        Expect (P.Items.size () == 3 && P.Items[0].State == InstallMissing && P.Installed.empty ()
                && P.nBytesToWrite == Kernel1.size () + Config1.size () + Old.size (), "an empty card: everything missing");
        TInstallReport Report;
        const bool bFirst = InstallWrite (Card, M1, { { "kernel8-rpi4.img", From + "/k1" },
                                                      { "config.txt", From + "/c1" }, { "old.bin", From + "/o" } },
                                          0, 0, &Report, &Why);
        Expect (bFirst && Report.Written.size () == 3 && Read (Card + "/kernel8-rpi4.img") == Kernel1
                && Exists (Card + "/" + MANIFEST_FILE) && !Exists (Card + "/kernel8-rpi4.img.okapia-new"),
                "written, verified, no temporary left, the manifest last");
        P = InstallPlan (Card, M1);
        Expect (P.bUpToDate && P.Installed == "1", "and then up to date");

        // config.txt edited by hand; the second release changes the kernel and
        // the config, drops old.bin and adds firmware/wifi.bin.
        Write (Card + "/config.txt", "edited by hand");
        std::string Text2 = "{\"schema\":1,\"installer_min\":\"0.1.0\",\"okapia\":{\"version\":\"2\",\"board\":\"pi4\"},"
            "\"files\":[" + Entry ("kernel8-rpi4.img", Kernel2) + "," + Entry ("config.txt", Config2) + ","
            + Entry ("firmware/wifi.bin", Wifi) + "]}";
        ManifestParse (Text2, &M2, &Why);
        Write (From + "/k2", Kernel2);
        Write (From + "/c2", Config2);
        Write (From + "/w", Wifi);
        P = InstallPlan (Card, M2);
        Expect (P.Items[0].State == InstallDiffers && P.Items[1].State == InstallEdited
                && P.Items[2].State == InstallMissing && P.Obsolete.size () == 1 && P.Obsolete[0] == "old.bin",
                "the next release: an older kernel, an edited config, a new file, one dropped");
        Report = TInstallReport ();
        const bool bSecond = InstallWrite (Card, M2, { { "kernel8-rpi4.img", From + "/k2" },
                                                       { "config.txt", From + "/c2" },
                                                       { "firmware/wifi.bin", From + "/w" } }, 0, 0, &Report, &Why);
        Expect (bSecond && Read (Card + "/config.txt.bak") == "edited by hand" && Read (Card + "/config.txt") == Config2,
                "the edited config kept as .bak before it is replaced");
        Expect (Read (Card + "/firmware/wifi.bin") == Wifi && !Exists (Card + "/old.bin") && Report.Removed.size () == 1,
                "the new file in its folder; the dropped one, untouched, removed");
        TManifest OnCard;
        Expect (InstallReadCard (Card, &OnCard) && OnCard.Version == "2", "the card's manifest says the second release");

        // A file the copy cannot verify leaves the card's manifest as it was.
        Write (From + "/bad", "not what the manifest says");
        TManifest M3 = M2;
        M3.Version = "3";
        M3.Files[0].Sha256 = Sha256Hex ("something else", 14);
        Report = TInstallReport ();
        const bool bThird = InstallWrite (Card, M3, { { "kernel8-rpi4.img", From + "/bad" },
                                                      { "config.txt", From + "/c2" },
                                                      { "firmware/wifi.bin", From + "/w" } }, 0, 0, &Report, &Why);
        Expect (!bThird && Read (Card + "/kernel8-rpi4.img") == Kernel2 && InstallReadCard (Card, &OnCard)
                && OnCard.Version == "2", "a file that reads back wrong is not renamed, and the record stays");

        const char *const Leftovers[] = { "kernel8-rpi4.img", "config.txt", "config.txt.bak", "firmware/wifi.bin",
                                          MANIFEST_FILE, "kernel8-rpi4.img.okapia-new" };
        for (const char *p : Leftovers)
            unlink ((Card + "/" + p).c_str ());
        rmdir ((Card + "/firmware").c_str ());
        rmdir (Dir);
        for (const char *p : { "k1", "c1", "o", "k2", "c2", "w", "bad" })
            unlink ((From + "/" + p).c_str ());
        rmdir (Src);
    }

    printf ("=== a board's zip ===\n");
    {
        char Dir[] = "/tmp/okapiazipXXXXXX";
        mkdtemp (Dir);
        const std::string Base = Dir, Zip = Base + "/okapia-1-pi4.zip";
        const std::string Kernel = "a kernel", Wifi = "wifi bytes";
        const std::string Text = "{\"schema\":1,\"installer_min\":\"0.1.0\",\"okapia\":{\"version\":\"1\",\"board\":\"pi4\"},"
            "\"archive\":\"okapia-1-pi4.zip\",\"files\":[" + Entry ("kernel8-rpi4.img", Kernel) + ","
            + Entry ("firmware/wifi.bin", Wifi) + "]}";
        mz_zip_archive Z;
        memset (&Z, 0, sizeof Z);
        mz_zip_writer_init_file (&Z, Zip.c_str (), 0);
        mz_zip_writer_add_mem (&Z, "kernel8-rpi4.img", Kernel.data (), Kernel.size (), MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem (&Z, "firmware/wifi.bin", Wifi.data (), Wifi.size (), MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem (&Z, MANIFEST_FILE, Text.data (), Text.size (), MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_finalize_archive (&Z);
        mz_zip_writer_end (&Z);

        TRelease R;
        TManifest Board;
        std::string Why;
        Expect (ReleaseFromLocal (Zip, &R, &Why) && R.Zip == Zip && ManifestForBoard (R.Manifest, "pi4", &Board),
                "one board's zip is a release of its own, its manifest inside");
        std::map<std::string, std::string> Files;
        Expect (ReleaseFetch (R, Board, Base + "/cache", &Files, 0, 0, &Why) && Files.size () == 2
                && Read (Files["firmware/wifi.bin"]) == Wifi, "its files unzipped and checked, a folder included");
        Board.Files[0].Sha256 = Sha256Hex ("x", 1);
        Expect (!ReleaseFetch (R, Board, Base + "/cache2", &Files, 0, 0, &Why) && Why.find ("SHA-256") != std::string::npos,
                "a member that is not what the manifest says is refused");
        Expect (!ReleaseFromLocal (Base + "/nothing", &R, &Why), "and a path that is no release, too");
        system (("rm -rf '" + Base + "'").c_str ());
    }

    printf ("=== okapia-installer's demonstration release, when beside this tree ===\n");
    {
        const std::string Demo = "../../../okapia-installer/demo-release";
        TRelease R;
        std::string Why;
        if (!ReleaseFromLocal (Demo, &R, &Why))
            printf ("  skip  %s (%s)\n", Demo.c_str (), Why.c_str ());
        else
        {
            TManifest Board;
            Expect (ManifestForBoard (R.Manifest, "pi4", &Board) && Board.Files.size () > 5
                    && ManifestRefusal (Board).empty (), "it reads, resolves to the Pi 4, and is not refused");
            char Dir[] = "/tmp/okapiademoXXXXXX";
            mkdtemp (Dir);
            const std::string Base = Dir;
            std::map<std::string, std::string> Files;
            const bool bFetched = ReleaseFetch (R, Board, Base + "/cache", &Files, 0, 0, &Why);
            Expect (bFetched && Files.size () == Board.Files.size (), "every file of its zip unzipped and checked");
            mkdir ((Base + "/card").c_str (), 0755);
            TInstallReport Report;
            const bool bWritten = bFetched && InstallWrite (Base + "/card", Board, Files, 0, 0, &Report, &Why);
            TManifest OnCard;
            Expect (bWritten && Report.Written.size () == Board.Files.size ()
                    && InstallReadCard (Base + "/card", &OnCard) && OnCard.Version == R.Manifest.Version
                    && InstallPlan (Base + "/card", Board).bUpToDate,
                    "installed into a folder standing in for a card, and then up to date");
            system (("rm -rf '" + Base + "'").c_str ());
        }
    }

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
