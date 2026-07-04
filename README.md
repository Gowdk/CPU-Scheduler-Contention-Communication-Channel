# CPU-Scheduler-Contention-Communication-Channel

To run:
  1) Download a copy of the "Finished Channel" directory.
  2) Navigate to this directory on your file system.
  3) run "make"
  4) Then run (in any order):
     --> ./consumer -c 2 -s 100
       -c specifies the CPU to attach to. Default = 2
       -s specifies the the interval at which symbols are sent. Default = 100

     --> ./producer -c 2 -s 100 -r 10 "This is my message"
       -c specifies the CPU to attach to. Default = 2
       -s specifies the the interval at which symbols are sent. Default = 100
       -r specifies how many frames of a given message will be sent.
       Any message between quotations will be considered the message to be sent/received. Maximum length = 255 characters.
