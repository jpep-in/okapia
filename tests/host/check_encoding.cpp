/*
 * check_encoding.cpp — les noms de fichiers du dossier partagé.
 *
 * Entre une carte FAT en UTF-8 et un Macintosh en MacRoman, la conversion est
 * exactement le genre de code qui a l'air juste et abîme un nom sur cinquante :
 * une table de 128 entrées, trois formes d'UTF-8, et deux cas où il vaut mieux
 * ne rien faire que faire à moitié. Rien de tout cela ne demande une carte, un
 * émulateur ni un Macintosh, donc rien de tout cela n'a de raison d'être
 * découvert sur une carte.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "cpu_emulation.h"

// mac_encoding_circle.cpp les appelle sous --wrap ; ici on les nomme.
extern "C" const char *__wrap__Z25host_encoding_to_macromanPKc (const char *);
extern "C" const char *__wrap__Z25macroman_to_host_encodingPKc (const char *);

#define ToMac  __wrap__Z25host_encoding_to_macromanPKc
#define ToHost __wrap__Z25macroman_to_host_encodingPKc

/*
 *  Le Macintosh que ce test n'a pas
 *
 *  La sonde du Script Manager ne part que si un 68000 tourne. Ici il n'y en a
 *  pas, ce qui est aussi le cas au démarrage réel : la conversion MacRoman est
 *  alors le pari par défaut, et c'est ce pari qui est mesuré.
 */
bool MacIsExecuting (void) { return false; }

void Execute68k (uint32, M68kRegisters *)      { }
void Execute68kTrap (uint16, M68kRegisters *)  { }

// Host2Mac_memcpy() est une fonction en ligne sous DIRECT_ADDRESSING et lit
// cet offset. Rien ne l'appelle ici — la sonde ne part pas — mais il faut
// qu'il existe pour que l'éditeur de liens s'y retrouve.
uintptr MEMBaseDiff;

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

static void ExpectSame (const char *pGot, const char *pWant, const char *pWhat)
{
    bool bOK = strcmp (pGot, pWant) == 0;
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        printf ("        attendu \"%s\", obtenu \"%s\"\n", pWant, pGot);
        s_nFailures++;
    }
}

/*
 *  L'aller et le retour
 */

static void CheckRoundTrip (void)
{
    printf ("aller-retour\n");

    // Un nom accentué, le cas qui a motivé tout le fichier. En MacRoman, é est
    // 0x8E et û est 0x9E.
    static const char Mac[]  = "R\x8Esum\x8E.txt";
    static const char Host[] = "R\xC3\xA9sum\xC3\xA9.txt";

    ExpectSame (ToMac (Host), Mac,  "UTF-8 accentué vers MacRoman");
    ExpectSame (ToHost (Mac), Host, "et MacRoman vers UTF-8");

    ExpectSame (ToMac (ToHost (Mac)), Mac, "l'aller-retour ne perd rien");

    // L'ASCII traverse sans être touché, ce qui est la quasi-totalité des noms.
    ExpectSame (ToMac ("System Folder"),  "System Folder", "l'ASCII passe tel quel");
    ExpectSame (ToHost ("System Folder"), "System Folder", "dans les deux sens");

    ExpectSame (ToMac (""),  "", "le nom vide aussi");
    ExpectSame (ToHost (""), "", "dans les deux sens");
}

/*
 *  Les caractères que MacRoman a et que personne n'attend
 */

static void CheckHighRange (void)
{
    printf ("le haut de la table\n");

    // 0xA5 est le rond médian, 0xD0 le tiret demi-cadratin, 0xAA le symbole
    // marque déposée : trois points de code hors du Latin-1, donc trois octets
    // en UTF-8. Ils prouvent que l'encodeur ne s'arrête pas à deux.
    static const char Mac[]  = "\xA5\xD0\xAA";
    static const char Host[] = "\xE2\x80\xA2\xE2\x80\x93\xE2\x84\xA2";

    ExpectSame (ToHost (Mac), Host, "trois octets en sortie quand il en faut trois");
    ExpectSame (ToMac (Host), Mac,  "et trois octets en entrée");

    // Les 128 entrées, une par une : une seule erreur de transcription se
    // cacherait derrière n'importe quel test plus étroit.
    bool bAll = true;
    for (int i = 0x80; i < 0x100; i++)
    {
        char Name[2] = { (char) i, 0 };
        char Copy[8];
        strcpy (Copy, ToHost (Name));
        bAll = bAll && strcmp (ToMac (Copy), Name) == 0;
    }
    Expect (bAll, "les 128 entrées hautes font l'aller-retour");
}

/*
 *  Ce qui ne se convertit pas
 *
 *  Rendre le nom d'origine est un choix, pas un oubli : un nom que le Mac ne
 *  peut pas lire est une gêne, un demi-nom est une perte.
 */

static void CheckRefusals (void)
{
    printf ("les refus\n");

    // Un caractère chinois : parfaitement valide en UTF-8, absent de MacRoman.
    static const char Chinese[] = "\xE6\x96\x87.txt";
    ExpectSame (ToMac (Chinese), Chinese,
                "un caractère hors MacRoman laisse le nom entier");

    // De l'UTF-8 malformé — une continuation isolée — ne doit rien produire.
    static const char Broken[] = "a\xC3.txt";
    ExpectSame (ToMac (Broken), Broken, "de l'UTF-8 cassé n'est pas deviné");

    // Un plan supplémentaire (un emoji) : quatre octets, hors MacRoman.
    static const char Emoji[] = "\xF0\x9F\x8D\x8E.txt";
    ExpectSame (ToMac (Emoji), Emoji, "et un caractère à quatre octets non plus");

    Expect (ToMac (0) == 0 && ToHost (0) == 0, "un pointeur nul revient nul");
}

/*
 *  Les longueurs
 */

static void CheckLengths (void)
{
    printf ("les longueurs\n");

    // 255 octets, la limite d'un nom long FAT, tous accentués : 510 octets en
    // UTF-8 à l'aller, 255 en MacRoman au retour. Le tampon doit tenir.
    char Long[256];
    for (int i = 0; i < 255; i++)
    {
        Long[i] = (char) 0x8E;      // é
    }
    Long[255] = 0;

    char Utf8[1024];
    strcpy (Utf8, ToHost (Long));
    Expect (strlen (Utf8) == 510, "255 lettres accentuées font 510 octets");
    ExpectSame (ToMac (Utf8), Long, "et reviennent entières");

    // Au-delà du tampon, le nom revient intact et non tronqué. Aucune carte FAT
    // ne produit un nom pareil ; c'est la garde qui est mesurée, pas le cas.
    static char Huge[600];
    for (int i = 0; i < 599; i++)
    {
        Huge[i] = (char) 0xA5;      // trois octets chacun en UTF-8
    }
    Huge[599] = 0;
    ExpectSame (ToHost (Huge), Huge, "un nom trop long revient tel quel");
}

int main (void)
{
    CheckRoundTrip ();
    CheckHighRange ();
    CheckRefusals ();
    CheckLengths ();

    if (s_nFailures > 0)
    {
        printf ("\n%u échec(s)\n", s_nFailures);
        return 1;
    }
    printf ("\nTout passe.\n");
    return 0;
}
