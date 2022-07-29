# swiper
Live wallpaper engine for Linux systems that support `feh --bg-scale`. See help menu (via `swiper` [no options]) for more information.

## Requirements
- feh
- ffmpeg
- ffprobe

## Functionality
Convert all sorts of video files (mov, mp4, avi, wmv, gif etc.) into a series of frames - extract at custom frame rates, resolutions, and file formats (namely: jpeg, png). Cache frames in memory instead of on disk to reduce frame drops. Apply wallpapers at specific playback frame rates, independent of render frame rates. The main features to enhance performance is frame caching (-c), custom resolutions (-w, -h), playback frame rates (-p) and rendering as jpeg frames (omission of -P). In the below examples, I suggest you understand how the last one works if you want the optimal performance.

## Example usage
- `$ swiper`
- `$ swiper -s 90s-synth.gif -r 442/10 -P -ad`
- `$ swiper -s ~kruz/298983.mp4 -w 1280 -h 720`
- `# swiper -adc`
- `# swiper -s /file.ext -r 15 -w 1280 -h 720 -Pac -p 20

## Limitations
- Stops extracting frames at 1GB of images
- Rendering JPEG frames causes low resolution
- Playback FPS depends on RAM
- Playback FPS must be applied each time via -p
