# Release notes

One file per release, `<YY.MM.N>.md`, written before `scripts/release.sh <YY.MM.N>` runs: the script refuses a
release without its notes, makes them the tag's message and the GitHub release's text
([Releases](../contributing/release.md)). Written for the people who use Okapia, not for those who build it.

```markdown
# Okapia 26.11.1

What a person notices: what Okapia now does, what it no longer gets wrong. A sentence each, the most noticeable
first.

## Install

- **On a Raspberry Pi 4**: open Okapia on a computer, choose *For: Raspberry Pi 4*, insert the card and choose
  *SD Card…*, then *Prepare the Card*. Or unzip `okapia-26.11.1-pi4.zip` onto a FAT32 card.
- **On a Mac** (Apple silicon, macOS 14 or later): unzip `Okapia-26.11.1-macos-arm64.zip` and move Okapia to
  Applications. Until the app is signed with a Developer ID, the first opening is right-click → Open, then Open.

Okapia ships no Apple ROM and no Apple System: use those of a Macintosh you own.

## Compatible with

Cards prepared by earlier versions are updated in place; their ROMs, disks and preferences are kept.
```
