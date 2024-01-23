    Detect camera:
      # v4l2-ctl -l

    GStreamer live stream:
      # gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=1280,height=1024 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false

    Capture one raw frame:
      # v4l2-ctl --set-fmt-video=width=1280,height=1024 --set-ctrl bypass_mode=0 --stream-mmap --stream-count=1 --stream-to=frame_001.raw

    dione_ir module options:
      - quick_mode
          0 keeps the camera in reset state when streaming isn't active
          1 prevents the module from resetting the camera when streaming has finished
          default value is 1
      - test_mode
          enables, or disables test pattern generation by the camera
          default value is 0

    Changing module options after startup:
      # rmmod dione_ir
      # modprobe dione_ir test_mode=1



    Python code to change test pattern.
    The following commands work only with quick_mode=1 and test_mode=0
    - in one terminal window execute:
      # gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=1280,height=1024 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false
    - image stream shall appear on Weston desktop

    - in another terminal window:
      # python3
      >>> import dione1280
      >>> di=dione1280.dione1280(dev_addr=0x5b)
      >>> di.ack_stop()
    - retry the last one if there was an I/O error
      >>> hex(di.write_reg32(0x00080108, 0))
      >>> di.ack_start()

    - in the 1st window stop streaming by pressing Ctrl-C
    - restart it as above
    - image stream shall change to a moving test pattern

    - test pattern can be changed in the Python window:
      >>> hex(di.write_reg32(0x0008012c, 0x8072))
    - no ack_stop/ack_start required here
      >>> hex(di.write_reg32(0x0008012c, 0x8071))
      >>> hex(di.write_reg32(0x0008012c, 0x8070))
      >>> hex(di.write_reg32(0x0008012c, 0x8073))

    - changing back to thermal stream:
      >>> di.ack_stop()
      >>> hex(di.write_reg32(0x00080108, 1))
      >>> di.ack_start()
    - restart "gst-launch-1.0" in the 1st window
