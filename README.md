# gptokeyb

Gamepad to Keyboard/mouse/xbox360(gamepad) emulator

Based on code by: [Kris Henriksen](https://github.com/krishenriksen/AnberPorts/tree/master/AnberPorts-Joystick) and fake Xbox code from: https://github.com/Emanem/js2xbox   
Modified to use SDL2 by: [Nikolai Wuttke](https://github.com/lethal-guitar) & [Shanti Gilbert](https://github.com/shantigilbert) for https://github.com/EmuELEC/EmuELEC
Interactive text entry added by [Robin Duxfield](https://github.com/romadu)

## Build
`make all`

`strip gptokeyb`

## Use
gptokeyb provides a kill switch for an application and mapping of gamepad buttons to keys and/or mouse. It also provides an xbox360-compatible controller mode.

### Environment Variable
`SDL_GAMECONTROLLERCONFIG_FILE` must be set so the gamepad buttons are properly assigned within gptokeyb, e.g. `SDL_GAMECONTROLLERCONFIG_FILE="./gamecontrollerdb.txt"`
`SDL_GAMECONTROLLERCONFIG_FILE` is automatically set in Emuelec

`export HOTKEY` sets the button used as hotkey. `BACK` button is automatically selected as hotkey, unless overridden by `HOTKEY` environment variable

`export TEXTINPUT="my name"` assigns text as preset for input so that `my name` is automatically entered, once triggered

### Command Line Options
`xbox360` selects xbox360 joystick mode

`textinput` select interactive text input mode (see below)

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

The default delay and interval are based on SDL1.2 standard and can be adjusted with `repeat_delay = ` and `repeat_interval = `
```SDL_DEFAULT_REPEAT_DELAY 500
SDL_DEFAULT_REPEAT_INTERVAL 30
```

Key repeat is configured by adding `gamepad_button = repeat` as a separate line, in addition to the line `gamepad_button = keyboard key`. The following assigns arrow keys with key repeat to the gamepad d-pad and left analog stick.
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

### Text Entry Options
Text Entry preset mode is enabled by `TEXTINPUTPRESET` environment variable whereby a name preset can be easily entered whenever a game displays a text prompt. When Text Entry is triggered with `HOTKEY+D-PAD LEFT`, the preset text is entered as a series of key strokes.

Text Entry preset mode also assigns `HOTKEY+D-PAD RIGHT` to send `ENTER`.

Interactive Text Entry mode is enabled by launching GPtoKEYB with command line option `"textinput"` or by environment variable `TEXTINPUTINTERACTIVE = "Y"` , and is triggered with `HOTKEY+D-PAD DOWN`. Once activated, Interactive Text Entry mode works similarly to entering initials for game highscores, with `D-PAD UP/DOWN` switching between options for the currently selected character, `D-PAD RIGHT` moving to next character, `D-PAD LEFT` deleting and moving back one character, `SELECT/HOTKEY` cancelling interactive text entry, and `START` to confirm and exit interactive text entry. `A` sends `ENTER KEY` in interactive text entry mode.

By default Interactive Text Entry mode will start with `A` as the first letter and immediately after a space, and `a` otherwise, unless environment variable `TEXTINPUTNOAUTOCAPITALS = "Y"` is set, whereby all letters will start as `a`.

By default Interactive Text Entry mode includes only a limited number of symbols [space] . , - _ ( ) and a full set of symbols is included with environment variable `TEXTINPUTADDEXTRASYMBOLS = "Y"`.

Interactive Text Entry relies on the game providing a text prompt and sends key strokes to add and change characters, so it is only useful in these situations. Interactive Text Entry is automatically exited when either `SELECT`, `HOTKEY` or `START` are pressed, to minimise issues by accidentally triggering this mode.
