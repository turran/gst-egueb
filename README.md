What is it?
===========
Gst-Egueb is a package that helps the integration of GStreamer with Egüeb and the other way around.
It provides:
+ A collection of GStreamer elements related to Egueb technology.
  + eguebxmlsink: Egueb XML Sink
  + eguebsrc: Egueb XML Source
  + eguebdemux: Egueb XML Parser/Demuxer/Decoder
+ A video provider interface implementation based on GStreamer

Dependencies
============
+ [Egueb](https://wwww.github.com/turran/egueb)
+ GStreamer

What can I do with it?
======================
With the elements you can build a pipeline like this:

```bash
gst-launch-0.10 filesrc location=some.svg ! eguebdemux ! xvimagesink
```


You will get the svg file rendered with animations, scripting, user navigation, etc. That is, all the features that Egüeb has.

The video provider interface let's you implement any <video> tag for your own XML dialect based on Egüeb. Right now it used to
provide multimedia on SVG files following the SVG Tiny spec.

Communication
=============
In case something fails, use this github project to create an issue, or if you prefer, you can go to #enesim on the freenode IRC server.

