Language packs
==============

A **language pack** translates Paper Cut's own menus (the settings page, the launcher and the Start menu's Settings tab) into a language of your own, or fixes the texts of a language Paper Cut already has. It is a folder of plain text files in the mods folder: no rebuild, no tools.

The game's own text (dialogue, items, the original menus) is not part of it: that comes from the disc. To change the game's messages, see [Mods](MODDING.md) (`messages/`).

## Where it goes

In the **mods folder** (see [MODDING.md](MODDING.md) for where it is on each platform; Language > Language packs > "Open the mods folder" opens it):

```
mods/
  my-language/
    lang/
      pack.txt       what the pack is
      strings.txt    the texts (any number of .txt files, any names)
      myfont.ttf     a font, if your script needs one (optional)
```

A pack is a folder (not a .zip or .ttydmod), and needs no `mod.json`. A mod folder that has one can carry a `lang/` folder too. Packs are listed in the [mod manager](MODDING.md#the-mod-manager) with the mods: its switch turns a pack off, and dropping a pack's folder, or a zip of one (unpacked into a folder), on Paper Cut's window installs it.

## The quickest start

Language > Language packs > **Make a language pack template** writes `mods/my-language/lang/strings.txt` with every English text of the menus, each with an empty line under it for your translation, and a `pack.txt` to fill in. Pressing it again later (after an update of Paper Cut) adds only the texts your file doesn't have yet, at its end, under `# New texts to translate`.

Translate a few lines, set `name=` and `code=` in `pack.txt`, press **Reload language packs**, and choose your language in Language > Menu language. Reload shows changed texts at once, without restarting.

## pack.txt

```
name=Português
code=pt
for=menus
fallback=es
font=myfont.ttf
```

| Key | |
|---|---|
| `name` | The language in its own words: how Language > Menu language lists it. |
| `code` | The language's code, lower case: `pt`, `pt-br`, `ko`, `ru`... One Paper Cut has (`de`, `fr`, `es`, `it`, `ja`, or `en`) makes the pack change that language's texts instead of adding one: a native speaker's fixes, say. |
| `for` | `menus` (Paper Cut's own menus; the only kind for now). |
| `fallback` | Optional, for a new language: a text the pack lacks is shown in this built-in language (`es`, `fr`...) instead of English. |
| `font` | Optional: a `.ttf` or `.otf` file in the `lang/` folder, for letters the menus' font lacks (see Fonts). |

Lines starting with `#` are comments.

## The texts

Every `.txt` file in `lang/` other than `pack.txt` holds texts, read in name order. They are UTF-8, in the same format as Paper Cut's own translations (`port/assets/lang/` in the source):

```
# a comment
Settings
Configurações

Close
Fechar
```

- The English text, exactly as Paper Cut shows it, on one line; its translation on the next; a blank line between texts.
- A line break inside a text is written `\n`.
- Keep `%s`, `%d` and `%%` as they are, in the same order: Paper Cut puts a name or a number there. A translation whose `%` codes differ from the English's isn't used (the pack's problems list says which).
- Keep the buttons in braces as they are, wherever your sentence needs them: `{A}`, `{B}`, `{X}`, `{Y}`, `{Z}`, `{L}`, `{R}`, `{START}`, `{STICK}`, `{CSTICK}` and `{DPAD}` (the game's buttons), Paper Cut's actions such as `{PING}`, `{PHOTO}` or `{FF}`, and a controller's own buttons with their key, such as `{SOUTH:Return}`. Each shows as the button that does it now: its icon for the controller in use (Controls > Button prompts) or the key's name on the keyboard, where the player has bound it (Controls > Bindings). Words around them, such as "the" in "the {DPAD}", are yours.
- An English line with nothing under it is **not translated yet**: it stays as the built-in language would show it. The pack's list in Language > Language packs counts them.
- If one pack translates a text twice differently, the first is used and the list says so.

A text is shown, in order: the pack's translation; else the built-in language's (for `code=de`, Paper Cut's German; for a new language, its `fallback=`); else English. Where two packs have the same code, their texts are merged, the folder first by name winning.

The list in Language > Language packs shows each pack: what it does, how many texts it translates and how many are still empty, and anything wrong with it (a key it doesn't know, a missing font file, a text translated twice).

`$TTYD_TR_DUMP=<file>` (an environment variable) writes each English text to the file as the menus ask for it: the same list the template comes from, for finding a text's exact English.

## Paper Cut's own languages

Paper Cut's menus come in English, German, French, Spanish, Italian and Japanese (`port/assets/lang/<code>/`). Each translation names the game's things as that language's release of the game does: the partners, places, items, Star Points, the Tattle Log, HP, FP and BP as the European release has them in German, French, Spanish and Italian, and as the Japanese release has them in Japanese. The achievements' names and notes are translated too. For the Start menu on the Japanese release, the Japanese texts have a second wording that keeps to the kanji the game's font has (`port/assets/lang/ja_game/`).

## Fonts

**The settings page and the launcher** use Fredoka. It has the Latin letters of Western Europe (Latin-1: French, German, Spanish, Italian, Portuguese, the Nordic languages...) and a few more (Œ, Š, Ž, Ł, ı), and Hebrew. It has no Cyrillic, Greek, Korean, Chinese, Arabic or Thai, and not most of the letters of Central European languages (ą, č, ő...).

For letters Fredoka lacks, Paper Cut tries, in order:
1. the pack's own `font=` file, for that language's letters only;
2. the system's Japanese font (Yu Gothic or Meiryo on Windows, Hiragino on macOS, Noto CJK on Linux and Android), which has the kanji Japanese uses (so many Chinese characters, not all), and Cyrillic and Greek, but drawn full-width: Русский reads as widely spaced letters. It has no Korean.

So any pack outside Latin-1 looks best with a font of its own, and a Korean pack, or any pack where text shows as `?`, needs one: one that allows redistribution (the SIL Open Font License, like Noto Sans KR). Letters are added to the fonts at the start: after Reload, letters new to them show after a restart (the page says so, with Restart now).

**The game's own font** (the Start menu's Settings tab, drawn with the disc's font) has only the game's characters: Latin-1 on the US and European releases; on the Japanese release, ASCII, the kana and about 200 kanji. A text shown there with any character that font lacks is shown in English there instead (the settings page still shows the translation). So a Portuguese pack shows in the Start menu on the US and European releases; a Russian or Korean one shows English there; on the Japanese release only texts with no accented letters (and Japanese within its font) show translated.

## Sharing a pack

Paper Cut never downloads packs: a pack is only ever one you put in the mods folder yourself.

Zip the pack's folder (`my-language/`, with `lang/` inside) and share it. To install, unzip it into the mods folder so that `mods/my-language/lang/pack.txt` exists, then press Reload language packs (or restart). A pack works on every platform and with every release (US, European, Japanese): it is keyed by the English texts, which are the same everywhere.

New versions of Paper Cut add texts now and then: Make a language pack template on your pack's folder adds them. (It writes to `mods/my-language`: rename your folder to that while updating, or copy the new lines across.)

To make a translation part of Paper Cut itself, send its file: it goes in `port/assets/lang/<code>/` and `tools/port/gen_lang.py` builds it in (that file's own rules are the same, but every text must be translated).
