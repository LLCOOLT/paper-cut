"""Super Luigi, chapter 3's text (docs/super_luigi/ch3.md): writes
port/src/aurora/superluigi_ch03_text_aurora.c from the lines below.

Each entry is a tag and its text. A line's paragraphs (split by "|") are
boxes: each is wrapped at WIDTH characters, three lines to a box, and ends
with <k>; boxes after the first open with <p>. Entries starting with "=" are
written as they are (system boxes, choices, names).

    python tools/port/sl_ch3_text.py
"""
import os
import textwrap

WIDTH = 30

T = [
    # 1. arrival
    ("sl_c3_arrive_01", "=<system>\nCircuit Break Island.\nYou hear it before you see it.\n<k>"),
    ("sl_c3_arrive_02", "Whoa! What IS that racket? It sounds like a thousand blenders full of gravel!"),
    ("sl_c3_arrive_03", "Engines, Sir Luigi. Many engines. Plumpbelly has but one, and we keep it in a museum."),
    ("sl_c3_arrive_04", "Loud. I like it."),
    ("sl_c3_arrive_05", "The compass pointed right here. Somewhere in all that noise is a piece of it.|Probably. I hope. Let's go, team! ...Carefully!"),
    ("sl_c3_sign_31", "=<system>\nCircuit Break Island.\nPopulation: loud.\n<k>"),
    # 2. the awards
    ("sl_c3_awards_01", "LADIES! GENTLEMEN! KOOPAS OF ALL AGES! For the THIRTY-FIRST day in a ROW,|the winner of the Circuit Break Grand Prix, and therefore KING OF THIS ISLAND|until tomorrow's race, issss..."),
    ("sl_c3_awards_02", "KIIIIING REVVOOOOO!"),
    ("sl_c3_awards_03", "That... that's... THAT'S A PIECE OF THE MARVELOUS COMPASS! On a TROPHY!|Oh, I think I need to sit down."),
    ("sl_c3_awards_04", "Steady, sir. Steady."),
    ("sl_c3_awards_05", "Thank you, thank you. Please, hold your applause. No, don't. Keep it coming."),
    ("sl_c3_awards_06", "As your king, my first decree is the same as yesterday's: everybody say \"Long live King Revvo!\""),
    ("sl_c3_awards_07", "=<system>\n<dynamic 3>Long live King Revvo!</dynamic>\n<k>"),
    ("sl_c3_awards_08", "My second decree: that was great, do it again tomorrow."),
    ("sl_c3_awards_09", "You're new, huh? Here's how it works. Every day there's a race.|Whoever wins is king of the island till the next one.|The king gets the crown, the cape, the trophy, and the parking space."),
    ("sl_c3_awards_10", "Revvo's won every race for a month. Nobody can beat him. Mostly 'cause nobody can get near him.|His karts have missiles. And bazookas. And a smaller bazooka for the bazooka."),
    ("sl_c3_awards_11", "Missiles?! In a RACE?! What happened to good, clean, family karting?!"),
    ("sl_c3_awards_12", "Anything goes on Circuit Break, mister. It's in the rules. The rules are just that one sentence."),
    ("sl_c3_awards_13", "OK. OK, OK, OK. So the compass piece is on the trophy, and the trophy goes to the winner.|So all we have to do is..."),
    ("sl_c3_awards_14", "Win."),
    ("sl_c3_awards_15", "A race. In a war machine. Against a king with a bazooka for his bazooka."),
    ("sl_c3_awards_16", "...Let's just go see how you sign up. Just to look! Looking is free!"),
    # 3. Race HQ
    ("sl_c3_hq_01", "Welcome to Race HQ! Spectator tickets are on the left, autographs are on the right,|and crying is outside."),
    ("sl_c3_hq_02", "Hi! I'd like to enter the next race, please. I'm Luigi.|Super Luigi, if you want the whole thing. Most people don't."),
    ("sl_c3_hq_03", "A rookie! How exciting. Very rare. Mostly they don't come back."),
    ("sl_c3_hq_04", "To start the Grand Prix you need three things. One: a kart.|Two: a licensed pit mechanic. Three: a racing license."),
    ("sl_c3_hq_05", "You get a racing license by winning the Bumper Bouts in the Stadium. Five wins and you're licensed.|Director Gasket runs those, through the door and down Pit Lane."),
    ("sl_c3_hq_06", "Bumper Bouts? Like... bumper cars? That sounds fun!"),
    ("sl_c3_hq_07", "Bumper as in \"you get bumped.\" By Hammer Bros. In a ring.|Here's your rookie badge! Don't lose it, we don't have another."),
    ("sl_c3_hq_got_pass", "=<system>\nLuigi got the <col 0000ffff>Rookie Pass</col>!\n<k>"),
    ("sl_c3_bout_medic", "=<system>\nThe ring medics patch Luigi up.\n<k>"),
    ("sl_c3_hq_stadium_shut", "=<system>\nAudience doors. Shut till the Grand\nPrix. Fighters go in from Pit Lane.\n<k>"),
    ("sl_c3_hq_locked", "=<system>\nStaff and racers only.\nCheck in at the desk first.\n<k>"),
    ("sl_c3_hq_tutor", "Oh, and that chest behind me? Lost and found. Been here since the first race. Nobody's claimed it.|You look like you need it."),
    ("sl_c3_hq_tutor_no", "Hands off the lost and found, rookie. Win a bout first. Then we'll talk."),
    ("sl_c3_hq_stamp", "Oh! You came back! That's never happened!|One Racing License, stamped. Good luck tomorrow. You'll need it. Everybody does."),
    ("sl_c3_npc_checkers_01", "The Director's office is down Pit Lane. Through the door on your left. Or your right. It's a loop."),
    ("sl_c3_npc_checkers_02", "Two wins? For a rookie? I'll have to find a bigger rubber stamp."),
    ("sl_c3_npc_checkers_03", "I always said you had it in you. I said it this morning. Quietly. To myself."),
    ("sl_c3_npc_reporter_00", "No comment? I didn't ask anything yet. Come back when you've been bumped."),
    ("sl_c3_npc_reporter_01", "Rookie! How do you feel?"),
    ("sl_c3_npc_reporter_02", "Rookie! Is it true you cried?"),
    ("sl_c3_npc_reporter_03", "Rookie! Is it true you're going to fight the KING?"),
    ("sl_c3_npc_reporter_04", "Luigi! Can I have an autograph?"),
    # 4. the Director
    ("sl_c3_director_01", "Step right up, step right up! Fresh meat for the Bumper Bouts!|Ohhh, look at those knees knocking, the crowd's gonna LOVE you!"),
    ("sl_c3_director_02", "Rules are simple! You go in the ring, you fight whoever I put in there, the last one standing wins!|Win five and you're licensed to race! Lose and you go home with a lovely bruise!"),
    ("sl_c3_director_03", "Do I get to pick who I fight?"),
    ("sl_c3_director_04", "Ha! HA! No."),
    ("sl_c3_director_05", "First bout's open now. But listen, rookie: no kart, no license, no race.|And no mechanic will touch a rookie's kart. You'll want to find somebody desperate.|Try the last garage on Pit Lane. Bring earplugs. And a treat. He bites."),
    ("sl_c3_director_mech", "Mechanic first, rookie! Last garage on Pit Lane! I'll keep the ring warm!"),
    ("sl_c3_director_bout1", "Bout one, rookie! The crowd's warmed up!"),
    ("sl_c3_director_torque", "Two down! And somebody's waiting for you out in Pit Lane, looking grumpy. Grumpier."),
    ("sl_c3_director_kart", "Whoa, whoa! The King's Bout is for racers! No kart, no King! Go get your little go-kart built!"),
    ("sl_c3_director_king", "This is it, rookie! The fifth bout! The KING'S BOUT! Nobody's ever won it!"),
    ("sl_c3_director_exhib", "Exhibition bouts! No license at stake! Just glory! And prizes!"),
    ("sl_c3_director_done", "You've beaten everybody I've got, kid. Go race! Shoo!"),
    ("sl_c3_director_ask", "=So! Fight the next bout?\n<o>"),
    ("sl_c3_director_yesno", "=<select 0 1 0 40>\nLet's go!\nNot yet"),
    ("sl_c3_director_go", "=<p>\nThat's the spirit! Out to the ring with you!\n<k>"),
    ("sl_c3_director_later", "=<p>\nSuit yourself! The bruises will wait!\n<k>"),
    ("sl_c3_cabinet_empty", "=<system>\nFiles. Lots of files. All of them\nabout King Revvo.\n<k>"),
    ("sl_c3_vent_open", "Vent's open. After you. You're skinnier."),
    # 5. Torque
    ("sl_c3_torque_meet_01", "Closed."),
    ("sl_c3_torque_meet_02", "But the door was open."),
    ("sl_c3_torque_meet_03", "Door's broken. Garage is closed."),
    ("sl_c3_torque_meet_04", "I'm looking for a mechanic. I'm entering the Grand Prix!"),
    ("sl_c3_torque_meet_05", "You. Racing. With what?"),
    ("sl_c3_torque_meet_06", "That's the other thing. I was hoping you'd have a kart."),
    ("sl_c3_torque_meet_07", "...No."),
    ("sl_c3_torque_meet_08", "Sir, there is plainly a kart under that sheet. It has wheels. They are sticking out."),
    ("sl_c3_torque_meet_09", "That's not a kart. That's the Girl. Big Green 01. Fastest thing I ever built.|Never been raced. Never will be. Not by a rookie with a mustache."),
    ("sl_c3_torque_meet_10", "Hey, the mustache has been in a LOT of adventures!"),
    ("sl_c3_torque_meet_11", "Anyway she's missing parts. Somebody cleaned me out last week. Spark plug, steering wheel, gearbox. Gone."),
    ("sl_c3_torque_meet_12", "Who."),
    ("sl_c3_torque_meet_13", "Who do you think? Revvo's crew. Can't have a kart faster than the King's on the island, can we."),
    ("sl_c3_torque_meet_14", "So if we found your parts..."),
    ("sl_c3_torque_meet_15", "Then I'd have my parts back. Good day."),
    ("sl_c3_npc_torque_under", "Still closed."),
    ("sl_c3_tarp_look", "=<system>\nSomething big under a green\ntarp. Its wheels stick out.\n<k>"),
    ("sl_c3_kart_look", "=<system>\nBig Green 01. Low, bright green,\nand very, very shiny.\n<k>"),
    # 6. the bouts
    ("sl_c3_bout1_intro", "In the red corner, a team that NEVER misses a pit stop, because they ARE the pit stop...|THE LUG NUTS!|And in the green corner, a ROOKIE who signed up an HOUR AGO... LUIGI!"),
    ("sl_c3_bout2_intro", "They're spiky! They're from the sky! They are absolutely not allowed in the parking lot!|It's... THE SPIKE STRIP!"),
    ("sl_c3_bout3_intro", "They're shady! They're shelly! They hold every grudge they've ever met! It's... THE SHELL SHOCKERS!"),
    ("sl_c3_bout4_intro", "Stack 'em up! They're tall! They're prickly! They're three at once! It's... THE POKEY PISTONS!"),
    ("sl_c3_boutE1_intro", "An exhibition bout, folks! Two tons of solid iron and no manners! THE IRON SIDES!"),
    ("sl_c3_boutE2_intro", "Exhibition time! Red, spiky, and very hard to jump on! THE RED SHELLS!"),
    ("sl_c3_boutE3_intro", "The last exhibition! Hold on to your wallets, folks! It's... THE WRECKING CREW!"),
    ("sl_c3_bout_lost", "Ooooh! The rookie's DOWN! Somebody bring a stretcher! And a smaller stretcher for the ego!|Come back when you've had a nap, rookie! The ring will still be here!"),
    ("sl_c3_bout_won", "AND THE ROOKIE WINS! Somebody check the scoreboard! Somebody check MY EYES!"),
    ("sl_c3_boutE_won", "The rookie takes the exhibition! And the prize! And my breath away!"),
    ("sl_c3_boutE1_prize", "=<system>\nLuigi got the <col 0000ffff>Charge</col> badge!\n<k>"),
    ("sl_c3_boutE2_prize", "=<system>\nLuigi got a <col 0000ffff>Star Piece</col>!\n<k>"),
    ("sl_c3_boutE3_prize", "=<system>\nLuigi got <col 0000ffff>30 coins</col>!\n<k>"),
    # 6'. Torque joins
    ("sl_c3_torque_join_01", "Watched your bout on the garage TV."),
    ("sl_c3_torque_join_02", "You did?! How was I?"),
    ("sl_c3_torque_join_03", "Bad. Jumped too early. Hammer swing like a wet noodle."),
    ("sl_c3_torque_join_04", "But you didn't run. Most rookies run."),
    ("sl_c3_torque_join_05", "...I'll make you a deal. You get my parts back from Revvo's crew. I'll get the Girl running.|And I'll stand in your pit. But if you scratch her, I'll scratch you."),
    ("sl_c3_torque_join_06", "Deal! Wait. How much scratching are we talking about?"),
    ("sl_c3_torque_join_07", "Name's Torque. Let's go."),
    ("sl_c3_torque_joined", "=<system>\n<col 0000ffff>Torque</col> joined your party!\n<k>"),
    # 7. Tune-up
    ("sl_c3_tutorial_01", "That shutter's been jammed since Tuesday. Nobody on this island can fix anything. Watch."),
    ("sl_c3_tutorial_03", "Stand by the shutter. Press the button. I'll do the rest. You do the tapping."),
    ("sl_c3_tutorial_04", "There. Wasn't hard. Everybody here just hits things."),
    ("sl_c3_tutorial_05", "I hit things too! With a hammer!"),
    ("sl_c3_tutorial_06", "I know. I saw."),
    ("sl_c3_shutter_jammed", "=<system>\nIt's jammed halfway. Too low to walk\nunder, too high to step over.\n<k>"),
    ("sl_c3_suite_locked", "=<system>\nBy order of the King: KEEP OUT.\nThis means you. Yes, you.\n<k>"),
    # 8-10. the parts
    ("sl_c3_plug_got", "=<system>\nYou got the <col 0000ffff>Spark Plug</col>!\n<k>"),
    ("sl_c3_plug_01", "One down."),
    ("sl_c3_lift_fixed", "Lift's live. Step on. Mind the edge."),
    ("sl_c3_gearbox_got", "=<system>\nYou got the <col 0000ffff>Gearbox</col>!\n<k>"),
    ("sl_c3_gearbox_01", "That's my gearbox. The Reverso-Matic. Hand-built. Six forward gears, one reverse."),
    ("sl_c3_gearbox_02", "Only one reverse? Is that... enough reverse?"),
    ("sl_c3_gearbox_03", "You won't need reverse."),
    ("sl_c3_wheel_01", "Hey, it's the rookie. Heard you're lookin' for a steering wheel."),
    ("sl_c3_wheel_02", "Funny thing. The King's got a new hat rack in his suite. Shaped like a steering wheel. Real green. Real shiny."),
    ("sl_c3_wheel_03", "Shame nobody's allowed in the King's Suite. Door's locked. Has been since forever."),
    ("sl_c3_wheel_04", "Lug. Stop telling him things."),
    ("sl_c3_wheel_got", "=<system>\nYou got the <col 0000ffff>Steering Wheel</col>!\n<k>"),
    ("sl_c3_wheel_05", "Well, well. A rookie, in my suite, touching my hat stand."),
    ("sl_c3_wheel_06", "This is a steering wheel! It's Torque's! You stole it!"),
    ("sl_c3_wheel_07", "\"Stole.\" Such an ugly word. I prefer \"won in advance.\" Everything on this island is mine eventually.|I just save time."),
    ("sl_c3_wheel_08", "But fine. Keep the wheel. Build your little green go-kart.|You'll still have to beat me in the ring before you ever see a starting grid."),
    ("sl_c3_wheel_09", "Your fifth bout is with me, rookie. The King's Bout. Nobody's ever won it. Nobody's ever finished it awake."),
    ("sl_c3_wheel_10", "Leave the door open, rookie. I like a draught."),
    ("sl_c3_duct_echo", "...Smells like feet."),
    ("sl_c3_bench", "Found it in the engine. Don't ask."),
    ("sl_c3_muffler_got", "=<system>\nYou got the <col 0000ffff>Turbo Muffler</col>!\n<k>"),
    ("sl_c3_muffler", "Muffler. Doesn't make her faster. Makes her louder. Loud is faster. Psychologically.|I'll fit it. Gives you ten more seconds on the Hourglass."),
    # 11. Big Green 01
    ("sl_c3_kart_00", "Spark plug. Gearbox. Wheel. Give me a minute. Don't touch anything."),
    ("sl_c3_kart_01", "There she is."),
    ("sl_c3_kart_02", "Oh, she's BEAUTIFUL! Look at her! She's green! I'm green! We're both green!"),
    ("sl_c3_kart_03", "Don't get attached. Get in."),
    ("sl_c3_kart_04", "Practice course is open to licensed crews and their karts. Take her round. Learn how she corners.|Then go win your license."),
    # 12. the King's Bout
    ("sl_c3_boss_01", "Folks, this is it! This is what you came for! In the green corner, the rookie who WOULD NOT GO HOME...|LUIGI!"),
    ("sl_c3_boss_02", "And in the gold corner! Thirty-one wins! Thirty-one crowns! One very large ego!|He's the Sultan of Speed! The Duke of Drift! He's... KING... REEEEVVOOOOO!"),
    ("sl_c3_boss_03", "Thank you. I've been called worse. Not by anyone who's still on this island."),
    ("sl_c3_boss_04", "Lug! Nut! Let's show the rookie what a pit crew is for."),
    ("sl_c3_boss_05", "Impossible. I... lost? In MY ring?"),
    ("sl_c3_boss_06", "IT'S OVER! The rookie WINS the King's Bout! He's LICENSED! He's IN THE RACE!"),
    ("sl_c3_boss_07", "Fine. Fine! Race tomorrow, rookie. And as King, I set the grid. You start LAST. Behind everyone.|In front of the wall."),
    ("sl_c3_boss_08", "Let's see your little green kart get past seven of the fastest machines in the world."),
    ("sl_c3_boss_license", "=<system>\nYou got the <col 0000ffff>Racing License</col>!\n<k>"),
    ("sl_c3_boss_bazooka", "=King Revvo pulls a bazooka out of\nhis shell!\n<k>\n<p>\n\"He's loading the bazooka! Rookie,\nMOVE!\"\n<k>"),
    ("sl_c3_boss_revving", "=Revvo is revving up!\n<k>"),
    ("sl_c3_boss_spinout", "=Revvo spins out! No Victory Lap\nthis time!\n<k>"),
    ("sl_c3_boss_pitstop", "=\"A PIT STOP! In the MIDDLE OF A BOUT!\nIs that legal?! ...It's legal!\"\n<k>"),
    ("sl_c3_boss_up", "=Nobody flips the King!\n<k>"),
    ("sl_un_revvo", "=King Revvo"),
    ("sl_un_lug", "=Lug"),
    # 13. the night before
    ("sl_c3_night_01", "Hey, Torque? Why didn't you ever race her yourself?"),
    ("sl_c3_night_02", "Can't reach the pedals."),
    ("sl_c3_night_03", "...And I build things. I don't drive things. A driver's gotta be a little crazy.|Gotta not know when to be scared."),
    ("sl_c3_night_04", "Oh. Well, I'm scared of almost everything. So I'm not sure that's me."),
    ("sl_c3_night_05", "Yeah. That's what worries me."),
    ("sl_c3_night_06", "Get some sleep. Shifter's on the left. Forward's up."),
    ("sl_c3_night_morning", "=<system>\nRace day.\n<k>"),
    ("sl_c3_night_07", "Big day. Save your game, rookie. The track gate's at the far side of the Stadium."),
    # 14. the start
    ("sl_c3_gate_practice", "The track's this way. Licensed crews only. Want to take her round?"),
    ("sl_c3_gate_race", "Race day, rookie! Your grid spot's waiting. Right at the back."),
    ("sl_c3_gate_tomorrow", "Race is tomorrow. Go get some sleep. Your mechanic's waiting in his garage."),
    ("sl_c3_gate_yesno", "=<select 0 1 0 40>\nTo the track\nNot yet"),
    ("sl_c3_grid_sign", "=<system>\nPractice course.\nLicensed crews only.\n<k>"),
    ("sl_c3_grid_official", "=The course is open. Take a lap? Or head back in?\n<o>"),
    ("sl_c3_grid_choice", "=<select 0 2 0 40>\nDrive a practice lap\nBack to the Stadium\nStay here"),
    ("sl_c3_grid_onward", "She's running. Off you go! The course is that way!"),
    ("sl_c3_grid_01", "It's RACE DAY on Circuit Break! Thirty thousand fans! Eight karts! One crown!|And one rookie in a little green kart at the very, very back!"),
    ("sl_c3_grid_02", "Enjoy the view, rookie! It's all exhaust from here!"),
    ("sl_c3_grid_03", "=<system>\n(Torque, over the radio:)\nLeft foot off the brake. Right foot\ndown. Forward's up. Don't think.\n<k>"),
    ("sl_c3_grid_04", "Don't think. Don't think. Don't think. Oh no, now I'm thinking about not thinking!"),
    ("sl_c3_grid_gas", "=Press <icon PAD_A 0.6 0 22 0> to hit the gas!"),
    ("sl_c3_crash_01", "=WAAAAAAAAAAAAAAAH!\n<k>"),
    ("sl_c3_crash_02", "..."),
    ("sl_c3_crash_03", "I... I've been doing this for twenty years, and I have NEVER... SEEN... ANYTHING LIKE THAT!|EVERY KART IS OUT! EVERY SINGLE ONE! EXCEPT..."),
    ("sl_c3_crash_04", "Is it over? Did I win? Did I die? Which one?"),
    ("sl_c3_crash_05", "GET... ME... OUT... OF... HERE!"),
    ("sl_c3_crash_06", "What did you DO to her?!"),
    ("sl_c3_crash_07", "I pushed the stick FORWARD! Like you said! Up is forward!"),
    ("sl_c3_crash_08", "...I put it in upside down."),
    ("sl_c3_crash_09", "Nobody tell anybody."),
    ("sl_c3_crash_10", "Folks, the rule book says the race ISN'T OVER until somebody finishes three laps!|And there's only ONE kart that can still move!|If the rookie can get that thing running and cross the line before the Royal Hourglass runs out,|he's your NEW KING!"),
    ("sl_c3_crash_11", "It can't move! It's upside down! It's ON FIRE!"),
    ("sl_c3_crash_12", "=<system>\n(Jerry:) It is very slightly\non fire.\n<k>"),
    ("sl_c3_crash_13", "Flip her. Tune-up. Now. You tap, I turn."),
    ("sl_c3_crash_14", "Stand by her and press the button. Fifteen seconds a go. Tap like you mean it."),
    ("sl_c3_crash_16", "She's running. Shifter's the right way up now. Go. GO!"),
    # 15. the Lone Lap
    ("sl_c3_lap_title", "=Circuit Break Grand Prix"),
    ("sl_c3_lap_01", "=He's OFF! The ONLY kart on the track!\nHe's in FIRST! He's also in LAST!\nHe's in EVERY position!"),
    ("sl_c3_lap_02", "=Through the Chicane of Chaos!\nWatch that wreckage, rookie!"),
    ("sl_c3_lap_03", "=Lap two! The King's crew are... are\nthey throwing WRENCHES?!\nThat's allowed! Everything's allowed!"),
    ("sl_c3_lap_04", "=FINAL LAP! The Hourglass is running\nlow! Can he do it?!"),
    ("sl_c3_lap_05", "=HE DOES IT! LUIGI WINS! LUIGI WINS\nTHE CIRCUIT BREAK GRAND PRIX!\n<k>\n<p>\nBY THREE LAPS! BY THE ENTIRE RACE!\n<k>"),
    ("sl_c3_lap_retry_01", "=<system>\n(Torque, over the radio:)\nAgain. Smoother.\n<k>"),
    ("sl_c3_lap_retry_02", "=<system>\n(Torque, over the radio:)\nYou're braking in the corners. Don't.\n<k>"),
    ("sl_c3_lap_retry_03", "=<system>\n(Torque, over the radio:)\nStop hitting the scrap. The scrap\nwas there first.\n<k>"),
    ("sl_c3_lap_retry_04", "=<system>\n(Torque, over the radio:)\nThe Hourglass resets. So do you. Go.\n<k>"),
    ("sl_c3_hud_hourglass", "=Royal Hourglass"),
    ("sl_c3_hud_clock", "=Medal Clock"),
    ("sl_c3_hud_lap", "=Lap"),
    ("sl_c3_result_race", "=Race Result: 1st (of 1)"),
    ("sl_c3_result_practice", "=Practice Lap"),
    ("sl_c3_result_time", "=Time"),
    ("sl_c3_result_dyn", "=<system>\n\n<k>"),
    ("sl_c3_medal_gold", "=<system>\nA <col c08000ff>Gold</col> lap! The track's prize:\na Shine Sprite!\n<k>"),
    ("sl_c3_medal_silver", "=<system>\nA <col 808080ff>Silver</col> lap! The track's prize:\na Star Piece!\n<k>"),
    ("sl_c3_practice_coins", "=<system>\nLuigi got 10 coins for finishing!\n<k>"),
    # 16. the crowning
    ("sl_c3_crown_01", "By the power vested in me by the one rule in the rule book...|I give you the KING OF CIRCUIT BREAK ISLAND, until tomorrow's race... KING LUIGIIIIII!"),
    ("sl_c3_crown_02", "=<system>\n<dynamic 3>Long live King Luigi!</dynamic>\n<k>"),
    ("sl_c3_crown_03", "King Luigi! Did you hear that, guys? KING! Me! Wait till I tell my brother!|He'll be SO... well, he won't believe it. But still!"),
    ("sl_c3_crown_04", "Enjoy it, rookie. One day. Tomorrow I'll be back on top."),
    ("sl_c3_crown_05", "Sure! You can have the crown back tomorrow. And the cape. And the parking space.|I just need the little shiny bit on the trophy. That's all I came for!"),
    ("sl_c3_crown_06", "...You did all this for the LID?"),
    ("sl_c3_crown_07", "It's a very important lid!"),
    ("sl_c3_crown_08", "=<system>\nYou got a piece of the\n<col 0000ffff>Marvelous Compass</col>!\n<k>"),
    # 17. the voice
    ("sl_c3_voice_01", "=<system>\n<center><col 6060a0ff>...Is someone there? Someone is coming.\nI can feel it, like a lamp left on\nin a window.</col></center>\n<k>"),
    ("sl_c3_voice_02", "=<system>\n<center><col 6060a0ff>Whoever you are... go east. Where the\nmusic never stops. And please...\ndon't give up.</col></center>\n<k>"),
    ("sl_c3_voice_03", "...Did you hear that? Guys? Did you hear her?"),
    ("sl_c3_voice_04", "Hear who."),
    ("sl_c3_voice_05", "The princess! Princess Eclair! It was her! It HAD to be her!|A voice like that... it was like... like a sunrise, if a sunrise could talk.|And it said \"don't give up\"! To ME!"),
    ("sl_c3_voice_06", "I heard only the crowd, sir. And Lakky. Lakky is always audible."),
    ("sl_c3_voice_07", "East. \"Where the music never stops.\" The compass says Jazzafrazz Town.|Princess, hang on! Super Luigi is on his way!"),
    # 18. Torque comes along
    ("sl_c3_torque_comes_01", "Hold it."),
    ("sl_c3_torque_comes_02", "Seven years I've been building engines. Never seen anybody drive like that.|Backwards, crash, flip, fire, win. Nobody'd believe it."),
    ("sl_c3_torque_comes_03", "Somebody's gotta follow you around and fix whatever you break next. Might as well be me."),
    ("sl_c3_torque_comes_04", "You mean it? You're coming? But what about the Girl?"),
    ("sl_c3_torque_comes_05", "She's retired. Undefeated. Best record on the island. One race, one win."),
    # 19. the tag
    ("sl_c3_tag_caption", "=As Torque tells it"),
    ("sl_c3_tag_01", "People ask me about the reverse start. Yeah. That was mine."),
    ("sl_c3_tag_02", "Rear-launch strategy. Took me six years to design. You go backwards,|you build up momentum off the wall, then you slingshot. Physics."),
    ("sl_c3_tag_03", "The other drivers? They panicked. Didn't understand what they were seeing.|Genius usually looks like an accident."),
    ("sl_c3_tag_04", "The shifter? Upside down on purpose. Element of surprise. Even the driver didn't know."),
    ("sl_c3_tag_05", "...Don't tell him that last part."),
    ("sl_c3_diary", "=Circuit Break Island crowned a new king for a day, and the Marvelous Compass had its fourth part.\n<k>\n<p>\nIts needle swung east, toward\nJazzafrazz Town. Big Green 01\nretired undefeated.\n<k>"),
    # the people around
    ("sl_c3_npc_fan_01", "The race is every day. The king is every day. The parking space is every day. It's a lot of days."),
    ("sl_c3_npc_fan_02", "You beat REVVO in the ring? Nobody's gonna believe that. I don't believe it and I was THERE."),
    ("sl_c3_npc_fan_03", "I knew it! I knew it all along! Backwards was the only way to win! Everybody knows that!"),
    ("sl_c3_npc_club_01", "Revvo Fan Club badges! Two coins! One coin if you say \"Long live King Revvo\"! Free if you mean it!"),
    ("sl_c3_npc_club_02", "Luigi Fan Club badges! I've ALWAYS been in the Luigi Fan Club. I'm the president. Since this morning."),
    ("sl_c3_npc_pianta_01", "Souvenirs! Revvo foam fingers! Revvo hats! Revvo... more Revvo! It's all Revvo!"),
    ("sl_c3_npc_pianta_02", "A rookie winning bouts? I'd better order some green foam fingers. Just in case. A small box."),
    ("sl_c3_npc_pianta_03", "Green foam fingers! Get your green foam fingers! I ordered a VERY big box! Always believed in you!"),
    ("sl_c3_npc_crew_01", "A rookie in Pit Lane? Somebody fetch the first aid kit. The big one."),
    ("sl_c3_npc_crew_02", "Two bouts, huh? Lucky. Luck runs out round here. Usually around bout three."),
    ("sl_c3_npc_crew_03", "You beat the King in his own ring? ...Doesn't matter. Nobody beats him on the TRACK."),
    ("sl_c3_npc_crew_04", "I saw it coming. Backwards! Classic! The oldest trick in the book. Page one."),
    ("sl_c3_npc_crew_11", "I'd give you a tip, rookie, but you won't be here long enough to use it."),
    ("sl_c3_npc_crew_12", "The Spike Strip didn't get you? Huh. Maybe you've got thick boots."),
    ("sl_c3_npc_crew_13", "Licensed? YOU? I need to sit down. Somebody bring me an oil drum."),
    ("sl_c3_npc_crew_14", "Every team on the island is fixing karts today. Except Torque. He's just smiling. It's terrifying."),
    ("sl_c3_npc_driver_01", "Look, the rookie came to see real karts. Take a good look. It's the closest you'll get."),
    ("sl_c3_npc_driver_02", "I was looking BACKWARDS! Who looks backwards in a RACE?!"),
    ("sl_c3_npc_driver_11", "Ticking? That's not me. That's my engine. ...Mostly my engine."),
    ("sl_c3_npc_driver_12", "My kart's in seven pieces. Eight if you count me."),
    ("sl_c3_npc_driver_21", "Revvo says you'll start behind the wall. Heh. Behind it."),
    ("sl_c3_npc_driver_22", "I saw the whole thing. Then a scoreboard sat on me. Then I saw nothing."),
    ("sl_c3_npc_blastard_01", "Welcome to Row B, home of the Blastards, the proudest team never to finish a race!|Wait... green kerchief... Jerry of Plumpbelly? THE Jerry? Of the Hizza business?"),
    ("sl_c3_npc_blastard_02", "The very same."),
    ("sl_c3_npc_blastard_03", "Those lockers at the end have been rattling for weeks. Something's back there. We'd check, but we'd blow it up."),
    ("sl_c3_npc_blastard_04", "Backwards! Into a wall! On purpose! You're a Blastard at heart, mister!"),
]


def wrap(text):
    if text.startswith("="):
        return text[1:]
    boxes = []
    for para in text.split("|"):
        lines = textwrap.wrap(para.strip(), WIDTH)
        for i in range(0, len(lines), 3):
            boxes.append("\n".join(lines[i:i + 3]) + "\n<k>")
    return "\n<p>\n".join(boxes)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = os.path.join(root, "port", "src", "aurora", "superluigi_ch03_text_aurora.c")
    tags = set()
    lines = [
        "/* Super Luigi, chapter 3's text (docs/super_luigi/ch3.md): our own words,",
        " * never the game's. GENERATED by tools/port/sl_ch3_text.py: edit the lines",
        " * there and run it. Luigi speaks as \"mario\", the partner as \"party\". */",
        "",
        "const char* const portSl3Text[] = {",
    ]
    for tag, text in T:
        assert tag not in tags, tag
        tags.add(tag)
        lines.append("    %s, %s," % (cstr(tag), cstr(wrap(text))))
    lines += ["    0,", "};", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", out, len(T), "messages")


if __name__ == "__main__":
    main()
