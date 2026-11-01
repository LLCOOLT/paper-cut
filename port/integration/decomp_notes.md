# Notes for the decomp, found while integrating

- include/driver/mapdrv.h `MapAnimJoint.nameRef` ("probably the joint name, not read"): it is read. It is
  the map object name, a relocated `const char*`: mapdrv.c walks the joint animations through MapAnimList,
  whose items are MapAnimNm, and uses MapAnimNm.name to find the object. With the name missing, map
  animations (e.g. tou_12's fly, anm_hae) do not move their objects.
- libs/dolsdk2004/include/charPipeline/texPalette.h: the four typedefs (CLUTHeader, TEXHeader, TEXDescriptor,
  TEXPalette) are anonymous structs; the port gave them tags (no layout or code change) so its decoder
  generator can find them. Upstream could do the same.
- include/map/sharin.h `wheelSize`/`speedSource`: EvtWord (as `direction` is EvtFloat) would match how the
  scripts read the table (READ_INT4 steps one script word); s32 on GameCube either way.
- include/evt/evt_yuugijou.h `evtYuuSetWindowEvt(s32)`: the definition takes EvtWord.
- Headers that declare a function its source file defines `static` (MWCC and clang in MSVC mode accept it;
  clang for Linux, macOS, iOS and Android rejects "static declaration follows non-static declaration"):
  include/driver/camdrv.h camLetterBox; include/eff/eff_batfukidashi.h, eff_break.h, eff_energy.h,
  eff_hibashira.h, eff_indirect.h, eff_irekaeru.h (each effect's Main/Disp); include/evt/evt_door.h door_entry;
  include/evt/evt_twoddisp.h twoddisp_main; include/mario/mario.h marioMakeDispDir, marioRearAnime,
  marioDispBlur, marioDisp; include/win/win_main.h itemUseFunc. Either drop them from the headers (with a static
  forward declaration in the source where it is used before its definition) or make them non-static.
- Static functions declared for every version but defined in one version's block (harmless):
  battle_stage_object.c _disp_escape_wall_message (defined for JP and the demos only).
