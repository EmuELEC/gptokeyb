# gptokeyb

Gamepad to Keyboard/mouse/xbox360(gamepad) emulator

Based on code by: [Kris Henriksen](https://github.com/krishenriksen/AnberPorts/tree/master/AnberPorts-Joystick) and fake Xbox code from: https://github.com/Emanem/js2xbox   
Modified to use SDL2 by: [Nikolai Wuttke](https://github.com/lethal-guitar) & [Shanti Gilbert](https://github.com/shantigilbert) for https://github.com/EmuELEC/EmuELEC

## Build
`make all`

`strip gptokeyb`

## Use
gptokeyb provides a kill switch for an application and mapping of gamepad buttons to keys and/or mouse. It also provides an xbox360-compatible controller mode.

### Environment Variable
`SDL_GAMECONTROLLERCONFIG_FILE` must be set so the gamepad buttons are properly assigned within gptokeyb, e.g. `SDL_GAMECONTROLLERCONFIG_FILE="./gamecontrollerdb.txt"`
`SDL_GAMECONTROLLERCONFIG_FILE` is automatically set in Emuelec

### Command Line Options
`xbox360` selects xbox360 joystick mode

`-c <config_file_path_and_name.gptk>` specifies button mapping for keyboard and mouse functions, e.g. `-c "./app.gptk"`

`-c` as the **last** of the command line options specifies that the default button mapping file should be used, which is `/emuelec/configs/gptokeyb/default.gptk`

`-1 <application name>` or

`-k <application name>` provides the name of the application that will be closed by pressing **start** and **select** together

`-sudokill` indicates that `sudo kill -9 <application name>` will be used to close the application instead of `killall <application name>`

### Keyboard Mapping Options
The config file that specifies button mapping for keyboard and mouse functions takes the form of `%s = %s` which is `gamepad button` = `keyboard key`. Any comment lines beginning with `#` are ignored. Deadzone values are used for analog sticks and triggers, and may be device specific. `mouse_scale` affects the speed of mouse movement, with a larger value causing slower movement. `mouse_scale = 8192` generally works well for RK3326 devices.

Default mappings are:
```back = KEY_ESC
start = KEY_ENTER
guide = KEY_ENTER
a = KEY_X
b = KEY_Z
x = KEY_C
y = KEY_A
l1 = KEY_RIGHTSHIFT
l2 = KEY_HOME
l3 = BTN_LEFT
r1 = KEY_LEFTSHIFT
r2 = KEY_END
r3 = BTN_RIGHT
up = KEY_UP
down = KEY_DOWN
left = KEY_LEFT
right = KEY_RIGHT

left_analog_as_mouse = false
right_analog_as_mouse = false
left_analog_up = KEY_W
left_analog_down = KEY_S
left_analog_left = KEY_A
left_analog_right = KEY_D
right_analog_up = KEY_END
right_analog_down = KEY_HOME
right_analog_left = KEY_LEFT
right_analog_right = KEY_RIGHT

deadzone_y = 15000
deadzone_x = 15000
deadzone_triggers = 3000

fake_mouse_scale = 512
fake_mouse_delay = 16
```
A simple keyboard key repeat function has been added that emulates automatic repeat of a keyboard key, once it has been held for at least an initial `delay`, at a regular `interval`. Key repeat works for one key at a time only (the first key that is pressed and held is repeated, and holding another key will not cause that to repeat, unless the first key is released). Key repeat has not been set up to work for analog triggers (L2/R2) at the moment.

The default delay and interval are based on SDL1.2 standard
```SDL_DEFAULT_REPEAT_DELAY 500
SDL_DEFAULT_REPEAT_INTERVAL 30
```

Key repeat is configured by added `gamepad_button = repeat` as a separate line, in addition to the line `gamepad_button = keyboard key`. The following assigns arrow keys with key repeat to the gamepad d-pad and left analog stick.
```
up = up
up = repeat
down = down
down = repeat
left = left
left = repeat
right = right
right = repeat
left_analog_up = up
left_analog_up = repeat
left_analog_down = down
left_analog_down = repeat
left_analog_left = left
left_analog_left = repeat
left_analog_right = right
left_analog_right = repeat
```
