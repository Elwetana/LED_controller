# Programs for controlling ws281x LEDs

This project started when I wanted to install some LED lighting in a gazebo in our garden. 
I wanted to be able to control the colour and intensity of the LEDs and I ordered some 
individually addressable LEDs, ws2813. I wanted to create a program that would control
these LEDs and create some nice ambient lighting. These are first two modes, called
EMBERS (fire ambient) and PERLIN (sky ambient). The former uses a 'simulation' of burning
embers and the latter simple one-dimensional Perlin noise.

I then added a very simple mode COLOR that can get a specific colour via zmq interface from
the HTTP server, and two modes that are really custom, CHASER shows a chasing animation 
(useful to gauge the refresh rate) and MORSE that can display messages in Morse code on
the LEDs.

Finally, I created a different project with sound input, where LEDs blink to the rhythm
of music from analogue input (you need extra sound card on Raspberry Pi for that). The
colour of LEDs depends on tempo, dominant frequency and phase of the beat.

The colours used by the application are in the `config` file, the interpretation varies
slightly in different modes, but basically its hex colours that are end points in a 
gradient and how many steps should be generated in between. So e.g. `0x00001 10 0x0000ff
5 0x008888` generates 10 steps gradient from black to blue (when making gradients from
black or white, it's better to give the program a hint about gradient path by using a 
number that's not pure black or white), followed by five step gradient from blue to dark
cyan.

## Requirements

This project has the following requirements:

* RPI WS281x library (https://github.com/jgarff/rpi_ws281x)
* Zero MQ library (https://zeromq.org/)

For remote control (using Python HTTP server) you also need my other repo:

* LED programs (https://github.com/Elwetana/LED_programs)

For "disco" function (LEDs that blink to the rhythm of music) there are additional dependencies:

* Aubio (https://aubio.org/) 
* ALSA library (https://alsa-project.org/wiki/Main_Page)
* You also need sound card with analogue input (I used HiFiBerry DAC+ ADC: https://www.hifiberry.com/shop/boards/hifiberry-dac-adc/)

## Installing requirements

RPI WS281x and Aubio must be downloaded and compiled. ZeroMQ and ALSA are in Raspbian depots.

### WS281x

`git clone https://github.com/jgarff/rpi_ws281x.git`

This library uses `scons`:

`sudo apt-get install scons`

then just go to the git directory (if you do it in your home direcotry, it will be `~/rpi_ws281x`) and run:

`scons`

### Aubio

`git clone git://git.aubio.org/git/aubio`

Then you need to apply aubio.patch that you find in this repository.

Then install and run `waf` using this guide: https://aubio.org/manual/latest/installing.html

### ZeroMQ

`sudo apt-get install libzmq3-dev`

### ALSA

`sudo apt-get install libasound2-dev`

## Installing and compiling

Clone this repository: 

`git clone https://github.com/Elwetana/LED_controller.git`

Change into the new directory:

`cd ~/LED_Controller`

Run `scons` to compile:

`scons`

Then you can run the application:

`sudo led_main -c -s <MODE> -n <number_of_leds> -p <strip_type> -g <GPIO_pin>`

You have to run it as a root because access to LED hw requires root privileges.

If you want the program to start on boot, modify and install the included .service file:

`sudo cp led_lights.service /etc/systemd/system/`\
`sudo systemctl start led_lights.service`\
`sudo systemctl enable led_lights.service`

If you want to use HTTP server to control what mode is used, get the other repository
and run the Python powered server there.

## The tutorials I followed:

* http://equalarea.com/paul/alsa-audio.html
* https://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=4422&lngWId=3
* http://zguide.zeromq.org (if every library had a tutorial like that, what a wonderful world we'd be living in!)
