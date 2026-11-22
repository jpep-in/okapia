//
// menus_desktop.mm — the application's menus and its one dialog, in Cocoa.
//
// SDL gives a Cocoa application its menus: Hide (Command-H), Quit (Command-Q),
// Minimize (Command-M)… A menu's key equivalent is taken before the window
// sees the key, so the Macintosh inside never got Command-Q, the one shortcut
// every Mac application answers. Here every Command equivalent of those menus
// becomes a Control one: Control-Q quits Okapia, and Command-anything goes to
// the Macintosh, as on the board's keyboard.
//
// Okapia's own: a File menu to change the Okapia Workspace (the folder laid out
// like a card: preferences, ROMs, disk images), which opens the welcome page,
// to show it in the Finder and to go full screen. The choosers themselves are
// SDL's, the system's own on every OS (okapia_host_desktop.cpp, HostPick).
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#import <AppKit/AppKit.h>
#include <DiskArbitration/DiskArbitration.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Declared here rather than included: the project's headers bring okapia_gfx.h,
// whose Rect meets the Mac's own Rect in AppKit's headers.
void OkapiaHostLogV (const char *pFrom, int nSeverity, const char *pFormat, va_list Args);
void DesktopMenusToControl (void);
bool DesktopShiftHeld (void);
void DesktopMenuBarVisible (void *pNSWindow, bool bShown);
bool DesktopDeviceRemovable (const char *pDevice);

void DesktopRevealWorkspace (const char *pPath);
int  DesktopTakeMenuRequest (void);

static void Log (const char *pFormat, ...)
{
    va_list Args;
    va_start (Args, pFormat);
    OkapiaHostLogV ("desktop", 3, pFormat, Args);      // LogNotice
    va_end (Args);
}

// French when the person's first language is, English otherwise: the
// preferences, which say the menu's language, are inside the workspace not
// chosen yet.
static bool French (void)
{
    NSString *pFirst = [[NSLocale preferredLanguages] firstObject];
    return pFirst != nil && [pFirst hasPrefix:@"fr"];
}

/*
 *  Requests from the menus, taken by the main loop (main_desktop.cpp)
 */
static volatile int s_nRequest;         // 0 none, 1 change workspace, 2 reveal it, 3 full screen

@interface OkapiaMenuTarget : NSObject
@end

@implementation OkapiaMenuTarget
- (void) changeWorkspace:(id)sender { (void) sender; s_nRequest = 1; }
- (void) revealWorkspace:(id)sender { (void) sender; s_nRequest = 2; }
- (void) toggleFullScreenOkapia:(id)sender { (void) sender; s_nRequest = 3; }
@end

static OkapiaMenuTarget *s_pTarget;

int DesktopTakeMenuRequest (void)
{
    const int n = s_nRequest;
    s_nRequest = 0;
    return n;
}

static unsigned MoveToControl (NSMenu *pMenu)
{
    unsigned nMoved = 0;
    for (NSMenuItem *pItem in [pMenu itemArray])
    {
        if ([pItem hasSubmenu])
        {
            nMoved += MoveToControl ([pItem submenu]);
        }
        NSEventModifierFlags Mask = [pItem keyEquivalentModifierMask];
        if ([[pItem keyEquivalent] length] != 0 && (Mask & NSEventModifierFlagCommand) != 0)
        {
            Mask = (Mask & ~NSEventModifierFlagCommand) | NSEventModifierFlagControl;
            [pItem setKeyEquivalentModifierMask:Mask];
            nMoved++;
        }
    }
    return nMoved;
}

static NSMenuItem *Item (NSString *pTitle, SEL Action, NSString *pKey)
{
    NSMenuItem *pItem = [[NSMenuItem alloc] initWithTitle:pTitle action:Action keyEquivalent:pKey];
    [pItem setKeyEquivalentModifierMask:NSEventModifierFlagControl];
    [pItem setTarget:s_pTarget];
    return pItem;
}

