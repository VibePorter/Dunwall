"""Adds the PORT_GAME switch to app/CMakeLists.txt and app/cmake/mgr_renut_gpu.cmake (one-time port step)."""
p = 'app/CMakeLists.txt'
s = open(p, encoding='utf-8').read()
old_head = """        target_sources(sa2 PRIVATE
            me2_src/me2_hooks.cpp"""
new_head = """        # Dishonored (2026-11-08): the ME2 game code below patches ME2's addresses, so it is only built for ME2.
        # Dishonored keeps the generic app files (the second list) and stubs what they need (dishonored_src/).
        if(PORT_GAME STREQUAL "me2")
        target_sources(sa2 PRIVATE
            me2_src/me2_hooks.cpp"""
assert s.count(old_head) == 1
s = s.replace(old_head, new_head)
old_tail = """            me2_src/me2_sa2_stubs.cpp
            src/sa2_capture.cpp"""
new_tail = """            me2_src/me2_sa2_stubs.cpp)
        else()
            target_sources(sa2 PRIVATE dishonored_src/dh_stubs.cpp)
        endif()
        target_sources(sa2 PRIVATE
            src/sa2_capture.cpp"""
assert s.count(old_tail) == 1
s = s.replace(old_tail, new_tail)
old_opt = """set(MGR_SOURCES"""
new_opt = """# Dishonored (2026-11-08): which game's own code is built. The tree came from Lazarus (Mass Effect 2); "me2" builds
# ME2's game code (app/me2_src, ME2's addresses), "dishonored" leaves it out.
set(PORT_GAME "dishonored" CACHE STRING "Game-specific code to build: dishonored or me2")
set_property(CACHE PORT_GAME PROPERTY STRINGS dishonored me2)
message(STATUS "port: game code for ${PORT_GAME}")

set(MGR_SOURCES"""
assert s.count(old_opt) == 1
s = s.replace(old_opt, new_opt)
open(p, 'w', encoding='utf-8', newline='\n').write(s)

p = 'app/cmake/mgr_renut_gpu.cmake'
s = open(p, encoding='utf-8').read()
old = """    # ME2 (2026-10-27): the guest occlusion queries' hooks"""
i = s.index(old)
j = s.index('me2_frame_diag.cpp")', i) + len('me2_frame_diag.cpp")')
block = s[i:j]
# The three ME2 files end the source list: close the list before them and add them for ME2 only.
s = s[:i].rstrip() + ')\nif(PORT_GAME STREQUAL "me2")\n    target_sources(mgr_renut_gpu PRIVATE\n' + block + '\nendif()' + s[j:]
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
