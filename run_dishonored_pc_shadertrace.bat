@echo off
rem 2026-11-12 (ahead-of-time shaders): the PC build with the creator of 25 shaders logged. Play normally: aim Blink at
rem the sky, use Dark Vision, play a bit in a level. Each listed shader logs one "[shader-trace] PS/VS <hash> made by ..."
rem line in out\pc\dishonored.log when the game creates it; any other missing shader logs its capture with the same chain.
call "%~dp0run_dishonored_pc.bat" %* --dh_shader_trace=0071BA8719AB575F,01CC690EC98CE2E6,17E0633C9153414F,190154032B8ECABD,2917F30B5E63ED40,2A707ADADC338C92,2EBD578E8F872029,341D117B3569B6F4,39415BA051E82271,463DDFD00A775DC4,5202C3E7CBE569A1,7264AD7D28240E13,83F82C4D63500295,AB934AB71FC3AC56,AFF626BF234CD409,B52DDD6C5064FFC8,B828F3CE8722F71A,BF7F8FF07CD37BBD,C943224A859D1062,D3C5B5081B9573B5,EB425F78D5755585,EF3080899768A58C,EF636C44A1EB6CA3,FB47DD7A4A5E787E,9D239E4008EAD744
