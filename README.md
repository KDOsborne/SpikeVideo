# SpikeVideo

# Session folder structure
- Session A
  - Alignment Info (.txt) This should be the alignmentinfo_README that is generated during the spike sorting pipeline.
  - Neural Data (.xdat) Right now only supports 128ch Smartbox recordings.
  - Picto Session File (.sqlite)
  - Behavioral File (.bhv) This should be a list of all trials, their game number, and their timestamp. An example file is in examples/games.bhv
  - Settings File (.ini) Currently not being used. You can utilize this to load in some settings so the user can modify things without needing to recompile.

# ffmpeg 
- The lib folder is missing avcodec-60.dll because it's too large. The ffmpeg libs were all compiled on my computer, so you may have to build ffmpeg on your computer and replace these dlls so you can compile SpikeVideo on your system.

# Missing Features
- Only supports neuronexus Smartbox recordings with two 64 channel probes. You should update this to be configurable. Will need to support more than just float32 data and more than just 64 channels.
- Does not support drawing Picto polygons or Picto fractals. You can find the fractal algorithm in the leelab picto code (not on github). Time to brush up on your OpenGL :)
- Only supports 30000Hz neural data. The frame rate and audio rate need to be chosen very carefully so that the audio and video of the recorded mp4s will line up. The way things are, with a frame rate of 62.5, an audio rate of 64000, and data sampled at 30000Hz, everything lines up great. If any of these values change, things will not be pretty :)
