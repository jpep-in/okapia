/*
 * render_website.cpp — website frames drawn by the real boot-menu renderer.
 * Only the workspace and network data are examples; no card or ROM is opened.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <vector>

#include "okapia_chooser.h"
#include "okapia_display.h"
#include "okapia_network.h"
#include "okapia_settings.h"
#include "okapia_info.h"
#include "okapia_journal.h"
#include "okapia_page.h"
#include "okapia_screen.h"
#include "okapia_strings.h"

static bool Save (const char *pFolder, const char *pName, TSurface *pSurface,
                  unsigned (*Widgets) (TWidget **), void (*Repaint) (TSurface *))
{
    TTheme Theme;
    ThemeMake (ThemeScaleFor (pSurface->nWidth, pSurface->nHeight), &Theme);
    TWidget *pWidgets = nullptr;
    const unsigned n = Widgets (&pWidgets);
    TScreen Screen;
    ScreenInit (&Screen, &Theme, pWidgets, n);
    Repaint (pSurface);

    if (strcmp (pName, "startup") == 0)
    {
        char MapPath[1024];
        snprintf (MapPath, sizeof MapPath, "%s/tabs.json", pFolder);
        FILE *pMap = fopen (MapPath, "w");
        if (!pMap) return false;
        fputs ("[", pMap);
        for (unsigned i = 0; i < n; ++i) if (pWidgets[i].Type == WidgetTabs)
        {
            const TWidget &W = pWidgets[i];
            const char *Labels[PageTabCount];
            for (unsigned j = 0; j < W.nItems; ++j) Labels[j] = W.pItems[j].pText;
            for (unsigned j = 0; j < W.nItems; ++j)
            {
                const TRect R = ThemeTabRect (&Theme, W.Rect, Labels, W.nItems, j);
                fprintf (pMap, "%s{\"x\":%d,\"y\":%d,\"width\":%u,\"height\":%u}",
                         j ? "," : "", R.nX, R.nY, R.nWidth, R.nHeight);
            }
        }
        fputs ("]", pMap);
        if (fclose (pMap) != 0) return false;
    }

    char Path[1024];
    const int nPath = snprintf (Path, sizeof Path, "%s/%s.ppm", pFolder, pName);
    if (nPath < 0 || (unsigned) nPath >= sizeof Path) return false;
    FILE *pFile = fopen (Path, "wb");
    if (!pFile) return false;
    bool bOK = fprintf (pFile, "P6\n%u %u\n255\n", pSurface->nWidth, pSurface->nHeight) > 0;
    const unsigned *pPixels = (const unsigned *) pSurface->pPixels;
    for (unsigned i = 0; bOK && i < pSurface->nWidth * pSurface->nHeight; ++i)
    {
        const unsigned c = pPixels[i];
        const unsigned char RGB[] = { (unsigned char) (c >> 16),
                                      (unsigned char) (c >> 8), (unsigned char) c };
        bOK = fwrite (RGB, 1, sizeof RGB, pFile) == sizeof RGB;
    }
    return fclose (pFile) == 0 && bOK;
}

int main (int argc, char **argv)
{
    if (argc != 2) { fprintf (stderr, "Usage: render_website OUTPUT_FOLDER\n"); return 1; }
    const unsigned w = 1280, h = 960;
    std::vector<unsigned> Pixels (w * h);
    TSurface Surface = { (unsigned char *) Pixels.data (), w, h, w * sizeof (unsigned) };
    StringsSetLanguage (StringsFromCode ("en"));
    PageChromeSet (false, false, true);
    PageChromeBootMenu (true, false);

    TChooser Chooser = {};
    Chooser.nCount = 2;
    Chooser.Built = CPU68k;
    for (unsigned i = 0; i < Chooser.nCount; ++i)
    {
        TChooserVolume &V = Chooser.Volumes[i];
        snprintf (V.Path, sizeof V.Path, "/%s.img", i ? "macos9" : "system7");
        snprintf (V.Name, sizeof V.Name, "%s", i ? "Mac OS 9" : "Macintosh HD");
        snprintf (V.System, sizeof V.System, "%s", i ? "9.0" : "7.5");
        V.CPU = V.Engine = i ? CPUPowerPC : CPU68k;
        V.bBootable = V.bClean = V.bMounted = true;
        V.Mount = MountHD;
        V.nFreeKB = 240 * 1024;
        V.nTotalKB = 500 * 1024;
    }
    ChooserDraw (&Surface, &Chooser);
    if (!Save (argv[1], "startup", &Surface, ChooserWidgets, ChooserRepaint)) return 1;
    Chooser.nStartup = 1;
    ChooserDraw (&Surface, &Chooser);
    if (!Save (argv[1], "powerpc", &Surface, ChooserWidgets, ChooserRepaint)) return 1;

    TSettings Settings = {};
    Settings.V.nMemoryMB = 256;
    Settings.V.nSound = SoundHDMI;
    Settings.V.bShared = true;
    strcpy (Settings.V.SharedPath, "/shared");
    strcpy (Settings.V.SharedName, "Okapia");
    Settings.Opened = Settings.V;
    SettingsDraw (&Surface, &Settings);
    if (!Save (argv[1], "settings", &Surface, SettingsWidgets, SettingsRepaint)) return 1;

    TNetwork Network = {};
    Network.V.nKind = NetworkWiFi;
    strcpy (Network.V.SSID, "Home Wi-Fi");
    strcpy (Network.V.Country, "GB");
    Network.Opened = Network.V;
    Network.nRunning = NetworkWiFi;
    Network.nAvailable = (1u << NetworkOff) | (1u << NetworkCable) | (1u << NetworkWiFi);
    Network.nSeen = 1;
    strcpy (Network.Seen[0].SSID, "Home Wi-Fi");
    Network.Seen[0].nDbm = -42;
    Network.Seen[0].bSecured = true;
    Network.State = NetStateConnected;
    strcpy (Network.Subject, "Home Wi-Fi");
    Network.nDbm = -42;
    NetworkDraw (&Surface, &Network);
    if (!Save (argv[1], "network", &Surface, NetworkWidgets, NetworkRepaint)) return 1;

    TDisplaySettings Display = {};
    Display.Output = Display.Preferred = {1920, 1080};
    Display.Start = {640, 480};
    DisplayDraw (&Surface, &Display);
    if (!Save (argv[1], "display", &Surface, DisplayWidgets, DisplayRepaint)) return 1;
    TInfo Info = {};
    InfoAdd (&Info, "Machine", "Raspberry Pi 4");
    InfoAdd (&Info, "Display", "1920 x 1080");
    InfoAdd (&Info, "Startup disk", "Macintosh HD");
    InfoCredit (&Info, "Okapia", "Jonathan Pepin / GPLv3+");
    InfoCredit (&Info, "Basilisk II / SheepShaver", "Christian Bauer et al.");
    InfoCredit (&Info, "Circle", "Rene Stange");
    InfoDraw (&Surface, &Info);
    if (!Save (argv[1], "info", &Surface, InfoWidgets, InfoRepaint)) return 1;

    static TJournal Journal;
    JournalClear (&Journal);
    const char *Lines = "Example session\nOkapia: 256 MB RAM, 68040\nNetwork: Wi-Fi connected\nStartup disk: Macintosh HD\n";
    JournalAppend (&Journal, Lines, (unsigned) strlen (Lines));
    JournalDraw (&Surface, &Journal);
    if (!Save (argv[1], "journal", &Surface, JournalWidgets, JournalRepaint)) return 1;
    return 0;
}
