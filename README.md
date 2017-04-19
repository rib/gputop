[![Build Status](https://travis-ci.org/rib/gputop.svg?branch=master)](https://travis-ci.org/rib/gputop)

# gputop

GPU Top is a tool to help developers understand GPU performance counters and provide graphical and machine readable data for the performance analysis of drivers and applications. GPU Top is compatible with all GPU programming apis such as OpenGL, OpenCL or Vulkan since it primarily deals with capturing periodic sampled metrics.

GPU Top so far includes a web based interactive UI as well as a non-interactive CSV logging tool suited to being integrated into continuous regression testing systems. Both of these tools can capture metrics from a remote system so as to try an minimize their impact on the system being profiled.

GPUs supported so far include: Haswell, Broadwell, Cherryview, Skylake, Broxton and Apollo Lake.

It's not necessary to build the web UI from source to use it since the latest tested version is automatically deployed to http://gputop.github.io

If you want to try out GPU Top on real hardware please follow these [build Instructions](https://github.com/rib/gputop/wiki/Build-Instructions) and give feedback [here](https://github.com/rib/gputop/issues).

# Web UI Screenshot
![](https://raw.githubusercontent.com/wiki/rib/gputop/images/webui-screenshot.png)


# CSV output example

Here's an example from running `gputop-csv` like:

```gputop-csv -m RenderBasic -c Timestamp,GpuBusy,EarlyDepthTestFails,RasterizedPixels```

Firstly the tool prints out a header that you might want to share with others to help ensure your comparing apples to apples when looking at metrics from different systems:

```
CSV: Capture Settings:
CSV:   Server: localhost:7890
CSV:   File: STDOUT
CSV:   Metric Set: Render Metrics Basic Gen9
CSV:   Columns: Timestamp,GpuBusy,EarlyDepthTestFails,RasterizedPixels
CSV:   OA Hardware Sampling Exponent: 18
CSV:   OA Hardware Period: 21845333.333333332ns
CSV:   Accumulation period (requested): 1000000000ns
CSV:   Accumulation period (actual): 1004885333.3333333ns (21845333.333333332ns * 46)


CSV: OS Info:
CSV:   Kernel Build: #125 SMP PREEMPT Thu Mar 23 18:58:23 GMT 2017
CSV:   Kernel Release: 4.11.0-rc3-drm-intel+


CSV: CPU Info:
CSV:   Model: Intel(R) Core(TM) i5-6440HQ CPU @ 2.60GHz
CSV:   N Cores: 4


CSV: GPU Info:
CSV:   Model: Skylake GT2
CSV:   N EUs: 24
CSV:   N EU Slices: 1
CSV:   N EU Sub Slices Per Slice: 3
CSV:   EU Threads Count (total): 168
CSV:   Min Frequncy: 350000000Hz
CSV:   Max Frequncy: 950000000Hz
CSV:   Timestamp Frequncy: 12000000Hz


CSV: Capture Notices:
CSV:   - RC6 power saving mode disabled

```

And then compactly prints the data collected. In this case the output was to a terminal and so the data is presented to be easily human readable. When output to a file then it will be a plain CSV file and numbers aren't rounded.

```
TIME           GPU   EARLY        RASTERIZED
STAMP          BUSY  DEPTH        PIXELS
                     TEST
                     FAILS
(ns)           (%)   (pixels/s)   (pixels/s)
306479502074,  0.0,  0.0,         0.0
307494911974,  1.2,  0.0,         23.7MP
308499797292,  2.4,  0.0,         47.5MP
309504682606,  1.7,  0.0,         31.8MP
310509567920,  33.2, 9.0MP,       605.1MP
311514453222,  99.7, 60.8MP,      2.3GP
312519338495,  99.7, 64.9MP,      2.2GP
313522150384,  99.8, 64.8MP,      2.2GP
314525057526,  99.7, 64.9MP,      2.2GP
315531271583,  99.7, 65.1MP,      2.2GP
316537554684,  99.8, 63.4MP,      2.2GP
317543184535,  99.7, 62.9MP,      2.2GP
318548650092,  99.7, 63.5MP,      2.2GP
```
