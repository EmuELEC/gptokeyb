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
