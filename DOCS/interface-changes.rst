Introduction
============

mpv provides access to its internals via the following means:

- options
- commands
- properties
- events
- hooks

The sum of these mechanisms is sometimes called command interface.

All of these are important for interfacing both with end users and API users
(which include Lua scripts, libmpv, and the JSON IPC). As such, they constitute
a large part of the user interface and APIs.

Also see compatibility.rst.

Prior to 0.40.0, only changes had to be listed here and not necessarily new
additions. After 0.40.0, all changes and additions to options/commands/etc are
listed here.

**Never** write to this file directly except when making releases. New changes
are added in the interface-changes directory instead. See contribute.md for more
details.

Interface changes
=================

::

 --- mpv 0.40.0 ---
 --- mpv 0.39.0 ---
    - turn `--cover-art-whitelist` into a list option
    - reserve `user-data/osc` and `user-data/mpv` sub-paths for internal use
    - remove deprecated `packet-video-bitrate` `packet-audio-bitrate` and
      `packet-sub-bitrate` properties
    - remove deprecated `--cache-dir` option alias
    - remove deprecated `--cache-unlink-files` option alias
    - remove deprecated `--demuxer-cue-codepage` option alias
    - remove deprecated `--fps` option alias
    - remove deprecated `--cdrom-device` option alias
    - remove deprecated `--sub-forced-only` option alias
    - remove deprecated `--vo-sixel-exit-clear` option alias
    - remove deprecated `--cdda-toc-bias` option
    - remove deprecated `--drm-atomic` option
    - remove `sub-ass-vsfilter-aspect-compat`: use `sub-ass-use-video-data=none`
      for disabling aspect compat
    - remove `sub-ass-vsfilter-blur-compat`: use `sub-ass-use-video-data=aspect-
      ratio` for disabling blur compat
    - add `sub-ass-use-video-data`
    - add `sub-ass-video-aspect-override`
    - change default V keybind to cycle `sub-ass-use-video-data` instead of the
      now removed `sub-ass-vsfilter-aspect-compat`
    - remove `console-scale` script-opt
    - remap numpad `+ - * /` keys to `KP_ADD/KP_SUBTRACT/KP_MULTIPLY/KP_DIVIDE`;
      keybinds which require these numpad keys to function need to use the new
      names instead
    - numerical values of `--loop-file` no longer decrease on each iteration
    - add `remaining-file-loops` property as a replacement to get the remaining
      loop count
    - numerical values of `--ab-loop-count` no longer decrease on each iteration
    - add `remaining-ab-loops` property as a replacement to get the remaining
      loop count
    - move 'scale' above 'force' for `sub-ass-override` in documentation as well
      as code. This more accurately reflects destructiveness of these options.
    - change `sub-ass-override` default from 'yes' to 'scale'. This should
      result in no effective changes because 'yes' used to unintentionally do
      what 'scale' should've done.
    - change 'u' binding to cycle between 'force' and 'scale', instead of
      'force' and 'yes'
    - deprecate `sub-text-ass` property; add `sub-text/ass` sub-property
    - change type of `sub-start` and `sub-end` properties to time
    - change `vidscale` script option type to string for osc.lua
    - change `vidscale` script option type to string for stats.lua
    - change `vidscale` default from `yes` to `auto` for osc.lua and stats.lua
    - change `mp.add_key_binding` so that by default, the callback is not
      invoked if the event is canceled; clients should now use the `complex`
      option to detect this situation
    - add `canceled` entry to `mp.add_key_binding` callback argument
    - add the `normalize-path` command
    - add `user-data/mpv/ytdl/path` and `user-data/mpv/ytdl/json-subprocess-
      result` properties
    - add `track-list/N/dolby-vision-profile` and `track-list/N/dolby-vision-
      level`
    - add `track-list/N/decoder`
    - add `sub-text/ass-full` sub-property
    - add `osc-show` script message
    - add `nonrepeatable` input command prefix
    - add `mp.input.select()`
    - add `--wasapi-exclusive-buffer` option
    - add `--vf=d3d11vpp=scaling-mode`
    - add `--vf=d3d11vpp=scale`
    - add `--sub-border-style` and `--osd-border-style` options
    - the border style does not depend on `--(sub/osd)-border-color` and
      `--(sub/osd)-shadow-color`; now it depends solely on `--(sub/osd)-border-
      style`
    - make `--(sub/osd)-border-color` an alias of `--(sub/osd)-outline-color`
    - make `--(sub/osd)-border-size` an alias of `--(sub/osd)-outline-size`
    - make `--(sub/osd)-shadow-color` an alias of `--(sub/osd)-back-color`; they
      cannot both be set now
    - make `--osd-bar-border-size` an alias of `--osd-bar-outline-size`
    - add `--show-in-taskbar` option
    - add `--pitch` option
    - add `--osd-playlist-entry` option
    - remove `osc-playlist_media_title` script-opt
    - add `--native-touch` option
    - add `--input-touch-emulate-mouse` option
    - add `touch-pos` property
    - add `--media-controls` option
    - add `--input-dragging-deadzone` option
    - add `--input-builtin-dragging` option
    - add `--egl-config-id` option
    - add `--egl-output-format` option
    - add `--directory-filter-types`
    - By default, opening a directory will create a playlist with only the media
      types "video, audio, image". To restore the previous behavior, use
      `--directory-filter-types-clr`.
    - add `--autocreate-playlist`
    - add `--video-exts`
    - add `--audio-exts`
    - add `--image-exts`
    - add `option-info/<name>/expects-file` sub-property
    - Bump dependency of VapourSynth to utilize its API version 4. New minimum
      VapourSynth version for runtime is R56. Some functions and plugins are
      changed or removed. For details, refer to VapourSynth documentation
      <http://www.vapoursynth.com/2021/09/r55-audio-support-and-improved-performance/> and
      <https://github.com/vapoursynth/vapoursynth/blob/R68/APIV4%20changes.txt>
 --- mpv 0.38.0 ---
    - add `term-size` property
    - add the `escape-ass` command
    - add `>` for fixed precision floating-point property expansion
    - add `--input-comands` option
    - change `--pulse-latency-hacks` default to `yes`
    - add `context-menu` command
    - add `menu-data` property
    - add `--vo-tct-buffering` option
    - add `begin-vo-dragging` command
    - add `--deinterlace-field-parity` option
    - add `--volume-gain`, `--volume-gain-min`, and `--volume-gain-max` options
    - add `current-gpu-context` property
    - add `--secondary-sub-ass-override` option
    - add `--input-preprocess-wheel` option
    - remove `shared-script-properties` (`user-data` is a replacement)
    - add `--secondary-sub-delay`, decouple secondary subtitles from
      `--sub-delay`
    - add the `--osd-bar-border-size` option
    - `--screenshot-avif-pixfmt` no longer defaults to yuv420p
    - `--screenshot-avif-opts` defaults to lossless screenshot
    - rename key `MP_KEY_BACK` to `MP_KEY_GO_BACK`
    - add `--sub-filter-sdh-enclosures` option
    - added the `mp.input` scripting API to query the user for textual input
    - add `forced` choice to `subs-with-matching-audio`
    - remove `--term-remaining-playtime` option
    - change fallback deinterlace to bwdif
    - add the command `load-config-file`
    - add the command `load-input-conf`
    - remove `--vo=rpi`, `--gpu-context=rpi`, and `--hwdec=mmal`
    - add `auto` choice to `--deinterlace`
    - change `--teletext-page` default from 100 to 0 ("subtitle" in lavc)
    - change `--hidpi-window-scale` default to `no`
    - add `insert-next`, `insert-next-play`, `insert-at`, and `insert-at-play`
      actions to `loadfile` and `loadlist` commands
    - add `index` argument to `loadlist` command
    - add `index` argument to `loadfile` command. This breaks all existing
      uses of this command which make use of the argument to include the list of
      options to be set while the file is playing. To address this problem, the
      third argument now needs to be set to -1 if the fourth argument needs to be used.
    - move the `options` argument of the `loadfile` command from the third
      parameter to the fourth (after `index`)
    - add `--drag-and-drop=insert-next` option
    - rename `--background` to `--background-color`
    - remove `--alpha` and reintroduce `--background` option for better control
      over blending alpha components into specific background types
    - add `--border-background` option
    - add `video-target-params` property
    - add `hdr10plus` sub-parameter to `format` video filter
    - remove `--focus-on-open` and add replacement `--focus-on`
    - remove debanding from the high-quality profile
 --- mpv 0.37.0 ---
    - `--save-position-on-quit` and its associated commands now store state files
      in %LOCALAPPDATA% instead of %APPDATA% directory by default on Windows.
    - change `--subs-with-matching-audio` default from `no` to `yes`
    - change `--subs-fallback` default from `no` to `default`
    - add the `--hdr-peak-percentile` option
    - include `--hdr-peak-percentile` in the `gpu-hq` profile
    - change `--audiotrack-pcm-float` default from `no` to `yes`
    - add video-params/aspect-name
    - change type of `--sub-pos` to float
    - The remaining time printed in the terminal is now adjusted for speed by default.
      You can disable this with `--no-term-remaining-playtime`.
    - add `playlist-path` and `playlist/N/playlist-path` properties
    - add `--x11-wid-title` option
    - add `--libplacebo-opts` option
    - add `--audio-file-exts`, `--cover-art-auto-exts`, and `--sub-auto-exts`
    - change `slang` default back to NULL
    - remove special handling of the `auto` value from `--alang/slang/vlang` options
    - add `--subs-match-os-language` as a replacement for `--slang=auto`
    - add `always` option to `--subs-fallback-forced`
    - remove `auto` choice from `--sub-forced-only`
    - remove `auto-forced-only` property
    - rename `--sub-forced-only` to `--sub-forced-events-only`
    - remove `sub-forced-only-cur` property (`--sub-forced-events-only` is a replacement)
    - remove deprecated `video-aspect` property
    - add `--video-crop`
    - add `video-params/crop-[w,h,x,y]`
    - remove `--tone-mapping-mode`
    - change `--subs-fallback-forced` so that it works alongside `--slang`
    - add `--icc-3dlut-size=auto` and make it the default
    - add `--scale=ewa_lanczos4sharpest`
    - remove `--scale-wblur`, `--cscale-wblur`, `--dscale-wblur`, `--tscale-wblur`
    - remove `bcspline` filter (`bicubic` is now the same as `bcspline`)
    - rename `--cache-dir` and `--cache-unlink-files` to `--demuxer-cache-dir` and
      `--demuxer-cache-unlink-files`
    - enable `--correct-downscaling`, `--linear-downscaling`, `--sigmoid-upscaling`
    - `--cscale` defaults to `--scale` if not defined
    - change `--tscale` default to `oversample`
    - change `--dither-depth` to `auto`
    - deprecate `--profile=gpu-hq`, add `--profile=<fast|high-quality>`
    - change `--dscale` default to `hermite`
    - update defaults to `--hdr-peak-decay-rate=20`, `--hdr-scene-threshold-low=1.0`,
      `--hdr-scene-threshold-high=3.0`
    - update defaults to `--deband-threshold=48`, `--deband-grain=32`
    - add `--directory-mode=auto` and make it the default
    - remove deprecated `--profile=opengl-hq`
    - remove several legacy fallbacks for old deprecated options (now they will just
      error out like normal)
    - remove deprecated `drop-frame-count` and `vo-drop-frame-count` property aliases
    - remove the ability to write to the `display-fps` property (use `override-display-fps`
      instead)
    - writing the current value to playlist-pos will no longer restart playback (use
      `playlist-play-index` instead)
    - remove deprecated `--oaoffset`, `--oafirst`, `--ovoffset`, `--ovfirst`,
      `--demuxer-force-retry-on-eof`, `--fit-border` options
    - remove deprecated `--record-file` option
    - remove deprecated `--vf-defaults` and `--af-defaults` options
    - `--drm-connector` no longer allows selecting the card number (use `--drm-device`
      instead)
    - add `--title-bar` option
    - add `--window-corners` option
    - rename `--cdrom-device` to `--cdda-device`
    - remove `--scale-cutoff`, `--cscale-cutoff`, `--dscale-cutoff`, `--tscale-cutoff`
    - remove `--scaler-lut-size`
    - deprecate shared-script-properties (user-data is a replacement)
    - add `--backdrop-type` option
    - add `--window-affinity` option
    - `--config-dir` no longer forces cache and state files to also reside in there
    - deprecate `--demuxer-cue-codepage` in favor of `--metadata-codepage`
    - change the default of `metadata-codepage` to `auto`
    - add `playlist-next-playlist` and `playlist-prev-playlist` commands
    - change `video-codec` to show description or name, not both
    - deprecate `--cdda-toc-bias` option, offsets are always checked now
    - disable `--allow-delayed-peak-detect` by default
    - rename `--fps` to `--container-fps-override`
    - rename `--override-display-fps` to `--display-fps-override`
    - rename `--sub-ass-force-style` to `--sub-ass-style-overrides`
    - alias `--screenshot-directory` to `--screenshot-dir`
    - alias `--watch-later-directory` to `--watch-later-dir`
    - rename `--play-dir` to `--play-direction`
    - `--js-memory-report` is now used for enabling memory reporting for javascript
      scripts
    - drop support for `-del` syntax for list options
    - `--demuxer-hysteresis-secs` now respects `--cache-secs` and/or
      `--demuxer-readahead-secs` as well
    - add hdr metadata to `video-params` property
    - add `--target-gamut`
    - change the way display names are retrieved on macOS, usage of options and properties
      `--fs-screen-name`, `--screen-name` and `display-names` needs to be adjusted
    - remove OpenGL cocoa backend that was deprecated in 0.29
    - remove `border`, `fullscreen`, `ontop`, `osd-level` and `pause`
      from default `--watch-later-options`
    - add `video-*` and `secondary-sub-visibility` to default `--watch-later-options`
 --- mpv 0.36.0 ---
    - add `--target-contrast`
    - Target luminance value is now also applied when ICC profile is used.
      `--icc-use-luma` has been added to use ICC profile luminance value.
      If target luminance and ICC luminance is not used, old behavior apply,
      defaulting to 203 nits. (Only applies for `--vo=gpu-next`)
    - `playlist/N/title` gets set upon opening the file if it wasn't already set
      and a title is available.
    - add the `--vo=kitty` video output driver, as well as the options
      `--vo-kitty-cols`, `--vo-kitty-rows`, `--vo-kitty-width`,
      `--vo-kitty-height`, `--vo-kitty-left`, `--vo-kitty-top`,
      `--vo-kitty-config-clear`, `--vo-kitty-alt-screen` and
      `--vo-kitty-use-shm`
    - add `--force-render`
    - add `--vo-sixel-config-clear`, `--vo-sixel-alt-screen` and
      `--vo-sixel-buffered`
    - add `--wayland-content-type`
    - deprecate `--vo-sixel-exit-clear` and alias it to
      `--vo-sixel-alt-screen`
    - deprecate `--drm-atomic`
    - add `--demuxer-hysteresis-secs`
    - add `--video-sync=display-tempo`
    - the `start` option is no longer unconditionally written by
      watch-later. It is still written by default but you may
      need to explicitly add `start` depending on how you have
      `--watch-later-options` configured.
    - add `--vd-lavc-dr=auto` and make it the default
    - add support for the fractional scale protocol in wayland
    - in wayland, hidpi window scaling now scales the window by the compositor's
      dpi scale factor by default (can be disabled with --no-hidpi-window-scale
      if fractional scaling support exists).
    - change --screenshot-tag-colorspace default value from `no` to `yes`
    - undeprecate vf_sub
    - add `--tone-mapping=st2094-40` and `--tone-mapping=st2094-10`
    - change `--screenshot-jxl-effort` default from `3` to `4`.
    - add `--tone-mapping-visualize`
    - change type of `--brightness`, `--saturation`, `--contrast`, `--hue` and
      `--gamma` to float.
    - add `platform` property
    - add `--auto-window-resize`
    - `--save-position-on-quit` and its associated commands now store state files in
      the XDG_STATE_HOME directory by default. This only has an effect on linux/bsd
      systems.
    - mpv now implicitly saves cache files in XDG_CACHE_HOME by default. This only has
      an effect if the user enables options that would lead to cache being stored and
      only makes a difference on linux/bsd systems.
    - `--cache-on-disk` no longer requires explicitly setting the `--cache-dir` option
    - add `--icc-cache` and `--gpu-shader-cache` options to control whether or not to
      save cache files for these features; explicitly setting `--icc-cache-dir` and
      `--gpu-shader-cache` is no longer required
    - remove the `--tone-mapping-crosstalk` option
    - add `--gamut-mapping-mode=perceptual|relative|saturation|absolute|linear`
    - add `--corner-rounding` option
    - change `--subs-with-matching-audio` default from `yes` to `no`
    - change `--slang` default from blank to `auto`
    - add `--input-cursor-passthrough` option to allow pointer events to completely
      passthrough the mpv window
    - icc and gpu-shader cache are now saved by default (use --no-icc-shader-cache and
      --no-gpu-shader-cache to disable)
    - add `--directory-mode=recursive|lazy|ignore`
    - `--hwdec=yes` is now mapped to `auto-safe` rather than `auto` (also used
      by ctrl+h keybind)
    - add `--hdr-contrast-recovery` and `--hdr-contrast-smoothness`
    - include `--hdr-contrast-recovery` in the `gpu-hq` profile
 --- mpv 0.35.0 ---
    - add the `--vo=gpu-next` video output driver, as well as the options
      `--allow-delayed-peak-detect`, `--builtin-scalers`,
      `--interpolation-preserve` `--lut`, `--lut-type`, `--image-lut`,
      `--image-lut-type` and `--target-lut` along with it.
    - add `--target-colorspace-hint`
    - add `--tone-mapping-crosstalk`
    - add `--tone-mapping` options `auto`, `spline` and `bt.2446a`
    - add `--inverse-tone-mapping`
    - add `--gamut-mapping-mode`, replacing `--gamut-clipping` and `--gamut-warning`
    - add `--tone-mapping-mode`, replacing `--tone-mapping-desaturate` and
      `--tone-mapping-desaturate-exponent`.
    - add `dolbyvision` sub-parameter to `format` video filter
    - `--sub-visibility` no longer has any effect on secondary subtitles
    - add `film-grain` sub-parameter to `format` video filter
    - add experimental `--vo=dmabuf-wayland` video output driver
    - add `--x11-present` for controlling whether to use xorg's present extension
    - add `engine` option to the `rubberband` audio filter to support the new
      engine introduced in rubberband 3.0.0. Defaults to `finer` (new engine).
    - add `--wayland-configure-bounds` option
    - deprecate `--gamma-factor`
    - deprecate `--gamma-auto`
    - remove `--vulkan-disable-events`
    - add `--glsl-shader-opts`
 --- mpv 0.34.0 ---
    - deprecate selecting by card number with `--drm-connector`, add
      `--drm-device` which can be used instead
    - add `--screen-name` and `--fs-screen-name` flags to allow selecting the
      screen by its name instead of the index
    - add `--macos-geometry-calculation` to change the rectangle used for screen
      position and size calculation. the old behavior used the whole screen,
      which didn't take the menu bar and Dock into account. The new default
      behaviour includes both. To revert to the old behavior set this to
      `whole`.
    - add an additional optional `albumart` argument to the `video-add` command,
      which tells mpv to load the given video as album art.
    - undeprecate `--cache-secs` option
    - remove `--icc-contrast` and introduce `--icc-force-contrast`. The latter
      defaults to the equivalent of the old `--icc-contrast=inf`, and can
      instead be used to specifically set the contrast to any value.
    - add a `--watch-later-options` option to allow configuring which
      options quit-watch-later saves
    - make `current-window-scale` writeable and use it in the default input.conf
    - add `--input-builtin-bindings` flag to control loading of built-in key
      bindings during start-up (default: yes).
    - add ``track-list/N/image`` sub-property
    - remove `--opengl-restrict` option
    - js custom-init: use filename ~~/init.js instead of ~~/.init.js (dot)
 --- mpv 0.33.0 ---
    - add `--d3d11-exclusive-fs` flag to enable D3D11 exclusive fullscreen mode
      when the player enters fullscreen.
    - directories in ~/.mpv/scripts/ (or equivalent) now have special semantics
      (see mpv Lua scripting docs)
    - names starting with "." in ~/.mpv/scripts/ (or equivalent) are now ignored
    - js modules: ~~/scripts/modules.js/ is no longer used, global paths can be
      set with custom init (see docs), dir-scripts first look at <dir>/modules/
    - the macOS bundle now logs to "~/Library/Logs/mpv.log" by default
    - deprecate the --cache-secs option (once removed, the cache cannot be
      limited by time anymore)
    - remove deprecated legacy hook API ("hook-add", "hook-ack"). Use either the
      libmpv API (mpv_hook_add(), mpv_hook_continue()), or the Lua scripting
      wrappers (mp.add_hook()).
    - improve how property change notifications are delivered on events and on
      hooks. In particular, a hook event is only returned to a client after all
      changes initiated before the hook point were delivered to the same client.
      In addition, it should no longer happen that events and property change
      notifications were interleaved in bad ways (it could happen that a
      property notification delivered after an event contained a value that was
      valid only before the event happened).
    - the playlist-pos and playlist-pos-1 properties now can return and accept
      -1, and are never unavailable. Out of range indexes are now accepted, but
      behave like writing -1.
    - the playlist-pos and playlist-pos-1 properties deprecate the current
      behavior when writing back the current value to the property: currently,
      this restarts playback, but in the future, it will do nothing.
      Using the "playlist-play-index" command is recommended instead.
    - add "playlist-play-index" command
    - add playlist-current-pos, playlist-playing-pos properties
    - Lua end-file events set the "error" field; this is deprecated; use the
      "file_error" instead for this specific event. Scripts relying on the
      "error" field for end-file will silently break at some point in the
      future.
    - remove deprecated --input-file option, add --input-ipc-client, which is
      vaguely a replacement of the removed option, but not the same
    - change another detail for track selection options (see --aid manpage
      entry)
    - reading loop-file property as native property or mpv_node will now return
      "inf" instead of boolean true (also affects loop option)
    - remove some --vo-direct3d-... options (it got dumbed down; use --vo=gpu)
    - remove video-params/plane-depth property (was too vaguely defined)
    - remove --video-sync-adrop-size option (implementation was changed, no
      replacement for what this option did)
    - undeprecate --video-sync=display-adrop
    - deprecate legacy auto profiles (profiles starting with "extension." and
      "protocol."). Use conditional auto profiles instead.
    - the "subprocess" command does not connect spawned processes' stdin to
      mpv's stdin anymore. Instead, stdin is connected to /dev/null by default.
      To get the old behavior, set the "passthrough_stdin" argument to true.
    - key/value list options do not accept ":" as item separator anymore,
      only ",". This means ":" is always considered part of the value.
    - remove deprecated --vo-vdpau-deint option
    - add `delete-watch-later-config` command to complement
      `write-watch-later-config`
 --- mpv 0.32.0 ---
    - change behavior when using legacy option syntax with options that start
      with two dashes (``--`` instead of a ``-``). Now, using the recommended
      syntax is required for options starting with ``--``, which means an option
      value must be strictly passed after a ``=``, instead of as separate
      argument. For example, ``--log-file f.txt`` was previously accepted and
      behaved like ``--log-file=f.txt``, but now causes an error. Use of legacy
      syntax that is still supported now prints a deprecation warning.
 --- mpv 0.31.0 ---
    - add `--resume-playback-check-mtime` to check consistent mtime when
      restoring playback state.
    - add `--d3d11-output-csp` to enable explicit selection of a D3D11
      swap chain color space.
    - the --sws- options and similar now affect vo_image and screenshot
      conversion (does not matter as much for vo_gpu, which does most of this
      with shaders)
    - add a builtin "sw-fast" profile, which restores performance settings
      for software video conversion. These were switched to higher quality since
      mpv 0.30.0 (related to the previous changelog entry). This affects video
      outputs like vo_x11 and vo_drm, and screenshots, but not much else.
    - deprecate --input-file (there are no plans to remove this short-term,
      but it will probably eventually go away <- that was a lie)
    - deprecate --video-sync=display-adrop (might be removed if it's in the way;
      undeprecated or re-added if it's not too much of a problem)
    - deprecate all input section commands (these will be changed/removed, as
      soon as mpv internals do not require them anymore)
    - remove deprecated --playlist-pos alias (use --playlist-start)
    - deprecate --display-fps, introduce --override-display-fps. The display-fps
      property now is unavailable if no VO exists (or the VO did not return a
      display FPS), instead of returning the option value in this case. The
      property will keep existing, but writing to it is deprecated.
    - the vf/af properties now do not reject the set value anymore, even if
      filter chain initialization fails. Instead, the vf/af options are always
      set to the user's value, even if it does not reflect the "runtime" vf/af
      chain.
    - the vid/aid/sid/secondary-sid properties (and their aliases: "audio",
      "video", "sub") will now allow setting any track ID; before this change,
      only IDs of actually existing tracks could be set (the restriction was
      active the MPV_EVENT_FILE_LOADED/"file-loaded" event was sent). Setting
      an ID for which no track exists is equivalent to disabling it. Note that
      setting the properties to non-existing tracks may report it as selected
      track for a small time window, until it's forced back to "no". The exact
      details how this is handled may change in the future.

 --- mpv 0.30.0 ---
    - add `--d3d11-output-format` to enable explicit selection of a D3D11
      swap chain format.
    - rewrite DVB channel switching to use an integer value
      `--dvbin-channel-switch-offset` for switching instead of the old
      stream controls which are now gone. Cycling this property up or down will
      change the offset to the channel which was initially tuned to.
      Example for `input.conf`: `H cycle dvbin-channel-switch-offset up`,
      `K cycle dvbin-channel-switch-offset down`.
    - adapt `stream_dvb` to support writing to `dvbin-prog` at runtime
      and also to consistently use dvbin-configuration over URI parameters
      when provided
    - add `--d3d11-adapter` to enable explicit selection of a D3D11 rendering
      adapter by name.
    - rename `--drm-osd-plane-id` to `--drm-draw-plane`, `--drm-video-plane-id` to
      `--drm-drmprime-video-plane` and `--drm-osd-size` to `--drm-draw-surface-size`
      to better reflect what the options actually control, that the values they
      accept aren't actually internal DRM ID's (like with similar options in
      ffmpeg's KMS support), and that the video plane is only used when the drmprime
      overlay hwdec interop is active, with the video being drawn to the draw plane
      otherwise.
    - in addition to the above, the `--drm-draw-plane` and `--drm-drmprime-video-plane`
      options now accept either an integer index, or the values primary or overlay.
      `--drm-draw-plane` now defaults to primary and `--drm-drmprime-video-plane`
      defaults to overlay. This should be similar to previous behavior on most drivers
      due to how planes are usually sorted.
    - rename --opensles-frames-per-buffer to --opensles-frames-per-enqueue to
      better reflect its purpose. In the past it overrides the buffer size the AO
      requests (but not the default/value of the generic --audio-buffer option).
      Now it only guarantees that the soft buffer size will not be smaller than
      itself while setting the size per Enqueue.
    - add --opensles-buffer-size-in-ms, allowing user to tune the soft buffer size.
      It overrides the --audio-buffer option unless it's set to 0 (with the default
      being 250).
    - remove `--linear-scaling`, replaced by `--linear-upscaling` and
      `--linear-downscaling`. This means that `--sigmoid-upscaling` no longer
      implies linear light downscaling as well, which was confusing.
    - the built-in `gpu-hq` profile now includes` --linear-downscaling`.
    - support for `--spirv-compiler=nvidia` has been removed, leaving `shaderc`
      as the only option. The `--spirv-compiler` option itself has been marked
      as deprecated, and may be removed in the future.
    - split up `--tone-mapping-desaturate`` into strength + exponent, instead of
      only using a single value (which previously just controlled the exponent).
      The strength now linearly blends between the linear and nonlinear tone
      mapped versions of a color.
    - add --hdr-peak-decay-rate and --hdr-scene-threshold-low/high
    - add --tone-mapping-max-boost
    - ipc: require that "request_id" fields are integers. Other types are still
      accepted for compatibility, but this will stop in the future. Also, if no
      request_id is provided, 0 will be assumed.
    - mpv_command_node() and mp.command_native() now support named arguments
      (see manpage). If you want to use them, use a new version of the manpage
      as reference, which lists the definitive names.
    - edition and disc title switching will now fully reload playback (may have
      consequences for scripts, client API, or when using file-local options)
    - with the removal of the stream cache, the following properties and options were
      dropped: `cache`, `cache-size`, `cache-free`, `cache-used`, `--cache-default`,
      `--cache-initial`, `--cache-seek-min`, `--cache-backbuffer`, `--cache-file`,
      `--cache-file-size`
    - the --cache option does not take a number value anymore
    - remove async playback abort hack. This may make it impossible to abort
      playback if --demuxer-thread=no is forced.
    - remove `--macos-title-bar-style`, replaced by `--macos-title-bar-material`
      and `--macos-title-bar-appearance`.
    - The default for `--vulkan-async-compute` has changed to `yes` from `no`
      with the move to libplacebo as the back-end for vulkan rendering.
    - Remove "disc-titles", "disc-title", "disc-title-list", and "angle"
      properties. dvd:// does not support title ranges anymore.
    - Remove all "tv-..." options and properties, along with the classic Linux
      analog TV support.
    - remove "program" property (no replacement)
    - always prefer EGL over GLX, which helps with AMD/vaapi, but will break
      vdpau with --vo=gpu - use --gpu-context=x11 to be able to use vdpau. This
      does not affect --vo=vdpau or --hwdec=vdpau-copy.
    - remove deprecated --chapter option
    - deprecate --record-file
    - add `--demuxer-cue-codepage`
    - add ``track-list/N/demux-bitrate``, ``track-list/N/demux-rotation`` and
      ``track-list/N/demux-par`` property
    - Deprecate ``--video-aspect`` and add ``--video-aspect-override`` to
      replace it. (The `video-aspect` option remains unchanged.)
 --- mpv 0.29.0 ---
    - drop --opensles-sample-rate, as --audio-samplerate should be used if desired
    - drop deprecated --videotoolbox-format, --ff-aid, --ff-vid, --ff-sid,
      --ad-spdif-dtshd, --softvol options
    - fix --external-files: strictly never select any tracks from them, unless
      explicitly selected (this may or may not be expected)
    - --ytdl is now always enabled, even for libmpv
    - add a number of --audio-resample-* options, which should from now on be
      used instead of --af-defaults=lavrresample:...
    - deprecate --vf-defaults and --af-defaults. These didn't work with the
      lavfi bridge, so they have very little use left. The only potential use
      is with af_lavrresample (going to be deprecated, --audio-resample-... set
      its defaults), and various hw deinterlacing filters (like vf_vavpp), for
      which you will have to stop using --deinterlace=yes, and instead use the
      vf toggle commands and the filter enable/disable flag to customize it.
    - deprecate --af=lavrresample. Use the ``--audio-resample-...`` options to
      customize resampling, or the libavfilter ``--af=aresample`` filter.
    - add --osd-on-seek
    - remove outfmt sub-parameter from "format" video filter (no replacement)
    - some behavior changes in the video filter chain, including:
        - before, using an incompatible filter with hwdec would disable hwdec;
          now it disables the filter at runtime instead
        - inserting an incompatible filter with hwdec at runtime would refuse
          to insert the filter; now it will add it successfully, but disables
          the filter slightly later
    - some behavior changes in the audio filter chain, including:
        - a manually inserted lavrresample filter is not necessarily used for
          sample format conversion anymore, so it's pretty useless
        - changing playback speed will not respect --af-defaults anymore
        - having libavfilter based filters after the scaletempo or rubberband
          filters is not supported anymore, and may desync if playback speed is
          changed (libavfilter does not support the metadata for playback speed)
        - the lavcac3enc filter does not auto detach itself anymore; instead it
          passes through the data after converting it to the sample rate and
          channel configuration the ac3 encoder expects; also, if the audio
          format changes midstream in a way that causes the filter to switch
          between PCM and AC3 output, the audio output won't be reconfigured,
          and audio playback will fail due to libswresample being unable to
          convert between PCM and AC3 (Note: the responsible developer didn't
          give a shit. Later changes might have improved or worsened this.)
        - inserting a filter that changes the output sample format will not
          reconfigure the AO - you need to run an additional "ao-reload"
          command to force this if you want that
        - using "strong" gapless audio (--gapless-audio=yes) can fail if the
          audio formats are not convertible (such as switching between PCM and
          AC3 passthrough)
        - if filters do not pass through PTS values correctly, A/V sync can
          result over time. Some libavfilter filters are known to be affected by
          this, such as af_loudnorm, which can desync over time, depending on
          how the audio track was muxed (af_lavfi's fix-pts suboption can help).
    - remove out-format sub-parameter from "format" audio filter (no replacement)
    - --lavfi-complex now requires uniquely named filter pads. In addition,
      unconnected filter pads are not allowed anymore (that means every filter
      pad must be connected either to another filter, or to a video/audio track
      or video/audio output). If they are disconnected at runtime, the stream
      will probably stall.
    - rename --vo=opengl-cb to --vo=libmpv (goes in hand with the opengl-cb
      API deprecation, see client-api-changes.rst)
    - deprecate the OpenGL cocoa backend, option choice --gpu-context=cocoa
      when used with --gpu-api=opengl (use --vo=libmpv)
    - make --deinterlace=yes always deinterlace, instead of trying to check
      certain unreliable video metadata. Also flip the defaults of all builtin
      HW deinterlace filters to always deinterlace.
    - change vf_vavpp default to use the best deinterlace algorithm by default
    - remove a compatibility hack that allowed CLI aliases to be set as property
      (such as "sub-file"), deprecated in mpv 0.26.0
    - deprecate the old command based hook API, and introduce a proper C API
      (the high level Lua API for this does not change)
    - rename the the lua-settings/ config directory to script-opts/
    - the way the player waits for scripts getting loaded changes slightly. Now
      scripts are loaded in parallel, and block the player from continuing
      playback only in the player initialization phase. It could change again in
      the future. (This kind of waiting was always a feature to prevent that
      playback is started while scripts are only half-loaded.)
    - deprecate --ovoffset, --oaoffset, --ovfirst, --oafirst
    - remove the following encoding options: --ocopyts (now the default, old
      timestamp handling is gone), --oneverdrop (now default), --oharddup (you
      need to use --vf=fps=VALUE), --ofps, --oautofps, --omaxfps
    - remove --video-stereo-mode. This option was broken out of laziness, and
      nobody wants to fix it. Automatic 3D down-conversion to 2D is also broken,
      although you can just insert the stereo3d filter manually. The obscurity
      of 3D content doesn't justify such an option anyway.
    - change cycle-values command to use the current value, instead of an
      internal counter that remembered the current position.
    - remove deprecated ao/vo auto profiles. Consider using scripts like
      auto-profiles.lua instead.
    - --[c]scale-[w]param[1|2] and --tone-mapping-param now accept "default",
      and if set to that value, reading them as property will also return
      "default", instead of float nan as in previous versions
 --- mpv 0.28.0 ---
    - rename --hwdec=mediacodec option to mediacodec-copy, to reflect
      conventions followed by other hardware video decoding APIs
    - drop previously deprecated --heartbeat-cmd and --heartbeat--interval
      options
    - rename --vo=opengl to --vo=gpu
    - rename --opengl-backend to --gpu-context
    - rename --opengl-shaders to --glsl-shaders
    - rename --opengl-shader-cache-dir to --gpu-shader-cache-dir
    - rename --opengl-tex-pad-x/y to --gpu-tex-pad-x/y
    - rename --opengl-fbo-format to --fbo-format
    - rename --opengl-gamma to --gamma-factor
    - rename --opengl-debug to --gpu-debug
    - rename --opengl-sw to --gpu-sw
    - rename --opengl-vsync-fences to --swapchain-depth, and the interpretation
      slightly changed. Now defaults to 3.
    - rename the built-in profile `opengl-hq` to `gpu-hq`
    - the semantics of --opengl-es=yes are slightly changed -> now requires GLES
    - remove the (deprecated) alias --gpu-context=drm-egl
    - remove the (deprecated) --vo=opengl-hq
    - remove --opengl-es=force2 (use --opengl-es=yes --opengl-restrict=300)
    - the --msg-level option now affects --log-file
    - drop "audio-out-detected-device" property - this was unavailable on all
      audio output drivers for quite a while (coreaudio used to provide it)
    - deprecate --videotoolbox-format (use --hwdec-image-format, which affects
      most other hwaccels)
    - remove deprecated --demuxer-max-packets
    - remove most of the deprecated audio and video filters
    - remove the deprecated --balance option/property
    - rename the --opengl-hwdec-interop option to --gpu-hwdec-interop, and
      change some of its semantics: extend it take the strings "auto" and
      "all". "all" loads all backends. "auto" behaves like "all" for
      vo_opengl_cb, while on vo_gpu it loads nothing, but allows on demand
      loading by the decoder. The empty string as option value behaves like
      "auto". Old --hwdec values do not work anymore.
      This option is hereby declared as unstable and may change any time - its
      old use is deprecated, and it has very little use outside of debugging
      now.
    - change the --hwdec option from a choice to a plain string (affects
      introspection of the option/property), also affects some properties
    - rename --hwdec=rpi to --hwdec=mmal, same for the -copy variant (no
      backwards compatibility)
    - deprecate the --ff-aid, --ff-vid, --ff-sid options and properties (there is
      no replacement, but you can manually query the track property and use the
      "ff-index" field to find the mpv track ID to imitate this behavior)
    - rename --no-ometadata to --no-ocopy-metadata
 --- mpv 0.27.0 ---
    - drop previously deprecated --field-dominance option
    - drop previously deprecated "osd" command
    - remove client API compatibility handling for "script", "sub-file",
      "audio-file", "external-file" (these cases used to log a deprecation
      warning)
    - drop deprecated --video-aspect-method=hybrid option choice
    - rename --hdr-tone-mapping to --tone-mapping (and generalize it)
    - --opengl-fbo-format changes from a choice to a string. Also, its value
      will be checked only on renderer initialization, rather than when the
      option is set.
    - Using opengl-cb now always assumes 8 bit per component depth, and dithers
      to this size. Before, it tried to figure out the depth of the first
      framebuffer that was ever passed to the renderer. Having GL framebuffers
      with a size larger than 8 bit per component is quite rare. If you need
      it, set the --dither-depth option instead.
    - --lavfi-complex can now be set during runtime. If you set this in
      expectation it would be applied only after a reload, you might observe
      weird behavior.
    - add --track-auto-selection to help with scripts/applications that
      make exclusive use of --lavfi-complex.
    - undeprecate --loop, and map it from --loop-playlist to --loop-file (the
      deprecation was to make sure no API user gets broken by a sudden behavior
      change)
    - remove previously deprecated vf_eq
    - remove that hardware deinterlace filters (vavpp, d3d11vpp, vdpaupp)
      changed their deinterlacing-enabled setting depending on what the
      --deinterlace option or property was set to. Now, a filter always does
      what its filter options and defaults imply. The --deinterlace option and
      property strictly add/remove its own filters. For example, if you run
      "mpv --vf=vavpp --deinterlace=yes", this will insert another, redundant
      filter, which is probably not what you want. For toggling a deinterlace
      filter manually, use the "vf toggle" command, and do not set the
      deinterlace option/property. To customize the filter that will be
      inserted automatically, use --vf-defaults. Details how this works will
      probably change in the future.
    - remove deinterlace=auto (this was not deprecated, but had only a very
      obscure use that stopped working with the change above. It was also
      prone to be confused with a feature not implemented by it: auto did _not_
      mean that deinterlacing was enabled on demand.)
    - add shortened mnemonic names for mouse button bindings, eg. mbtn_left
      the old numeric names (mouse_btn0) are deprecated
    - remove mouse_btn3_dbl and up, since they are only generated for buttons
      0-2 (these now print an error when sent from the 'mouse' command)
    - rename the axis bindings to wheel_up/down/etc. axis scrolling and mouse
      wheel scrolling are now conceptually the same thing
      the old axis_up/down names remain as deprecated aliases
 --- mpv 0.26.0 ---
    - remove remaining deprecated audio device options, like --alsa-device
      Some of them were removed in earlier releases.
    - introduce --replaygain... options, which replace the same functionality
      provided by the deprecated --af=volume:replaygain... mechanism.
    - drop the internal "mp-rawvideo" codec (used by --demuxer=rawvideo)
    - rename --sub-ass-style-override to --sub-ass-override, and rename the
      `--sub-ass-override=signfs` setting to `--sub-ass-override=scale`.
    - change default of --video-aspect-method to "bitstream". The "hybrid"
      method (old default) is deprecated.
    - remove property "video-params/nom-peak"
    - remove option --target-brightness
    - replace vf_format's `peak` suboption by `sig-peak`, which is relative to
      the reference white level instead of in cd/m^2
    - renamed the TRCs `st2084` and `std-b67` to `pq` and `hlg` respectively
    - the "osd" command is deprecated (use "cycle osd-level")
    - --field-dominance is deprecated (use --vf=setfield=bff or tff)
    - --really-quiet subtle behavior change
    - the deprecated handling of setting "no-" options via client API is dropped
    - the following options change to append-by-default (and possibly separator):
        --script
      also, the following options are deprecated:
        --sub-paths => --sub-file-paths
      the following options are deprecated for setting via API:
        "script" (use "scripts")
        "sub-file" (use "sub-files")
        "audio-file" (use "audio-files")
        "external-file" (use "external-files")
        (the compatibility hacks for this will be removed after this release)
    - remove property `vo-performance`, and add `vo-passes` as a more general
      replacement
    - deprecate passing multiple arguments to -add/-pre options (affects the
      vf/af commands too)
    - remove --demuxer-lavf-cryptokey. Use --demux-lavf-o=cryptokey=<hex> or
      --demux-lavf-o=decryption_key=<hex> instead (whatever fits your situation).
    - rename --opengl-dumb-mode=no to --opengl-dumb-mode=auto, and make `no`
      always disable it (unless forced on by hardware limitation).
    - generalize --scale-clamp, --cscale-clamp etc. to accept a float between
      0.0 and 1.0 instead of just being a flag. A value of 1.0 corresponds to
      the old `yes`, and a value of 0.0 corresponds to the old `no`.
 --- mpv 0.25.0 ---
    - remove opengl-cb dxva2 dummy hwdec interop
      (see git "vo_opengl: remove dxva2 dummy hwdec backend")
    - remove ppm, pgm, pgmyuv, tga choices from the --screenshot-format and
      --vo-image-format options
    - the "jpeg" choice in the option above now leads to a ".jpg" file extension
    - --af=drc is gone (you can use e.g. lavfi/acompressor instead)
    - remove image_size predefined uniform from OpenGL user shaders. Use
      input_size instead
    - add --sub-filter-sdh
    - add --sub-filter-sdh-harder
    - remove --input-app-events option (macOS)
    - deprecate most --vf and --af filters. Only some filters not in libavfilter
      will be kept.
      Also, you can use libavfilter filters directly (e.g. you can use
      --vf=name=opts instead of --vf=lavfi=[name=opts]), as long as the
      libavfilter filter's name doesn't clash with a mpv builtin filter.
      In the long term, --vf/--af syntax might change again, but if it does, it
      will switch to libavfilter's native syntax. (The above mentioned direct
      support for lavfi filters still has some differences, such as how strings
      are escaped.) If this happens, the non-deprecated builtin filters might be
      moved to "somewhere else" syntax-wise.
    - deprecate --loop - after a deprecation period, it will be undeprecated,
      but changed to alias --loop-file
    - add --keep-open-pause=no
    - deprecate --demuxer-max-packets
    - change --audio-file-auto default from "exact" to "no" (mpv won't load
      files with the same filename as the video, but different extension, as
      audio track anymore)
 --- mpv 0.24.0 ---
    - deprecate --hwdec-api and replace it with --opengl-hwdec-interop.
      The new option accepts both --hwdec values, as well as named backends.
      A minor difference is that --hwdec-api=no (which used to be the default)
      now actually does not preload any interop layer, while the new default
      ("") uses the value of --hwdec.
    - drop deprecated --ad/--vd features
    - drop deprecated --sub-codepage syntax
    - rename properties:
        - "drop-frame-count" to "decoder-frame-drop-count"
        - "vo-drop-frame-count" to "frame-drop-count"
      The old names still work, but are deprecated.
    - remove the --stream-capture option and property. No replacement.
      (--record-file might serve as alternative)
    - add --sub-justify
    - add --sub-ass-justify
    - internally there's a different way to enable the demuxer cache now
      it can be auto-enabled even if the stream cache remains disabled
 --- mpv 0.23.0 ---
    - remove deprecated vf_vdpaurb (use "--hwdec=vdpau-copy" instead)
    - the following properties now have new semantics:
        - "demuxer" (use "current-demuxer")
        - "fps" (use "container-fps")
        - "idle" (use "idle-active")
        - "cache" (use "cache-percent")
        - "audio-samplerate" (use "audio-params/samplerate")
        - "audio-channels" (use "audio-params/channel-count")
        - "audio-format" (use "audio-codec-name")
      (the properties equivalent to the old semantics are in parentheses)
    - remove deprecated --vo and --ao sub-options (like --vo=opengl:...), and
      replace them with global options. A somewhat complete list can be found
      here: https://github.com/mpv-player/mpv/wiki/Option-replacement-list#mpv-0210
    - remove --vo-defaults and --ao-defaults as well
    - remove deprecated global sub-options (like -demuxer-rawaudio format=...),
      use flat options (like --demuxer-rawaudio-format=...)
    - the --sub-codepage option changes in incompatible ways:
        - detector-selection and fallback syntax is deprecated
        - enca/libguess are removed and deprecated (behaves as if they hadn't
          been compiled-in)
        - --sub-codepage=<codepage> does not force the codepage anymore
          (this requires different and new syntax)
    - remove --fs-black-out-screens option for macOS
    - change how spdif codecs are selected. You can't enable spdif passthrough
      with --ad anymore. This was deprecated; use --audio-spdif instead.
    - deprecate the "family" selection with --ad/--vd
      forcing/excluding codecs with "+", "-", "-" is deprecated as well
    - explicitly mark --ad-spdif-dtshd as deprecated (it was done so a long time
      ago, but it didn't complain when using the option)
 --- mpv 0.22.0 ---
    - the "audio-device-list" property now sets empty device description to the
      device name as a fallback
    - add --hidpi-window-scale option for macOS
    - add audiounit audio output for iOS
    - make --start-time work with --rebase-start-time=no
    - add --opengl-early-flush=auto mode
    - add --hwdec=vdpau-copy, deprecate vf_vdpaurb
    - add tct video output for true-color and 256-color terminals
 --- mpv 0.21.0 ---
    - unlike in older versions, setting options at runtime will now take effect
      immediately (see for example issue #3281). On the other hand, it will also
      do runtime verification and reject option changes that do not work
      (example: setting the "vf" option to a filter during playback, which fails
      to initialize - the option value will remain at its old value). In general,
      "set name value" should be mostly equivalent to "set options/name value"
      in cases where the "name" property is not deprecated and "options/name"
      exists - deviations from this are either bugs, or documented as caveats
      in the "Inconsistencies between options and properties" manpage section.
    - deprecate _all_ --vo and --ao suboptions. Generally, all suboptions are
      replaced by global options, which do exactly the same. For example,
      "--vo=opengl:scale=nearest" turns into "--scale=nearest". In some cases,
      the global option is prefixed, e.g. "--vo=opengl:pbo" turns into
      "--opengl-pbo".
      Most of the exact replacements are documented here:
        https://github.com/mpv-player/mpv/wiki/Option-replacement-list
    - remove --vo=opengl-hq. Set --profile=opengl-hq instead. Note that this
      profile does not force the VO. This means if you use the --vo option to
      set another VO, it won't work. But this also means it can be used with
      opengl-cb.
    - remove the --vo=opengl "pre-shaders", "post-shaders" and "scale-shader"
      sub-options: they were deprecated in favor of "user-shaders"
    - deprecate --vo-defaults (no replacement)
    - remove the vo-cmdline command. You can set OpenGL renderer options
      directly via properties instead.
    - deprecate the device/sink options on all AOs. Use --audio-device instead.
    - deprecate "--ao=wasapi:exclusive" and "--ao=coreaudio:exclusive",
      use --audio-exclusive instead.
    - subtle changes in how "--no-..." options are treated mean that they are
      not accessible under "options/..." anymore (instead, these are resolved
      at parsing time). This does not affect options which start with "--no-",
      but do not use the mechanism for negation options.
      (Also see client API change for API version 1.23.)
    - rename the following properties
        - "demuxer" -> "current-demuxer"
        - "fps" -> "container-fps"
        - "idle" -> "idle-active"
        - "cache" -> "cache-percent"
      the old names are deprecated and will change behavior in mpv 0.23.0.
    - remove deprecated "hwdec-active" and "hwdec-detected" properties
    - deprecate the ao and vo auto-profiles (they never made any sense)
    - deprecate "--vo=direct3d_shaders" - use "--vo=direct3d" instead.
      Change "--vo=direct3d" to always use shaders by default.
    - deprecate --playlist-pos option, renamed to --playlist-start
    - deprecate the --chapter option, as it is redundant with --start/--end,
      and conflicts with the semantics of the "chapter" property
    - rename --sub-text-* to --sub-* and --ass-* to --sub-ass-* (old options
      deprecated)
    - incompatible change to cdda:// protocol options: the part after cdda://
      now always sets the device, not the span or speed to be played. No
      separating extra "/" is needed. The hidden --cdda-device options is also
      deleted (it was redundant with the documented --cdrom-device).
    - deprecate --vo=rpi. It will be removed in mpv 0.23.0. Its functionality
      was folded into --vo=opengl, which now uses RPI hardware decoding by
      treating it as a hardware overlay (without applying GL filtering). Also
      to be changed in 0.23.0: the --fs flag will be reset to "no" by default
      (like on the other platforms).
    - deprecate --mute=auto (informally has been since 0.18.1)
    - deprecate "resume" and "suspend" IPC commands. They will be completely
      removed in 0.23.0.
    - deprecate mp.suspend(), mp.resume(), mp.resume_all() Lua scripting
      commands, as well as setting mp.use_suspend. They will be completely
      removed in 0.23.0.
    - the "seek" command's absolute seek mode will now interpret negative
      seek times as relative from the end of the file (and clamps seeks that
      still go before 0)
    - add almost all options to the property list, meaning you can change
      options without adding "options/" to the property name (a new section
      has been added to the manpage describing some conflicting behavior
      between options and properties)
    - implement changing sub-speed during playback
    - make many previously fixed options changeable at runtime (for example
      --terminal, --osc, --ytdl, can all be enable/disabled after
      mpv_initialize() - this can be extended to other still fixed options
      on user requests)
 --- mpv 0.20.0 ---
    - add --image-display-duration option - this also means that image duration
      is not influenced by --mf-fps anymore in the general case (this is an
      incompatible change)
 --- mpv 0.19.0 ---
    - deprecate "balance" option/property (no replacement)
 --- mpv 0.18.1 ---
    - deprecate --heartbeat-cmd
    - remove --softvol=no capability:
        - deprecate --softvol, it now does nothing
        - --volume, --mute, and the corresponding properties now always control
          softvol, and behave as expected without surprises (e.g. you can set
          them normally while no audio is initialized)
        - rename --softvol-max to --volume-max (deprecated alias is added)
        - the --volume-restore-data option and property are removed without
          replacement. They were _always_ internal, and used for watch-later
          resume/restore. Now --volume/--mute are saved directly instead.
        - the previous point means resuming files with older watch-later configs
          will print an error about missing --volume-restore-data (which you can
          ignore), and will not restore the previous value
        - as a consequence, volume controls will no longer control PulseAudio
          per-application value, or use the system mixer's per-application
          volume processing
        - system or per-application volume can still be controlled with the
          ao-volume and ao-mute properties (there are no command line options)
 --- mpv 0.18.0 ---
    - now ab-loops are active even if one of the "ab-loop-a"/"-b" properties is
      unset ("no"), in which case the start of the file is used if the A loop
      point is unset, and the end of the file for an unset B loop point
    - deprecate --sub-ass=no option by --ass-style-override=strip
      (also needs --embeddedfonts=no)
    - add "hwdec-interop" and "hwdec-current" properties
    - deprecated "hwdec-active" and "hwdec-detected" properties (to be removed
      in mpv 0.20.0)
    - choice option/property values that are "yes" or "no" will now be returned
      as booleans when using the mpv_node functions in the client API, the
      "native" property accessors in Lua, and the JSON API. They can be set as
      such as well.
    - the VO opengl fbo-format sub-option does not accept "rgb" or "rgba"
      anymore
    - all VO opengl prescalers have been removed (replaced by user scripts)
 --- mpv 0.17.0 ---
    - deprecate "track-list/N/audio-channels" property (use
      "track-list/N/demux-channel-count" instead)
    - remove write access to "stream-pos", and change semantics for read access
    - Lua scripts now don't suspend mpv by default while script code is run
    - add "cache-speed" property
    - rename --input-unix-socket to --input-ipc-server, and make it work on
      Windows too
    - change the exact behavior of the "video-zoom" property
    - --video-unscaled no longer disables --video-zoom and --video-aspect
      To force the old behavior, set --video-zoom=0 and --video-aspect=0
 --- mpv 0.16.0 ---
    - change --audio-channels default to stereo (use --audio-channels=auto to
      get the old default)
    - add --audio-normalize-downmix
    - change the default downmix behavior (--audio-normalize-downmix=yes to get
      the old default)
    - VO opengl custom shaders must now use "sample_pixel" as function name,
      instead of "sample"
    - change VO opengl scaler-resizes-only default to enabled
    - add VO opengl "interpolation-threshold" suboption (introduces new default
      behavior, which can change e.g. ``--video-sync=display-vdrop`` to the
      worse, but is usually what you want)
    - make "volume" and "mute" properties changeable even if no audio output is
      active (this gives not-ideal behavior if --softvol=no is used)
    - add "volume-max" and "mixer-active" properties
    - ignore --input-cursor option for events injected by input commands like
      "mouse", "keydown", etc.
 --- mpv 0.15.0 ---
    - change "yadif" video filter defaults
 --- mpv 0.14.0 ---
    - vo_opengl interpolation now requires --video-sync=display-... to be set
    - change some vo_opengl defaults (including changing tscale)
    - add "vsync-ratio", "estimated-display-fps" properties
    - add --rebase-start-time option
      This is a breaking change to start time handling. Instead of making start
      time handling an aspect of different options and properties (like
      "time-pos" vs. "playback-time"), make it dependent on the new option. For
      compatibility, the "time-start" property now always returns 0, so code
      which attempted to handle rebasing manually will not break.
 --- mpv 0.13.0 ---
    - remove VO opengl-cb frame queue suboptions (no replacement)
 --- mpv 0.12.0 ---
    - remove --use-text-osd (useless; fontconfig isn't a requirement anymore,
      and text rendering is also lazily initialized)
    - some time properties (at least "playback-time", "time-pos",
      "time-remaining", "playtime-remaining") now are unavailable if the time
      is unknown, instead of just assuming that the internal playback position
      is 0
    - add --audio-fallback-to-null option
    - replace vf_format outputlevels suboption with "video-output-levels" global
      property/option; also remove "colormatrix-output-range" property
    - vo_opengl: remove sharpen3/sharpen5 scale filters, add sharpen sub-option
 --- mpv 0.11.0 ---
    - add "af-metadata" property
 --- mpv 0.10.0 ---
    - add --video-aspect-method option
    - add --playlist-pos option
    - add --video-sync* options
      "display-sync-active" property
      "vo-missed-frame-count" property
      "audio-speed-correction" and "video-speed-correction" properties
    - remove --demuxer-readahead-packets and --demuxer-readahead-bytes
      add --demuxer-max-packets and --demuxer-max-bytes
      (the new options are not replacement and have very different semantics)
    - change "video-aspect" property: always settable, even if no video is
      running; always return the override - if no override is set, return
      the video's aspect ratio
    - remove disc-nav (DVD, BD) related properties and commands
    - add "option-info/<name>/set-locally" property
    - add --cache-backbuffer; change --cache-default default to 75MB
      the new total cache size is the sum of backbuffer and the cache size
      specified by --cache-default or --cache
    - add ``track-list/N/audio-channels`` property
    - change --screenshot-tag-colorspace default value
    - add --stretch-image-subs-to-screen
    - add "playlist/N/title" property
    - add --video-stereo-mode=no to disable auto-conversions
    - add --force-seekable, and change default seekability in some cases
    - add vf yadif/vavpp/vdpaupp interlaced-only suboptions
      Also, the option is enabled by default (Except vf_yadif, which has
      it enabled only if it's inserted by the deinterlace property.)
    - add --hwdec-preload
    - add ao coreaudio exclusive suboption
    - add ``track-list/N/forced`` property
    - add audio-params/channel-count and ``audio-params-out/channel-count props.
    - add af volume replaygain-fallback suboption
    - add video-params/stereo-in property
    - add "keypress", "keydown", and "keyup" commands
    - deprecate --ad-spdif-dtshd and enabling passthrough via --ad
      add --audio-spdif as replacement
    - remove "get_property" command
    - remove --slave-broken
    - add vo opengl custom shader suboptions (source-shader, scale-shader,
      pre-shaders, post-shaders)
    - completely change how the hwdec properties work:
        - "hwdec" now reflects the --hwdec option
        - "hwdec-detected" does partially what the old "hwdec" property did
          (and also, "detected-hwdec" is removed)
        - "hwdec-active" is added
    - add protocol-list property
    - deprecate audio-samplerate and audio-channels properties
      (audio-params sub-properties are the replacement)
    - add audio-params and audio-out-params properties
    - deprecate "audio-format" property, replaced with "audio-codec-name"
    - deprecate --media-title, replaced with --force-media-title
    - deprecate "length" property, replaced with "duration"
    - change volume property:
        - the value 100 is now always "unchanged volume" - with softvol, the
          range is 0 to --softvol-max, without it is 0-100
        - the minimum value of --softvol-max is raised to 100
    - remove vo opengl npot suboption
    - add relative seeking by percentage to "seek" command
    - add playlist_shuffle command
    - add --force-window=immediate
    - add ao coreaudio change-physical-format suboption
    - remove vo opengl icc-cache suboption, add icc-cache-dir suboption
    - add --screenshot-directory
    - add --screenshot-high-bit-depth
    - add --screenshot-jpeg-source-chroma
    - default action for "rescan_external_files" command changes
 --- mpv 0.9.0 ---
