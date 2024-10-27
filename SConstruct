#gcc -lasound -laubio -Wall -Wl,-rpath,"/usr/local/lib" -I /usr/local/include -L /usr/local/lib  test.c

env = Environment()
env.Append(LINKFLAGS=['-Wl,-rpath,"/usr/local/lib"', '-Wall', '-lm'])
env.Append(CPPFLAGS=['-fPIC', '-g', '-O2', '-Wall', '-Wextra', '-Werror'])


srcs = Split('''
    common/led_main.c
    common/common_source.c
    common/fire_source.c
    common/perlin_source.c
    common/color_source.c
    common/chaser_source.c
    common/morse_source.c
    common/disco_source.c
    common/xmas_source.c
    common/ip_source.c
    common/source_manager.c    
    common/colours.c
    common/listener.c
    common/ini.c
    common/base64.c
    common/game_source.c
    game/controller.c
    game/game_object.c
    game/moving_object.c
    game/player_object.c
    game/stencil_handler.c
    game/input_handler.c
    game/pulse_object.c
    game/callbacks.c
    common/rad_game_source.c
    rad_game/sound_player.c
    rad_game/rad_input_handler.c
    rad_game/oscillators.c
    rad_game/ddr_game.c
    rad_game/nonplaying_states.c
    common/m3_game_source.c
    m3_game/m3_game.c
    m3_game/m3_input_handler.c
    m3_game/m3_field.c
    m3_game/m3_players.c
    m3_game/m3_bullets.c
    common/paint_source.c
''')


env.Program(srcs, LIBS=['asound', 'aubio', 'zmq', 'ws2811'], LIBPATH=['/usr/local/lib','/home/pi/rpi_ws281x'], CPPPATH=['/home/pi/rpi_ws281x', 'include'])

