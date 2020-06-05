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
    source_manager.c    
    colours.c
    listener.c
''')


env.Program(srcs, LIBS=['asound', 'aubio', 'zmq', 'ws2811'], LIBPATH=['/usr/local/lib','/home/pi/rpi_ws281x'], CPPPATH=['/home/pi/rpi_ws281x'])

