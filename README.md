# SpikeVideo

Session folder structure:
- Session #x
  - Alignment Info (.txt) This should be the alignmentinfo_README that is generated during the spike sorting pipeline.
  - Neural Data (.xdat) Right now only supports 128ch Smartbox recordings.
  - Picto Session File (.sqlite)
  - Behavioral File (.bhv) This should be a list of all trials, their game number, and their timestamp. An example file is in examples/games.bhv
  - Settings File (.ini) Currently not being used. You can utilize this to load in some settings so the user can modify things without needing to recompile.
