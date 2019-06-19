# Wheatley robot

This robot was created for the [eurobot 2019](http://www.eurobot.org/) contest and it's french edition. ( *French:* [Coupe de France de robotique](https://www.coupederobotique.fr/))

![](wheatley.gif)

# Hardware

The robot is fully 3 printed. All parts are available in the .step file.

The actuators are standard cheap __9g servomotors__.

The main board is a __WeMos D1 ESP8266__ with all servomotors connected throug a 3.3v to 5v __level shifter__.

# Software

The software is developped and sent to the device with the __Arduino interface__.

It generates a wifi hotspot that you can connect to, and __hosts a webpage__ for controlling the robot axes.

You can also send animations in the form of json documents thanks to the [__ArduinoJson library__](https://github.com/bblanchon/ArduinoJson).