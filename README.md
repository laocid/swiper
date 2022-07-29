# swiper
Live wallpaper engine for Linux systems that support `feh --bg-scale`. See help menu (via `swiper` [no options]) for more information.

## Requirements
- feh
- ffmpeg
- ffprobe

## Functionality
- Convert all sorts of video files (mov, mp4, avi, wmv, gif etc.) into a series of frames - extract at custom frame rates, resolutions, and file formats (namely: jpeg, png). 
- Cache frames in memory instead of on disk to reduce frame drops. 
- Apply wallpapers at specific playback frame rates, independent of render frame rates. 
- Main performance enhancing features: Frame caching (-c), custom resolution (-w, -h), render frame rate (-r), playback frame rate (-p), and rendering as jpeg frames (omit -P). 

Take a look at the last example if you want optimal performance, although you should understand it before you apply it. Arguments in each example, while compatible with some video files, will not have the same effect on others.

## Example usage
- `$ swiper`
- `$ swiper -s 90s-synth.gif -r 442/10 -P -ad`
- `$ swiper -s ~kruz/298983.mp4 -w 1280 -h 720`
- `# swiper -adc`
- `# swiper -s /file.ext -r 15 -w 1280 -h 720 -Pac -p 20`

## Limitations
- Stops extracting frames at 1GB of images
- Rendering JPEG frames causes low resolution
- Playback FPS depends on RAM
- Playback FPS must be applied each time via -p
