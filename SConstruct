#gcc -lasound -laubio -Wall -Wl,-rpath,"/usr/local/lib" -I /usr/local/include -L /usr/local/lib  test.c

env = Environment()
env.Append(LINKFLAGS=['-Wl,-rpath,"/usr/local/lib"', '-Wall', '-lm'])
env.Append(CPPFLAGS=['-fPIC', '-g', '-O2', '-Wall', '-Wextra', '-Werror'])


srcs = Split('''
    led_main.c
    common_source.c
    fire_source.c
    perlin_source.c
    color_source.c
    chaser_source.c
    morse_source.c
    disco_source.c
    xmas_source.c
    source_manager.c    
    colours.c
    listener.c
    ini.c
    game_source.c
    game/controller.c
    game/game_object.c
    game/moving_object.c
    game/player_object.c
    game/stencil_handler.c
    game/input_handler.c
    game/pulse_object.c
    game/callbacks.c
    rad_game_source.c
    rad_game/sound_player.c
    rad_game/rad_input_handler.c
    rad_game/oscillators.c
    rad_game/ddr_game.c
    rad_game/nonplaying_states.c
''')


env.Program(srcs, LIBS=['asound', 'aubio', 'zmq', 'ws2811'], LIBPATH=['/usr/local/lib','/home/pi/rpi_ws281x'], CPPPATH=['/home/pi/rpi_ws281x', 'include'])

