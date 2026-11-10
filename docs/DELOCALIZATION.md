Delocalisation
==============

The US and European releases of Paper Mario: The Thousand-Year Door changed a few things from the Japanese release: some pictures and scenes, and some of the text (Vivian's lines are the best-known case). Paper Cut can put both kinds back, in two different ways:

- **Pictures and scenes**: Settings > Enhancements > Restorations > **Restorations**. Each one is its own switch, off until you turn it on. They come from your own discs: data the US and European discs still have, the Japanese release's own way of running a scene, or (for one of them) a file read from your own Japanese disc.
- **Text**: **fan text packs**. Paper Cut doesn't ship the game's text or new wording of the game's script. Instead it lets anyone write a pack that changes any of the game's messages, in any of its languages, and makes that easy to start: Language > **Game text packs** writes every message on your disc into your mods folder as a template.

This page is about both: what Restorations does, then how a text pack is laid out, how to make one, and how to install one.

## Restorations

Settings > Enhancements > Restorations, on the US and European builds (the Japanese build already plays this way):

| Setting | What it puts back | Where it comes from |
|---|---|---|
| Rogueport's chalk outline | The chalk outline and the marks on a Rogueport house's floor, under the flies | Your Japanese disc, or redrawn from your own disc (see below) |
| The Parlor Boo's bunny ears | The Pianta Parlor's Boo, and her turn in the credits, in her bunny ears instead of cat ears | The Japanese model, still on the US and European discs |
| Japanese screen flashes | The flashes the US release cut down: the Great Tree's time bomb, the darkness falling, the Shadow Queen. **Rapid flashing light.** | The Japanese release's own timing of those scenes |
| 1000 X-Nauts in the prologue | The prologue's crowd at its Japanese size, 1000 rather than 400, and its smoke | The Japanese release's own numbers |
| Japanese bad ending | Say yes to the Shadow Queen and Mario stays at her side | The Japanese release's scene |
| Hooktail fears frogs | Attack FX R's croak is a frog's, as in Japan, and Hooktail says so | The sound is the game's; three short lines about it are Paper Cut's own wording (an older setting, the one place it writes game text) |
| Japanese recipes | Zess T. cooks a Trial Stew from a Thunder Rage and a Thunder Bolt | The Japanese recipe table |

None of them changes how the game plays except the recipe, which is marked as changing play and is off online.

**The chalk outline: the Japanese release's own, or redrawn.** Only the Japanese disc has that room's floor marks. With your Japanese disc image (G8MJ01) in the launcher's library, turn the setting on and start the game: Paper Cut reads that one room from it, keeps it in your user folder (`restorations/G8MJ01/`), and uses it from then on, even without the Japanese disc in the library. Without a Japanese disc, the outline is redrawn from your own US or European disc: Mario's outline, traced from his own arms-out picture on the disc (an [art recipe](../port/assets/luigi/art/RECIPES.md)), drawn as a chalk-white line where the Japanese release's lies, with the stains of the room's own texture where the Japanese release has them. It is a look-alike, not the Japanese drawing. The Restorations section says which one is in use. Nothing from the Japanese disc is shipped with Paper Cut or uploaded anywhere.

## Text packs

A text pack is a [mod](MODDING.md) with a `messages/` folder. Each message of the game has a **tag** (`name_party3`, `stg4_gra_25`...), and a pack replaces messages by tag, so it changes only what it lists and leaves everything else to the disc.

### How one is laid out

```
mods/
  my-delocalisation/
    mod.json
    messages/
      US/        English (the US build; the European build's English)
        global.txt
        win_00.txt
      DE/        German   (the European build, or the US build with German text)
      FR/        French
      SP/        Spanish
      IT/        Italian
      every.txt  a file at the top: for every language
```

`mod.json` says what the pack is:

```json
{
  "id": "com.example.my-delocalisation",
  "name": "My delocalisation",
  "version": "1.0",
  "author": "You",
  "description": "What it changes.",
  "games": ["G8ME01", "G8MP01"]
}
```

`games` lists the releases it is for: `G8ME01` (US), `G8MP01` (Europe), `G8MJ01` (Japan). A pack for the English text works on both the US and the European build.

Each `.txt` file holds messages in this form (lines before the first tag are comments):

```
These lines are comments.

[a_message_tag]
The text, with the game's own control tags:<wait 250> a new box after this.<p>
And it waits for A at the end.<k>
[another_tag]
More text.<k>
```

- A `[tag]` line starts a message; its text runs to the next tag line. The line break just before the next tag isn't part of the text, so don't put blank lines between messages unless you want a line break at the end.
- File names don't matter (any `.txt`, any number of them), but keeping the disc's names (`global.txt`, `gor_01.txt`...) makes a pack easy to find your way around.
- Write the files in UTF-8. On the US and European builds, letters are converted to the game's encoding (Latin-1); a character the game's font has no glyph for becomes `?`. On the Japanese build, text must be in Shift-JIS.
- **Numbers and names the game fills in** are written `%s` (a name, an item) and `%d`, `%04d` (numbers). Keep them exactly as the game's message has them, in the same order: Paper Cut keeps every one the game's own message has and shows any other `%` as it is, so a pack can never make the game read something that isn't there.
- The control tags (`<p>`, `<k>`, `<wait 250>`, `<shake>`, `<col ...>`, `<dkey>`...) are the game's; copy them from the template.

### Which language a pack's file is for

| Build | Language shown | Files used (the first that has the tag wins) |
|---|---|---|
| US | English | `messages/US/`, then `messages/*.txt` |
| US with Language > Text language (from the European disc) | German, French, Spanish or Italian | `messages/DE/` (or `FR`, `SP`, `IT`), then `messages/*.txt` |
| Europe | the language chosen in the game | `messages/US/`, `DE/`, `FR/`, `SP/` or `IT/`, then `messages/*.txt` |
| Japan | Japanese, or English mode | `messages/JP/` or `messages/EN/`, then `messages/*.txt` |

### Names

Characters' names are messages too: the partners' (`name_party1`...), the enemies' (in `global.txt`), the places'. A pack that renames someone changes their name everywhere the game shows it, menus and Tattles included, as long as it lists every message that names them (search the template for the old name).

One name is also an answer: Doopliss's, which you type on the name-entry screen in chapter 4. A pack that renames him gives the new answer as a message of its own, in each language it covers:

```
[pc_doopliss_name]
Newname
```

The name-entry screen takes up to 8 letters, from its own letters: keep the answer to those. Without `pc_doopliss_name`, the game's own answer stays.

### Making one

1. Start the game (any file), then Language > **Game text packs** > **Write the game's messages as a text pack template**. It reads every message of the languages this build shows from your disc and writes them to `mods/text-pack-template/` in your user folder: `messages/<LANG>/<file>.txt` and a `mod.json`. The template is turned **off** in the mod manager, so on its own it changes nothing.
2. Rename the folder (`my-delocalisation`), and set `id`, `name` and `author` in `mod.json` (and `games`, if it's for both US and European builds).
3. Change the messages you want. Then **delete every message you didn't change**: a pack should hold only its own text. That keeps it small, lets it sit beside other packs, and means it carries nothing of the game's.
4. Turn it on in the mod manager (Settings > Mods). After editing a file, Reload on the Mods tab reads it again; a changed message shows the next time the game prints it.

The Mods tab shows how many messages a pack replaces, and where two packs change the same message (the higher one in the list wins).

### Installing one

Drop the pack's folder, or a `.zip` of it, onto Paper Cut's window, or put it in the mods folder yourself (Settings > Mods > Open the mods folder), and turn it on in the mod manager. Put a delocalisation pack above other text mods if both change the same lines.

### What a text pack can't change

- **Text drawn in pictures**: signs, posters, the battle's "Great!" and "Lucky!" banners, logos. Those are textures: see [texture packs](MODDING.md#textures-texture-packs). Some signs the Japanese release had in Japanese are on every disc for its own language; the Restorations above don't switch those.
- **Paper Cut's own menus**: those are [language packs](LANGUAGE_PACKS.md).
- **Scenes and pictures** the US release changed: see Restorations above.

### Sharing

Share only what you wrote: a pack with the messages you changed. Don't share the template, or a pack still full of the game's own messages: that is the game's text, read from your disc for you.
