/* Goombella's field Tattle for Paper Cut's own NPCs (port/include/port/qol.h's
 * portTattleTag). The game's Tattle of an NPC is the message whose tag is the
 * NPC's name; Paper Cut's NPCs had none, so a Tattle of one (the badge
 * folder's Toad in Rogueport's plaza, with the "!" prompt inviting it) was the
 * game's red "No messages". Here each has its own, under its name; any other
 * without one gets "pc_tattle_npc", Goombella not placing them.
 * Set in the menu language (English online and in replays) with the
 * post-game's texts (postgame_extra_aurora.c), laid out as the game's speech. */
#include "port/postgame_extra.h"

static const PortPgText kTattles[] = {
    {"pc_tattle_npc",
     "Hmm... I don't know anything about this one, Mario.<p>Not even my notes from the University of Goom have "
     "everybody in them!<k>",
     1},
    /* a room with no Tattle of its own (party_kuribo.c's KURI_MAP) */
    {"pc_tattle_map",
     "Hmm... I can't tell you much about this place, Mario.<p>Let's look around and see what we find!<k>", 1},
    /* the badge folder (battlex_aurora.c): a Toad in Rogueport's plaza */
    {"pc_merchant",
     "That's a Toad who folds badges. Each one he sells is two or three badges folded into one.<p>You get all of "
     "their effects for one badge's BP. I have no idea how that works, but it's, like, totally genius.<k>",
     1},
    /* Rowf (pm64_badges_aurora.c) */
    {"pc_pm64_rowf",
     "That's Rowf. He runs the badge shop in Toad Town, back where your first adventure was.<p>He came all the "
     "way here with his badges. Now that's dedication!<k>",
     1},
    /* Paper Mario 64's story bosses (pm64_story_aurora.c), each while beating it frees its partner: by fight */
    {"pc_pm64_free0", "That's Jr. Troopa, from your first adventure.<p>Beat him to free Goombario.<k>", 1},
    {"pc_pm64_free1", "Those are the Koopa Bros., from your first adventure.<p>Beat them to free Kooper.<k>", 1},
    {"pc_pm64_free2", "That's Tutankoopa, from your first adventure.<p>Beat him to free Parakarry.<k>", 1},
    {"pc_pm64_free3", "That's Tubba Blubba, from your first adventure.<p>Beat him to free Bow.<k>", 1},
    {"pc_pm64_free4", "That's General Guy, from your first adventure.<p>Beat him to free Watt.<k>", 1},
    {"pc_pm64_free5", "That's Lava Piranha, from your first adventure.<p>Beat it to free Sushie.<k>", 1},
    {"pc_pm64_free6", "That's Huff N. Puff, from your first adventure.<p>Beat him to free Lakilester.<k>", 1},
    {"pc_pm64_free10", "The Koopa Bros. are back for more!<p>Beat them to free Bombette.<k>", 1},
    /* after the story (postgame_more_aurora.c, pit_whacka_aurora.c) */
    {"pg_quiz_boo",
     "That's a Boo who's been haunting this steeple's library for ages.<p>He says he's read every single book "
     "here. I bet his quiz is tough!<k>",
     1},
    {"pg_news_editor",
     "That's the editor of Rogueport's newsletter. He knows every bit of gossip in town.<p>And now he's after a "
     "new story... I wonder whose?<k>",
     1},
    {"pg_news_reader",
     "That's a Toad who reads the Rogueport newsletter.<p>Out here, it's the only way to hear what's going on in "
     "the big city.<k>",
     1},
    {"pg_token_seller",
     "That's one of Don Pianta's guys. He sells Pianta tokens for coins.<p>Three coins a token... I wouldn't "
     "call that a bargain, Mario.<k>",
     1},
    {"pg_whacka",
     "It's Whacka! He must have dug all the way down here.<p>He looks way tougher than he did on Keelhaul Key. "
     "Be careful, Mario!<k>",
     1},
    /* the remake's merchants (remake_maps_pc.c) */
    {"rm_charlieton",
     "That's Charlieton, the salesman. He sets up shop anywhere he can find customers.<p>His prices are, like, "
     "way too high, but he has stuff you won't find anywhere else.<k>",
     1},
    {"rm_toad",
     "That's a Toad who sells supplies all the way back here.<p>Smart! Anyone who makes it this far probably "
     "needs a Mushroom or two.<k>",
     1},
};

void portNpcTattlesRegister(void) {
    portPgAddTexts(kTattles, (int)(sizeof(kTattles) / sizeof(kTattles[0])));
}