void DesktopMenusToControl (void)
{
    @autoreleasepool
    {
        NSMenu *pMain = [NSApp mainMenu];
        if (pMain == nil)
        {
            return;
        }
        const unsigned nMoved = MoveToControl (pMain);

        // File, after the application's menu, as every Mac application has it.
        s_pTarget = [[OkapiaMenuTarget alloc] init];
        const bool bFr = French ();
        NSMenu *pFile = [[NSMenu alloc] initWithTitle:bFr ? @"Fichier" : @"File"];
        [pFile addItem:Item (bFr ? @"Changer d’Okapia Workspace…" : @"Change Okapia Workspace…",
                             @selector (changeWorkspace:), @"o")];
        [pFile addItem:Item (bFr ? @"Afficher l’Okapia Workspace dans le Finder"
                                 : @"Show Okapia Workspace in Finder",
                             @selector (revealWorkspace:), @"r")];
        [pFile addItem:[NSMenuItem separatorItem]];
        [pFile addItem:Item (bFr ? @"Plein écran" : @"Full Screen",
                             @selector (toggleFullScreenOkapia:), @"f")];
        NSMenuItem *pFileItem = [[NSMenuItem alloc] initWithTitle:[pFile title] action:nil
                                                    keyEquivalent:@""];
        [pFileItem setSubmenu:pFile];
        [pMain insertItem:pFileItem atIndex:([pMain numberOfItems] > 0 ? 1 : 0)];

        Log ("Menus: %u shortcut(s) on Control, Command left to the Macintosh (Control-Q quits)",
             nMoved);
    }
}

// Over a full-screen window that is not in a Space of its own (main_desktop.cpp
// asks SDL for that): the menu bar shown or hidden at once, not on hover.
//
// SDL lifts such a window above the menu bar (the main menu's level, plus
// one) and hides the bar whenever the window becomes key: shown alone, the bar
// was there, under the window. So the window comes down to the normal level
// while the bar is shown, and the presentation options say the same.
void DesktopMenuBarVisible (void *pNSWindow, bool bShown)
{
    @autoreleasepool
    {
        NSWindow *pWindow = (__bridge NSWindow *) pNSWindow;
        if (pWindow != nil)
        {
            [pWindow setLevel:bShown ? NSNormalWindowLevel : NSMainMenuWindowLevel + 1];
        }
        [NSApp setPresentationOptions:bShown ? NSApplicationPresentationDefault
                                             : (NSApplicationPresentationHideDock
                                                | NSApplicationPresentationHideMenuBar)];
        [NSMenu setMenuBarVisible:bShown ? YES : NO];
        Log ("Full screen: menu bar %s", bShown ? "shown" : "hidden");
    }
}

bool DesktopShiftHeld (void)
{
    return ([NSEvent modifierFlags] & NSEventModifierFlagShift) != 0;
}

void DesktopRevealWorkspace (const char *pPath)
{
    @autoreleasepool
    {
        NSURL *pURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:pPath]];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ pURL ]];
    }
}

// Whether the volume at this BSD device is a card or a stick, not a disk image
// (a mounted .dmg is ejectable too) or the Mac's own disk (cards_desktop.cpp).
bool DesktopDeviceRemovable (const char *pDevice)
{
    static DASessionRef s_Session;
    if (s_Session == 0)
    {
        s_Session = DASessionCreate (kCFAllocatorDefault);
        if (s_Session == 0)
        {
            return false;
        }
    }
    const char *pName = strncmp (pDevice, "/dev/", 5) == 0 ? pDevice + 5 : pDevice;
    DADiskRef Disk = DADiskCreateFromBSDName (kCFAllocatorDefault, s_Session, pName);
    if (Disk == 0)
    {
        return false;
    }
    bool bRemovable = false;
    CFDictionaryRef Description = DADiskCopyDescription (Disk);
    if (Description != 0)
    {
        const CFBooleanRef Ejectable = (CFBooleanRef) CFDictionaryGetValue (Description,
                                                        kDADiskDescriptionMediaEjectableKey);
        const CFBooleanRef Removable = (CFBooleanRef) CFDictionaryGetValue (Description,
                                                        kDADiskDescriptionMediaRemovableKey);
        const CFStringRef Protocol = (CFStringRef) CFDictionaryGetValue (Description,
                                                        kDADiskDescriptionDeviceProtocolKey);
        const bool bVirtual = Protocol != 0
            && CFStringCompare (Protocol, CFSTR ("Virtual Interface"), 0) == kCFCompareEqualTo;
        // OKAPIA_CARDS_IMAGES (the test mode's --cards-images): a mounted image
        // stands in for a card, so a test needs no reader.
        const bool bImages = getenv ("OKAPIA_CARDS_IMAGES") != 0;
        bRemovable = (!bVirtual || bImages) && ((Ejectable != 0 && CFBooleanGetValue (Ejectable))
                                   || (Removable != 0 && CFBooleanGetValue (Removable)));
        CFRelease (Description);
    }
    CFRelease (Disk);
    return bRemovable;
}
