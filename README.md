# Ardupilot Solo

[![Build Status](https://travis-ci.org/OpenSolo/ardupilot-solo.svg?branch=master)](https://travis-ci.org/OpenSolo/ardupilot-solo)

### 3DR's creation of ArduPilot-Solo
ArduPilot-Solo is a fork of [ArduPilot](https://www.github.com/ArduPilot/ArduPilot), customized by 3DR for the Solo. This was forked in 2015, making it an early beta of ArduCopter 3.3. It has not been rebased and kept up to date with [ArduPilot](https://www.github.com/ArduPilot/ArduPilot) since then.  The most recent release from 3DR for the consumer Solo is v1.3.1.  The most recent release from 3DR for the site scan Solo is 1.5.3.

### Open Solo Era
Since 3DR open sourced the Solo and ceased maintaining it themselves, the ArduPilot-Solo repository has been duplicated here, taking over the upkeep.  There is not a whole lot of changes and enhancements that can be made due to it's age.  [ArduPilot](https://www.github.com/ArduPilot/ArduPilot) development is now on version 3.6, many years ahead of where 3DR left off with this.  Extensive changes have been made across the [ArduPilot](https://www.github.com/ArduPilot/ArduPilot) codebase that cannot be backported to ArduPilot-Solo. The Open Solo team is doing what it can to make it as useful as possible.
- Updated parameters to make the consumer Solos able to fly the latest release that was previously only available to Site Scan commercial Solos.  The features that adds to consumer solos are things like distance based battery failsafe and improved landing detection.
- Adding the LED control scripts and handling by @hugheaves.
- Packaging the final product with Open Solo for use on stock Solos (not green cubes)
- Open Solo 3.0.0 includes release version 1.5.4 of ArduPilot-Solo for the stock cubes.

See the releases section for compiled binaries to download if you wish. https://github.com/OpenSolo/ardupilot-solo/releases.


## How to access Solo

### SSH Details

| Host | IP |
|------|----|
| Solo | 10.1.1.10 |
| Controller | 10.1.1.1 |

#### Default root password

> TjSDBkAu

### How to load to Pixhawk 2 on Solo

- Make sure you are running at least Solo version v1.0.0 (from app)
- Copy your firmware to **/firmware** on Solo (10.1.1.10)
- Reboot Solo
- After reboot the LED's on SOLO should change colours (party mode)
---

## License
>>[Overview of license](http://dev.ardupilot.com/wiki/license-gplv3)

>>[Full Text](https://github.com/diydrones/ardupilot/blob/master/COPYING.txt)
