What is it?
===========
Gst-Egueb provides a collection of GStreamer elements related to Egüeb technology

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

Communication
=============
In case something fails, use this github project to create an issue, or if you prefer, you can go to #enesim on the freenode IRC server.

