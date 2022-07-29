# swiper
Live wallpaper engine for Linux systems that support `feh --bg-scale`. See help menu (via `swiper` [no options]) for more information.

## Example usage
`$ swiper`
`$ swiper -s 90s-synth.gif -r 442/10 -P -ad -p 30`
`$ swiper -s ~kruz/298983.mp4 -w 1280 -h 720`
`# swiper -adc`

## Limitations
- Stops extracting frames at 1GB of images
- Rendering JPEG frames causes low resolution
- Playback FPS depends on RAM
- Playback FPS must be applied each time via -p
